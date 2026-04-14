# Teleop 端到端信号与接口契约

本文档定义 Client、MQTT、ZLMediaKit、carla-bridge（及实车 Vehicle-side）之间的**控制面、媒体面、状态面**接口，作为实现与联调的唯一参考。若代码与本文冲突，应以本文为准并修正代码。

## 1. 控制面（车端 / 仿真桥必须可达）

### 1.1 传输

- **唯一标准路径**：JSON 消息发布到 MQTT 主题 **`vehicle/control`**（可配置，默认如此）。
- **不得**将底盘 / 会话类控制 JSON 经 Client↔ZLM 的 WebRTC DataChannel 作为到达车端的路径：ZLM 不向 MQTT 转发 DataChannel（见 `docs/ZLM_DATACHANNEL_CAPABILITY.md`）。
- **QoS**：`start_stream`、`stop_stream` 使用 **QoS 1**；其余控制消息可为 QoS 0（低延迟），可按部署调整。

### 1.2 载荷

- UTF-8 JSON 对象；字段 **`type`** 必填（车端 `Vehicle-side/src/control_protocol.cpp` 对缺少 `type` 的消息会直接拒绝，避免绕过远驾使能检查）。
- **`vin`**：除明确允许省略的场景外，应由客户端填入当前选中车辆 VIN；车端/桥按 VIN 过滤（空 VIN 或匹配本机 VIN 时处理）。
- **`schemaVersion`**：semver 字符串，当前与 `mqtt/schemas/vehicle_control.json` 对齐为 **`1.2.0`**；由 `MqttControlEnvelope::prepareForSend` 在缺失时补齐（高频 `drive` 由 `VehicleControlService` 在签名前写入）。
- **`timestampMs`**、**`seq`**：由 `prepareForSend` 在缺失时补齐（毫秒时间戳；与 JSON Schema 及车端防重放解析一致）。
- **Shell 运维示例**：仓库内 `scripts/lib/mqtt_control_json.sh` 生成与 Schema 一致的 `mosquitto_pub -m` 载荷；所有 `scripts/*.sh` 中的 `vehicle/control` 发布应通过该库或等价字段，避免手写残缺 JSON。
- 会话字段名：**`sessionId`**（camelCase）；参与 HMAC 的 canonical 字段与之一致（见 `CommandSigner`）。

### 1.3 车端/桥必须处理的 `type`（最小集）

| type | 语义 |
|------|------|
| `start_stream` | 开始推流到 ZLM |
| `stop_stream` | 停止推流 |
| `remote_control` | 远驾使能，`enable` 布尔 |
| `drive` | 综合驾驶指令（转向/油门/刹车/档、**`emergency_stop` 布尔**，Schema 必填） |
| `steering` / `throttle` / `brake` / `gear` / `target_speed` / `emergency_stop` / `sweep` / `mode` | 分项控制（与现有客户端一致） |

### 1.4 客户端就绪条件

- **可发控车指令**当且仅当 **MQTT broker 已连接** 且 **VIN 已设置**（与 `MqttController::mqttBrokerConnected` 及选车状态一致）。
- 前向视频 WebRTC 连通 **不**代表可发控车指令。

---

## 2. 状态面（车 → Client）

### 2.1 主题

- **`vehicle/status`**：全局状态（默认）。
- **`vehicle/<VIN>/status`**：按车订阅时可选。

### 2.2 载荷约定

- 同一主题上为多类消息，由 **`type`** 区分；`mqtt/schemas/vehicle_status.json` 以 `oneOf` 描述：**`vehicle_status`**（周期底盘）、**`remote_control_ack`**、**`encoder_hint_ack`**、**`offline`**（如 MQTT 遗嘱）。
- 时间字段：状态面使用 **`timestamp`**（毫秒 epoch），与控制面的 **`timestampMs`** 并存；命名在 Schema 中分别固定，避免混用。
- 所有状态消息应携带 **`schemaVersion`**（当前 **`1.2.0`**），与 golden 样例及 carla-bridge / Vehicle-side 发布格式一致。
- `encoder_hint_ack`：车端/桥对 `teleop/client_encoder_hint`（payload `type: client_video_encoder_hint`）的应答（可选）。

---

## 3. 媒体面（ZLM）

### 3.1 推流（桥 / 车端 → ZLM）

- **协议**：RTMP（默认 `rtmp://<ZLM_HOST>:1935/<app>/<stream>`）。
- **app**：默认 `teleop`（`ZLM_APP`）。
- **stream 命名（当前实现真源）**：四路独立流  
  `{VIN}_cam_front`、`{VIN}_cam_rear`、`{VIN}_cam_left`、`{VIN}_cam_right`。  
  VIN 为空时退化为无前缀的 `cam_front` 等（仅用于单车测试）。

### 3.2 拉流（Client → ZLM）

- **信令**：HTTP `…/index/api/webrtc?app=teleop&stream=<上表之一>&type=play`（WHEP 风格）。
- **Backend WHEP 占位 URL**：`backend/src/main.cpp` 中 `build_whep_url` 使用代表性流名 `{vin}_cam_front`；客户端解析 **host + app** 后自行拼接四路 `stream`，与上表一致。

### 3.3 与文档 M2 的差异说明

- `docs/M2_WHIPPUBLISHER_SPEC.md` 中曾示例 `stream=<VIN>-<SESSION_ID>` 的单流会话模型；**当前仓库仿真与 Backend 已实现路径为 VIN 前缀四路流**。若未来切换为会话级流名，须同时改：Backend、Client `WebRtcStreamManager`、carla-bridge / Vehicle-side 推流端。

---

## 4. 编码提示（可选）

- **MQTT**：`teleop/client_encoder_hint`（JSON）。
- **DataChannel**：仅作客户端→ZLM 侧的 hint 通道；不替代 `vehicle/control`。

---

## 5. carla-bridge 实现形态

- **C++**：容器/进程内 MQTT + RTMP 推流（见 `carla-bridge/cpp/`）。
- **Python**：`carla_bridge.py`（CARLA 相机 + ffmpeg）；流名与 MQTT 语义与 C++ 对齐。
- 部署时须明确运行哪一种，避免混用两套进程连同一 VIN。

---

## 6. 验证

- `scripts/verify-client-to-carla-step-by-step.sh`：全链路冒烟。
- `scripts/verify-carla-by-logs.sh`：桥侧日志与可选 ZLM `getMediaList`。
