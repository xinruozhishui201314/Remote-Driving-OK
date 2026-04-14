# 远程驾驶客户端架构文档 (Remote Driving Client Architecture)

## 1. 概述 (Overview)

本项目远程驾驶客户端基于 Qt C++ 和 QML 构建，旨在提供低时延、高可靠、可观测的扫地车远程驾驶操作界面。它通过 WebRTC (ZLMediaKit) 获取多路实时视频，通过 MQTT 发送高频控制指令和接收遥测数据。

### 核心架构原则
- **分层解耦**：基础设施层、核心框架层、应用服务层、表现层。
- **实时路径最短化**：视频渲染零拷贝，控制指令直连 MQTT。
- **安全机制多层化**：看门狗、心跳、死手 (Deadman) 等多级防护。
- **自适应降级**：根据网络质量自动调整视频码率、帧率及驾驶权限。
- **可观测性优先**：统一日志前缀、全链路时延埋点、关键状态实时监控。

---

## 2. 系统分层架构 (System Layers)

### 2.1 表现层 (Presentation Layer - QML/C++)
- **QML UI Engine**: 采用声明式 UI，负责交互逻辑。
- **Video Renderer**: 自定义 Scene Graph 节点，实现硬件解码纹理直接上屏（零拷贝）。
- **HUD (Head-Up Display)**: 叠加在视频上的仪表盘、状态栏、告警提示。
- **DrivingFacade (v3)**: QML 模块契约，规范子模块与服务间的通信通道。

### 2.2 应用服务层 (Application Layer - C++)
- **SessionManager**: 管理用户登录、VIN 授权列表、会话生命周期。
- **VehicleControlService**: 100Hz 实时控制环路，处理输入映射、速率限制。
- **SafetyMonitorService**: 监控死手、心跳超时、延迟抖动。
- **DegradationManager**: 实现 6 级平滑降级策略。
- **DiagnosticsService**: 收集节点健康状态与故障码。

### 2.3 核心框架层 (Core Framework - C++)
- **EventBus**: 高性能、线程安全的事件分发中心。
- **SystemStateMachine**: 驱动客户端从 IDLE 到 DRIVING 的全状态转换。
- **PluginManager**: 支持功能扩展（如录制插件、辅助驾驶插件）。
- **ThreadPool**: 管理异步任务，确保主线程不阻塞。

### 2.4 基础设施层 (Infrastructure Layer - C++)
- **TransportManager**: MQTT (控制/遥测) + WebRTC/WHEP (媒体) 统一传输管理。
- **MediaPipeline**: 硬件加速解码管线 (VA-API/D3D11VA/NVDEC)。
- **Hardware/Input**: 对接方向盘、手柄、键盘等 HID 设备。
- **Logging & Telemetry**: 结构化日志输出与 Prometheus 指标上报。

---

## 3. 关键组件详细设计 (Key Components)

### 3.1 视频渲染零拷贝管线
为了满足 < 150ms 的端到端延迟要求，客户端实现了零拷贝渲染路径：
1. **接收**: WebRTC 接收 RTP 包并解封装。
2. **解码**: `IHardwareDecoder` 调用 GPU 硬件解码器。
3. **传递**: 解码后的显存通过 `DMA-BUF` (Linux) 或 `D3D11 Texture` (Windows) 直接导出句柄。
4. **渲染**: QML `VideoRenderer` 通过 `QSGGeometryNode` 绑定该 GPU 纹理进行绘制。
*CPU 仅负责流程调度，不参与像素拷贝。*

### 3.2 100Hz 控制环路
`VehicleControlService` 运行在独立的高优先级线程：
- **采样**: 200Hz 采集 HID 设备输入。
- **预测**: 基于 RTT 的延迟补偿预测（二次多项式拟合）。
- **安全检查**: `SafetyChecker` 校验指令合法性与物理越界。
- **发送**: 经 `MqttController` 发送到 `vehicle/control` 主题。

### 3.3 系统状态机 (FSM)
核心状态包括：
`IDLE` -> `CONNECTING` -> `AUTHENTICATING` -> `READY` -> `PRE_FLIGHT` -> `DRIVING` <-> `DEGRADED` -> `EMERGENCY` -> `STOPPING` -> `IDLE`。
状态转换由 `Trigger` 驱动，并带有 `Guard` 校验和 `Action` 动作。

---

## 4. 通信协议契约 (Communication Contract)

### 4.1 控制面 (MQTT)
- **主题**: `vehicle/control`
- **格式**: JSON (`schemaVersion: 1.2.0`)
- **关键字段**: `type`, `vin`, `sessionId`, `seq`, `timestampMs`, `payload`.
- **签名**: 使用 HMAC-SHA256 对关键指令签名（防重放/防篡改）。

### 4.2 状态面 (MQTT)
- **主题**: `vehicle/status` 或 `vehicle/<VIN>/status`
- **格式**: JSON (`schemaVersion: 1.2.0`)
- **消息类**: `vehicle_status` (底盘遥测), `remote_control_ack`, `offline` (遗嘱).

### 4.3 媒体面 (WebRTC)
- **协议**: WHEP (WebRTC-HTTP Egress Protocol) 风格拉流。
- **流命名**: `{VIN}_cam_front`, `{VIN}_cam_rear`, `{VIN}_cam_left`, `{VIN}_cam_right`。
- **URL 格式**: `http://<ZLM_HOST>:<PORT>/index/api/webrtc?app=teleop&stream=<STREAM_ID>&type=play`.

---

## 5. UI 架构与 DrivingFacade (v3)

客户端 UI 采用 Facade 模式解耦 QML 与 C++ 服务：
- **Facade 根**: `DrivingInterface.qml` 提供统一接口。
- **appServices**: 窄面注入 `mqttController`, `vehicleStatus`, `safetyMonitor` 等服务。
- **teleop**: 别名暴露实时远驾状态。
- **模块化**: UI 拆分为 `TopChrome`, `LeftRail`, `RightRail`, `CenterColumn`, `Dashboard` 五件套。
*禁止子模块直接访问 `AppContext`，必须通过 `facade.appServices` 访问。*

---

## 6. 安全与可靠性 (Safety & Reliability)

### 6.1 四层安全防护
1. **Layer 1 (Vehicle)**: 硬件看门狗、物理急停、速度/转向硬约束。
2. **Layer 2 (Network)**: 链路质量监控 (RTT/Loss)、双链路冗余热备。
3. **Layer 3 (System)**: 软件看门狗 (500ms 超时)、心跳对齐、状态一致性检查。
4. **Layer 4 (Operator)**: 死手 (Deadman) 检测、操作注意力检查。

### 6.2 六级降级策略
- **FULL**: 4K/60fps, 多路全开。
- **HIGH**: 1080p/30fps, 多路全开。
- **MEDIUM**: 720p/30fps, 主视 + 缩略图。
- **LOW**: 480p/15fps, 仅主视。
- **MINIMAL**: 360p/10fps, 仅主视 + 强制限速 5km/h。
- **SAFETY_STOP**: 触发紧急停车。

---

## 7. 工程化与质量保证

### 7.1 目录结构
- `src/core`: 框架基础 (EventBus, FSM, Config).
- `src/infrastructure`: 传输与媒体管线 (MQTT, WebRTC, Decoder).
- `src/services`: 业务逻辑 (Control, Safety, Session).
- `src/presentation`: QML 模型与自定义渲染器.
- `qml/`: 界面文件.
- `tests/`: 单元、集成与性能测试.

### 7.2 自动化门禁
- **`verify-client-ui-quality-chain.sh`**: 运行 `qmllint`, `clang-format`, 契约静态检查。
- **`build-and-verify.sh`**: 编译并运行全量单测与模块验证。
- **`verify-contract-artifacts.sh`**: 校验 OpenAPI 与 JSON Schema 一致性。

---

## 8. 修订历史与核心修复记录
- **v1.3**: 深度重构，引入 DrivingFacade v3 契约。
- **2026-04**: 完成方向盘可视化、高精地图集成、进度条显示优化。
- **2026-02**: 修复黑屏/花屏问题，优化解码器刷新机制。
- **2026-01**: 优化登录流与车辆选择逻辑，引入 Keycloak OIDC。

---
*更多细节请参考代码中各模块头文件注释及 `.cursorrules`。*
