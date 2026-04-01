# 实现说明文档

## Executive Summary

已创建完整的远程驾驶客户端项目，包含：
- ✅ QML UI 界面（基于远程驾驶客户端设计）
- ✅ WebRTC 视频流接收模块（C++）
- ✅ MQTT 车辆控制指令模块（C++）
- ✅ 车辆状态管理
- ✅ 完整的 CMake 构建系统

**注意**: WebRTC 和 MQTT 的完整实现需要集成第三方库（libdatachannel/GStreamer 和 Qt MQTT/Paho MQTT C++）。

---

## 1. 项目结构

```
client/
├── CMakeLists.txt              # CMake 构建配置
├── README.md                   # 项目说明
├── BUILD.md                    # 编译说明
├── IMPLEMENTATION.md           # 本文档
├── .gitignore                 # Git 忽略文件
│
├── src/                       # C++ 源代码
│   ├── main.cpp               # 程序入口
│   ├── webrtcclient.h/cpp     # WebRTC 客户端（接口已定义）
│   ├── mqttcontroller.h/cpp    # MQTT 控制器（接口已定义）
│   └── vehiclestatus.h/cpp     # 车辆状态管理
│
├── qml/                       # QML UI 文件
│   ├── main.qml              # 主界面
│   ├── VideoView.qml         # 视频显示组件
│   ├── ControlPanel.qml      # 控制面板（方向盘、油门、刹车、档位）
│   ├── StatusBar.qml         # 状态栏
│   ├── ConnectionsDialog.qml # 连接配置对话框
│   └── qmldir                # QML 模块定义
│
└── resources/                 # 资源文件
    └── icons/                # 图标（占位符）
```

---

## 2. 核心模块说明

### 2.1 WebRTC 客户端 (`webrtcclient.h/cpp`)

**功能**:
- 连接到 ZLMediaKit WebRTC API
- 处理 SDP Offer/Answer 交换
- 管理视频流连接状态

**当前实现**:
- ✅ HTTP API 调用接口
- ✅ SDP 交换逻辑框架
- ⚠️ 需要集成 WebRTC 库（libdatachannel 或 GStreamer）进行完整实现

**当前状态**:
- 已使用最小 play Offer（m=audio+m=video recvonly）通过 ZLM 校验并收到 SDP Answer。
- 无 libdatachannel 时：仅信令成功，**不**标记「已连接」，界面显示「信令成功，需 WebRTC 库以接收视频」。
- 有 libdatachannel + FFmpeg 时：onTrack 收到视频轨后，通过 `setupVideoDecoder()` 创建 H264Decoder、绑定 `track.onMessage`，RTP → H.264 解包（含 FU-A）→ FFmpeg 解码 → 发出 `videoFrameReady(QImage)`，QML 中 VideoRenderer 接收并绘制，**四路画面可显示**。

**视频管线（已实现）**:
1. `client/Dockerfile.client-dev`：镜像内安装 libdatachannel（源码构建）+ FFmpeg 开发库。
2. CMake：`pkg_check_modules(FFMPEG)`，条件加入 `h264decoder.cpp/h`、`videorenderer.cpp/h`，链接 avcodec/avutil/swscale。
3. `H264Decoder`：`feedRtp()` 剥 12 字节 RTP 头，处理单 NAL 与 FU-A，`decodeAndEmit()` 用 avcodec 解码，sws_scale 转 RGB32 后 `emit frameReady(QImage)`。
4. `VideoRenderer`（QQuickPaintedItem）：`setFrame(QImage)` 线程安全，`paint()` 中绘制当前帧；已注册为 QML 类型 `RemoteDriving 1.0 VideoRenderer`。
5. `WebRtcClient::setupVideoDecoder()`：主线程中创建 H264Decoder，连接 `frameReady` → `videoFrameReady`（QueuedConnection），`m_videoTrack->onMessage` 将收到的 binary 送入 `feedRtp()`。
6. QML：`VideoPanel` 与中央「前方摄像头」内嵌 VideoRenderer，`Connections { target: streamClient; onVideoFrameReady: videoRenderer.setFrame(frame) }`，四路分别绑定 frontClient/leftClient/rearClient/rightClient。

**待完成**:
1. ~~集成 WebRTC 库生成真实的 Offer SDP~~（已用最小 Offer）
2. ~~接收并渲染视频帧：RTP→H.264 解包→FFmpeg 解码→VideoRenderer~~（已实现）
3. 可选：DataChannel 双向通信

**API 使用**:
```cpp
// 连接到视频流
webrtcClient->connectToStream("http://192.168.1.100:8080", "live", "test");

// 断开连接
webrtcClient->disconnect();
```

### 2.2 MQTT 控制器 (`mqttcontroller.h/cpp`)

**功能**:
- 连接到 MQTT Broker
- 发送车辆控制指令
- 订阅车辆状态信息

**当前实现**:
- ✅ 控制指令接口定义
- ✅ JSON 消息格式
- ⚠️ 需要集成 MQTT 客户端库（Qt MQTT 或 Paho MQTT C++）

**待完成**:
1. 集成 MQTT 客户端库
2. 实现连接管理
3. 实现消息发布和订阅
4. 处理连接状态和错误

**控制指令格式**:
```json
{
  "type": "drive",
  "steering": 0.0,    // -1.0 到 1.0
  "throttle": 0.0,    // 0.0 到 1.0
  "brake": 0.0,       // 0.0 到 1.0
  "gear": 1,          // -1, 0, 1
  "timestamp": 1234567890
}
```

**API 使用**:
```cpp
// 连接 MQTT
mqttController->setBrokerUrl("mqtt://192.168.1.100:1883");
mqttController->connectToBroker();

// 发送控制指令
mqttController->sendDriveCommand(0.5, 0.3, 0.0, 1);  // 转向50%, 油门30%, 前进档
mqttController->sendSteeringCommand(0.8);  // 单独控制方向盘
mqttController->sendThrottleCommand(0.5);   // 单独控制油门
mqttController->sendBrakeCommand(1.0);      // 紧急刹车
```

### 2.3 车辆状态 (`vehiclestatus.h/cpp`)

**功能**:
- 管理车辆状态信息（速度、电池、连接状态）
- 提供 QML 属性绑定

**已实现**:
- ✅ 速度、电池电量管理
- ✅ 视频/MQTT 连接状态
- ✅ QML 属性绑定

### 2.4 QML UI 组件

#### `main.qml` - 主界面
- 布局：左侧视频 + 右侧控制面板
- 顶部状态栏
- 菜单栏（连接、设置、帮助）

#### `VideoView.qml` - 视频显示
- 视频渲染区域（当前为占位符）
- 连接状态显示
- 全屏按钮

#### `ControlPanel.qml` - 控制面板
- 方向盘滑动条（-100% 到 +100%）
- 油门滑动条（0% 到 100%）
- 刹车滑动条（0% 到 100%）
- 档位按钮（R/N/D）
- 紧急停止按钮

#### `StatusBar.qml` - 状态栏
- 连接状态指示器
- 视频/MQTT 状态
- 车辆速度、电池电量
- 当前时间

#### `ConnectionsDialog.qml` - 连接对话框
- WebRTC 服务器配置
- MQTT Broker 配置
- 连接/取消按钮

---

## 3. 集成第三方库指南

### 3.1 WebRTC 集成

#### 选项 A: libdatachannel

```cmake
# 在 CMakeLists.txt 中添加
find_package(libdatachannel REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE datachannel)
```

```cpp
// 在 webrtcclient.cpp 中
#include <rtc/rtc.hpp>

// 创建 PeerConnection
auto pc = std::make_shared<rtc::PeerConnection>();

// 创建 DataChannel
auto dc = pc->createDataChannel("control");

// 处理视频轨道
pc->onTrack([this](rtc::Track track) {
    if (track.kind() == "video") {
        // 处理视频帧
    }
});
```

#### 选项 B: GStreamer WebRTC

```cmake
# 在 CMakeLists.txt 中添加
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-webrtc-1.0)
target_link_libraries(${PROJECT_NAME} PRIVATE ${GSTREAMER_LIBRARIES})
```

```cpp
// 使用 GStreamer WebRTC pipeline
GstElement *pipeline = gst_parse_launch(
    "webrtcbin name=webrtcbin "
    "videotestsrc ! videoconvert ! vp8enc ! rtpvp8pay ! "
    "application/x-rtp,media=video,encoding-name=VP8,payload=96 ! webrtcbin. "
    "audiotestsrc ! audioconvert ! opusenc ! rtpopuspay ! "
    "application/x-rtp,media=audio,encoding-name=OPUS,payload=111 ! webrtcbin.",
    NULL);
```

### 3.2 MQTT 集成

#### 选项 A: Qt MQTT（如果 Qt 版本支持）

```cmake
# 在 CMakeLists.txt 中添加
find_package(Qt6 REQUIRED COMPONENTS Mqtt)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Mqtt)
```

```cpp
// 在 mqttcontroller.cpp 中
#include <QMqttClient>

QMqttClient *m_client = new QMqttClient(this);
m_client->setHostname("192.168.1.100");
m_client->setPort(1883);
m_client->connectToHost();
```

#### 选项 B: Paho MQTT C++

```cmake
# 在 CMakeLists.txt 中添加
find_package(PahoMqttCpp REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE paho-mqttpp3)
```

```cpp
// 在 mqttcontroller.cpp 中
#include <mqtt/async_client.h>

mqtt::async_client client("tcp://192.168.1.100:1883", "client_id");
client.connect()->wait();
```

---

## 4. 数据流图

```
┌─────────────┐
│   ZLMediaKit │
│   (Media)    │
└──────┬───────┘
       │ WebRTC
       │ (Video Stream)
       ▼
┌─────────────┐
│ WebRtcClient│
│   (C++)     │
└──────┬───────┘
       │ Video Frames
       ▼
┌─────────────┐
│  VideoView  │
│    (QML)    │
└─────────────┘

┌─────────────┐
│ ControlPanel│
│    (QML)    │
└──────┬───────┘
       │ User Input
       ▼
┌─────────────┐
│MqttController│
│    (C++)    │
└──────┬───────┘
       │ MQTT
       │ (Control Commands)
       ▼
┌─────────────┐
│  MQTT       │
│  Broker     │
└──────┬───────┘
       │
       ▼
┌─────────────┐
│ Vehicle-side│
│  (ROS2)     │
└─────────────┘
```

---

## 5. 配置示例

### 5.1 WebRTC 配置

```cpp
// 连接到 ZLMediaKit
webrtcClient->connectToStream(
    "http://192.168.1.100:8080",  // 服务器地址
    "live",                         // 应用名称
    "vehicle_stream"                // 流名称
);
```

### 5.2 MQTT 配置

```cpp
// 配置 MQTT
mqttController->setBrokerUrl("mqtt://192.168.1.100:1883");
mqttController->setClientId("remote_driving_client_001");
mqttController->setControlTopic("vehicle/control");
mqttController->setStatusTopic("vehicle/status");
mqttController->connectToBroker();
```

---

## 6. 待完成任务清单

### 高优先级

- [ ] 集成 WebRTC 库（libdatachannel 或 GStreamer）
- [ ] 集成 MQTT 客户端库（Qt MQTT 或 Paho MQTT C++）
- [ ] 实现视频帧渲染（QVideoSink）
- [ ] 实现 MQTT 消息发布和订阅

### 中优先级

- [ ] 添加键盘/手柄输入支持
- [ ] 实现配置文件保存/加载
- [ ] 添加日志记录系统
- [ ] 实现连接重试机制

### 低优先级

- [ ] 添加视频录制功能
- [ ] 实现多路视频流支持
- [ ] 添加车辆状态历史记录
- [ ] 实现全屏模式优化

---

## 7. 测试建议

### 单元测试

```cpp
// 测试车辆状态
VehicleStatus status;
status.setSpeed(50.0);
QCOMPARE(status.speed(), 50.0);

// 测试控制指令格式
QJsonObject cmd = createDriveCommand(0.5, 0.3, 0.0, 1);
QVERIFY(cmd.contains("steering"));
```

### 集成测试

1. 启动 ZLMediaKit 服务器
2. 启动 MQTT Broker
3. 运行客户端
4. 测试连接和基本控制

### 端到端测试

1. 完整系统测试（客户端 + 媒体服务器 + 车端）
2. 延迟测试（视频延迟、控制延迟）
3. 稳定性测试（长时间运行）
4. 错误恢复测试（网络中断、服务器重启）

---

## 8. 性能考虑

### 视频流

- **延迟目标**: < 200ms（端到端）
- **帧率**: 30fps
- **分辨率**: 1920x1080（可配置）

### 控制指令

- **发送频率**: 20-50Hz（根据控制精度需求）
- **消息大小**: < 1KB
- **延迟要求**: < 100ms

### 资源使用

- **CPU**: < 30%（单核）
- **内存**: < 500MB
- **网络带宽**: 视频流 + 控制指令

---

## 9. 安全考虑

### 通信安全

- [ ] MQTT over TLS/SSL
- [ ] WebRTC over DTLS
- [ ] 身份认证和授权

### 数据安全

- [ ] 控制指令签名验证
- [ ] 防止重放攻击
- [ ] 敏感数据加密

---

## 10. 参考资源

- [ZLMediaKit WebRTC API](https://github.com/ZLMediaKit/ZLMediaKit/wiki/WebRTC)
- [Qt6 QML Documentation](https://doc.qt.io/qt-6/qtqml-index.html)
- [libdatachannel](https://github.com/paullouisageneau/libdatachannel)
- [Paho MQTT C++](https://github.com/eclipse/paho.mqtt.cpp)
