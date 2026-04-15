# 远程驾驶系统：客户端接口规范 (v1.3)
# Remote Driving System: Client Interface Specification (v1.3)

## 1. 概述 (Overview)

本规范定义了远程驾驶客户端与后端（Backend）、车端（Vehicle-side）以及流媒体服务器（ZLMediaKit）之间的交互契约。本规范遵循 Google 工程实践标准，旨在为开发人员提供明确、一致且可验证的接口定义。

### 1.1 目标范围 (Scope)
- **控制面**: 基于 MQTT 的高频驾驶指令下发。
- **状态面**: 基于 MQTT 的遥测数据与故障码上报。
- **媒体面**: 基于 WebRTC/WHEP 的低时延视频拉流。
- **业务面**: 基于 REST API 的鉴权、会话与 VIN 管理。
- **表现层**: C++ 与 QML 之间的属性绑定与方法调用。

### 1.2 核心原则 (Design Principles)
- **强类型校验**: 所有 JSON 消息必须符合 `mqtt/schemas/` 中的 JSON Schema 约束。
- **确定性时延**: 控制指令环路必须保证 100Hz 确定性（Jitter < 5ms）。
- **失效安全 (Fail-safe)**: 任何接口层面的心跳丢失必须在 500ms 内触发安全停车。
- **显式版本化**: 接口变更必须 bump `schemaVersion` 或 API version，确保向后兼容。

---

## 2. 通信协议栈 (Communication Stack)

| 维度　　　　　| 协议　　　　　　　　　　　　| 用途　　　　　　　　　　| 关键属性　　　　　　　　　　　　　　　　　 |
| :--------------| :----------------------------| :------------------------| :-------------------------------------------|
| **信令/控制** | MQTT over TLS (v3.1.1+)　　 | 指令、遥测、事件　　　　| QoS 0/1, 100Hz (Control), 20Hz (Telemetry) |
| **媒体/视频** | WebRTC / WHEP　　　　　　　 | 实时监控视频　　　　　　| UDP/SRTP, < 150ms E2E Latency　　　　　　　|
| **业务/鉴权** | HTTPS / REST / OIDC (JWT)　 | 登录、选车、会话建立　　| API-Version Negotiation, Bearer Token　　　|
| **内部桥接**　| Qt Property / Signal / Slot | C++ 逻辑与 QML 界面交互 | 线程安全, 声明式绑定　　　　　　　　　　　 |

---

## 3. 业务接口定义 (REST API Plane)

所有请求必须包含 `Authorization: Bearer <JWT>` 头部，且建议包含 `API-Version: 1.1.0` 头部进行版本协商。

### 3.1 版本协商 (Version Negotiation)
- **客户端发送**: `API-Version: x.y.z`
- **服务端返回**: `API-Version` 响应头（指示实际使用的版本）。
- **兼容性**: 
  - 大版本变更（如 v2）路径会变更为 `/api/v2/`。
  - 小版本变更（如 v1.1.0）保持向后兼容。

### 3.2 核心端点 (Core Endpoints)

| 方法　 | 路径　　　　　　　　　　　　　 | 描述　　　　　　 | 关键响应/参数　　　　　　　　　　　　　　　　　|
| :-------| :-------------------------------| :-----------------| :-----------------------------------------------|
| `GET`　| `/api/v1/me`　　　　　　　　　 | 获取当前用户信息 | `username`, `roles`, `apiVersion`　　　　　　　|
| `GET`　| `/api/v1/vins`　　　　　　　　 | 获取可见车辆列表 | `vins[]` (含 `status`, `capabilities`)　　　　 |
| `POST` | `/api/v1/vins/{vin}/sessions`　| 创建远驾会话　　 | 返回 `sessionId`, `media` URLs, `control` 配置 |
| `POST` | `/api/v1/sessions/{id}/end`　　| 显式结束会话　　 | `reason`　　　　　　　　　　　　　　　　　　　 |
| `POST` | `/api/v1/sessions/{id}/unlock` | 释放控制锁　　　 | `unlocked: true`　　　　　　　　　　　　　　　 |

### 3.3 会话响应对象 (Session Response)
创建会话成功后，服务端返回包含流地址和控制配置的复杂对象：
- **`media`**: 包含 `whep` (拉流) 和 `whip` (推流) 的 URI。
- **`control`**: 
  - `algo`: `HMAC-SHA256` 或 `NONE`。
  - `mqttConfig`: 包含 `brokerUrl`, `clientId`, `topics` (control/status)。

---

## 4. MQTT 接口定义 (Data Plane)

### 3.1 控制指令 (Outgoing: `vehicle/control`)
客户端每 10ms (100Hz) 发送一次控制包。

**消息 Schema**: `urn:teleop:schemas:vehicle_control:1.2.0`
**示例 (Drive Command)**:
```json
{
  "type": "drive",
  "vin": "VIN1234567890",
  "schemaVersion": "1.2.0",
  "seq": 1024,
  "timestampMs": 1713100000000,
  "steering": 0.5,
  "throttle": 0.2,
  "brake": 0.0,
  "gear": 1,
  "emergency_stop": false,
  "sessionId": "sess_888",
  "signature": "hmac_sha256_hash"
}
```

**关键字段约束**:
- `steering`: [-1.0, 1.0]，负值为左，正值为右。
- `throttle`: [0.0, 1.0]，归一化油门深度。
- `brake`: [0.0, 1.0]，归一化刹车深度。
- `gear`: -1 (R), 0 (N), 1 (D), 2 (P)。
- `seq`: 单调递增 uint32，用于防重放检查。

### 3.2 遥测状态 (Incoming: `vehicle/status`)
车端/桥接端以 20Hz 频率上报。

**消息 Schema**: `urn:teleop:schemas:vehicle_status:1.2.0`
**关键字段**:
- `speed`: 车速 (km/h)。
- `battery`: 电量 (0-100%)。
- `network_rtt`: 链路往返时延 (ms)。
- `remote_control_enabled`: 车辆当前是否处于远驾接管模式。

---

## 4. 视频流接口 (Media Plane)

客户端使用 WHEP 协议从 ZLMediaKit 拉取四路视频。

### 4.1 流命名约定
每个 VIN 对应四路固定流名：
- `{VIN}_cam_front`: 前视主摄像头
- `{VIN}_cam_rear`: 后视摄像头
- `{VIN}_cam_left`: 左侧盲区摄像头
- `{VIN}_cam_right`: 右侧盲区摄像头

### 4.2 WHEP URL 模板
```
http://<ZLM_HOST>:<PORT>/index/api/webrtc?app=teleop&stream=<STREAM_ID>&type=play&token=<SESSION_TOKEN>
```

---

## 5. C++ 与 QML 交互接口 (Internal Bridge)

客户端通过 `AppContext` 将核心服务暴露给 QML，并使用 **DrivingFacade (v3)** 模式解耦。

### 5.1 全局上下文 (AppContext)
`AppContext` 是一个单例，导出到 QML 的全局命名空间。
- **`mqttController`**: `MqttController*`
- **`vehicleStatus`**: `TelemetryModel*`
- **`safetyMonitor`**: `SafetyMonitorService*`
- **`webrtcStreamManager`**: `WebRtcStreamManager*`
- **`vehicleControl`**: `VehicleControlService*`
- **`vehicleManager`**: `VehicleManager*`
- **`systemStateMachine`**: `SystemStateMachine*`

### 5.2 `VehicleManager` (车辆资源管理)
**属性 (Properties)**:
- `vehicleList`: QStringList (只读) - 可见 VIN 列表。
- `currentVin`: QString - 当前选中的车辆 VIN。
- `lastSessionId`: QString (只读) - 最近一次成功的会话 ID。

**方法 (Invokable Methods)**:
- `refreshVehicleList(QString server, QString token)`: 刷新车辆列表。
- `startSessionForCurrentVin(QString server, QString token)`: 为当前选定车辆申请创建会话。

### 5.3 `MqttController` (控制中心)
**属性 (Properties)**:
- `isConnected`: bool (只读) - 是否已连接到 MQTT Broker。
- `controlChannelReady`: bool (只读) - 控制通道是否已就绪（含鉴权）。

**方法 (Invokable Methods)**:
- `requestStreamStart()`: 发送 `start_stream` MQTT 指令。
- `requestStreamStop()`: 发送 `stop_stream` MQTT 指令。
- `requestRemoteControl(bool enable)`: 切换远驾接管状态 (`remote_control`)。
- `sendGearCommand(int gear)`: 切换档位。

### 5.4 `WebRtcStreamManager` (多路流拉取)
**属性 (Properties)**:
- `frontClient`, `rearClient`, `leftClient`, `rightClient`: `WebRtcClient*` - 四路 WHEP 客户端实例。
- `anyConnected`: bool (只读) - 是否有任意一路视频已连通。

**方法 (Invokable Methods)**:
- `connectFourStreams(QString whepUrl)`: 根据基准 WHEP URL 拉取四路视频（自动添加 VIN 前缀）。
- `disconnectAll()`: 断开所有视频流。

### 5.5 `WebRtcClient` (单路视频客户端)
**方法 (Invokable Methods)**:
- `bindVideoOutput(QObject* videoOutputItem)`: 将视频输出绑定到 QML `VideoOutput`。
- `bindVideoSurface(QObject* surfaceItem)`: 将视频输出绑定到自定义 `RemoteVideoSurface`（零拷贝路径）。

### 5.6 `TelemetryModel` (遥测模型)
**属性 (Properties)**:
- `speed`: double - 当前车速 (km/h)。
- `steering`: double - 当前转向角度 ([-1, 1])。
- `gear`: int - 当前档位 (-1, 0, 1, 2)。
- `battery`: double - 电池电量 (0-100%)。
- `vehicleReady`: bool - 车辆是否处于就绪状态。

### 5.4 `DrivingFacade` (UI 契约)
`DrivingInterface.qml` 作为 UI 根节点，通过 `appServices` 属性暴露服务，禁止子模块直接 `import RemoteDriving`。
```qml
// 子模块调用示例
facade.appServices.mqttController.requestRemoteControl(true)
```

---

## 6. 事件流与生命周期 (Event Flow & Lifecycle)

### 6.1 典型启动序列 (Startup Sequence)
1. **认证**: 用户登录 Keycloak，获取 JWT。
2. **加载**: `GET /api/v1/vins` 加载可见车辆列表。
3. **选择**: 用户点击车辆，触发 `SessionManager::onVinSelected`。
4. **会话**: `POST /api/v1/vins/{vin}/sessions` 创建后端会话，获取 MQTT/WebRTC 配置。
5. **连通**:
   - `MqttController` 连接到 Broker。
   - `WebRtcStreamManager` 连接到四路视频流。
6. **接管**: 用户按下“接管”按钮，发送 `remote_control: true`，收到 `remote_control_ack` 后进入驾驶状态。

### 6.2 实时控制环路 (Real-time Loop)
1. **采样**: `InputSampler` 以 200Hz 采集 HID 输入。
2. **逻辑**: `VehicleControlService` 以 100Hz 频率触发 `controlTick`。
3. **补齐**: 注入 `sessionId`, `vin`, `seq`, `timestamp`。
4. **发送**: 经 `MqttController` 投递到 `vehicle/control`。

---

## 7. 安全与冗余 (Safety & Security)

### 6.1 指令签名 (HMAC)
对于 `type: "drive"` 和 `type: "emergency_stop"` 消息，必须包含 `signature` 字段。
- **算法**: HMAC-SHA256。
- **Payload**: `vin + sessionId + seq + timestampMs + steering + throttle + brake + gear`。
- **Key**: 会话建立时通过后端下发的 `session_secret`。

### 6.2 死手开关 (Deadman Switch)
客户端 UI 必须确保 `Deadman` 逻辑处于按下状态（通常为手柄按钮或屏幕长按）才会下发非零 `throttle`。如果 `Deadman` 释放，客户端必须立即下发 `brake: 1.0`。

### 6.3 网络监控与降级
客户端 `SafetyMonitorService` 持续监控 RTT：
- **RTT < 150ms**: 正常模式 (Green)。
- **150ms < RTT < 300ms**: 警告模式 (Yellow)，提示限速。
- **RTT > 300ms**: 危险模式 (Red)，建议立即停车。
- **断连 > 500ms**: 自动触发 `SAFE_STOP` 指令。

### 6.4 降级策略 (Degradation Policy)
系统根据链路质量自动触发 6 级平滑降级：
- **FULL**: 4K/60fps, 全部通道可用。
- **HIGH**: 1080p/30fps, 全部通道可用。
- **MEDIUM**: 720p/30fps, 主视图 + 缩略图。
- **LOW**: 480p/15fps, 仅主视图。
- **MINIMAL**: 360p/10fps, 仅主视图 + 强制限速 5km/h。
- **SAFETY_STOP**: 触发紧急停车并切断动力。

---

## 7. 错误码规范 (Standard Error Codes)

系统采用 `DOMAIN-CODE` 格式，例如：
- `AUTH-401`: JWT 令牌过期。
- `NET-1001`: MQTT 连接超时。
- `STR-2002`: WebRTC ICE 协商失败。
- `VHL-3005`: 车端急停触发。

---

## 8. 开发对接清单 (Checklist)

1. [ ] **认证**: 获取 Keycloak 凭证并换取 JWT。
2. [ ] **选车**: 调用 `GET /vins` 获取授权列表。
3. [ ] **建连**: 建立 MQTT 连接并订阅 `vehicle/status`。
4. [ ] **拉流**: 根据 `whepUrl` 初始化 WebRtcClient。
5. [ ] **控车**: 开启 100Hz 定时器发送 `drive` JSON 包。
6. [ ] **监控**: 监听 `errorOccurred` 信号并展示 HUD 告警。

---
*本文档为客户端接口的唯一真源 (Single Source of Truth)。*
