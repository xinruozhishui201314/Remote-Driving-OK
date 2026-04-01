# 控制通道配置说明（方案 A）

控制指令与遥测支持多通道传输，可配置优先 DataChannel（经 ZLM），备选 MQTT 或 WebSocket。

## 1. 客户端（Client）

### 1.1 通道类型

| 取值 | 说明 |
|------|------|
| `data_channel` / `webrtc` | 优先使用 WebRTC DataChannel（经 ZLM），需先建立视频拉流连接 |
| `mqtt` | 强制使用 MQTT |
| `websocket` / `ws` | 强制使用 WebSocket（当前实现中回退到 MQTT） |
| （未设置或 `auto`） | 自动：DataChannel 可用时优先，否则 MQTT |

### 1.2 环境变量

- **CONTROL_CHANNEL_PREFERRED**  
  首选控制通道：`data_channel`、`mqtt`、`websocket` 或留空（表示 auto）。

示例：

```bash
# 自动（默认）：有 DataChannel 用 DataChannel，否则 MQTT
export CONTROL_CHANNEL_PREFERRED=

# 强制 MQTT
export CONTROL_CHANNEL_PREFERRED=mqtt

# 优先 DataChannel
export CONTROL_CHANNEL_PREFERRED=data_channel
```

### 1.3 行为说明

- 发送控制指令时，若首选为 `data_channel` 且当前 WebRTC 已连接，则通过 **DataChannel** 发送（经 ZLM 到车端）。
- 若 DataChannel 不可用或首选为 `mqtt`，则通过 **MQTT** 发送。
- **已连接** 判断：MQTT 已连接 **或** DataChannel 已连接，即视为控制通道可用；UI 据此启用/禁用控制。

## 2. 车端（Vehicle-side）

### 2.1 当前实现

- **控制接收**：MQTT 订阅 `vehicle/control`，收到后调用 `handle_control_json()`。
- **ZLM 控制通道**：`ZLM_CONTROL_WS_URL` 已预留；车端连接该 WebSocket 并接收 ZLM 转发的控制消息后，将调用同一 `handle_control_json()`。WebSocket 客户端实现可在后续迭代中接入。

### 2.2 环境变量

- **ZLM_CONTROL_WS_URL**  
  ZLM 控制通道 WebSocket 地址。若设置，车端会尝试连接该 URL 接收经 ZLM 转发的控制指令（当前为占位逻辑，仅打日志）。
- **MQTT_BROKER_URL**  
  MQTT Broker 地址，用于接收控制与发布状态。

## 3. 数据流概览

```
[ 客户端 ]                    [ ZLM / MQTT ]                 [ 车端 ]
    |                                |                            |
    | -- DataChannel (优先) --------> ZLM ---(转发)---> WebSocket -|-> handle_control_json()
    | -- MQTT (备选) --------------> Broker ----------> MqttHandler |-> handle_control_json()
    |                                |                            |
    | <----------------------- MQTT status ----------------------|  (遥测仍经 MQTT)
```

## 4. 验证

- 不设 `CONTROL_CHANNEL_PREFERRED`：先连车端 MQTT，再连视频；控制应优先走 DataChannel（若前端已用四路拉流，使用 front 的 DataChannel）。
- 设 `CONTROL_CHANNEL_PREFERRED=mqtt`：控制仅经 MQTT，与旧行为一致。
