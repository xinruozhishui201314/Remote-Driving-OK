# Client 模块（远驾驾驶舱）

基于 **Qt 6 + QML** 的远程驾驶客户端，C++ 侧为 **C++20**，采用 **核心 / 基础设施 / 应用服务 / 表现层** 分层，并与既有 WebRTC、MQTT、鉴权模块衔接。行为与接口以仓库根目录 [`project_spec.md`](../project_spec.md) 为准。

## Executive Summary

| 维度 | 说明 |
|------|------|
| UI | QML：`main.qml` → 登录/选车 → `DrivingInterface` / `DrivingHUD`、`ControlPanel`、`VideoView` 及 `qml/components/*`、`styles/Theme.qml` |
| 视频 | `presentation/renderers/*` + `shaders/*.vert|frag`；解码走 `infrastructure/media/*`（FFmpeg 软解，可选 VA-API、NVDEC、Linux EGL DMA-BUF） |
| 信令与控车 | `WebRtcClient` / `WebRtcStreamManager`、`MqttController`；服务层 `SessionManager`、`VehicleControlService`、`SafetyMonitorService` 等 |
| 构建 | **CMake 3.21+**；默认在 **`client-dev` Docker 容器**内编译运行（见 [`docs/BUILD_AND_RUN_POLICY.md`](../docs/BUILD_AND_RUN_POLICY.md)） |

## 功能特性

- Qt6 Quick/QML 驾驶舱界面与 HUD 组件
- Keycloak OIDC 登录（`AuthManager`）
- WebRTC（libdatachannel）经 ZLMediaKit **WHEP** 等多路拉流
- MQTT 会话、遥测、`start_stream` 等控制面消息
- 车辆状态与健康检查展示
- 键盘/鼠标输入采样（`KeyboardMouseInput` / `InputSampler`）
- **死手（Deadman）**：`SafetyMonitorService`，远驾无操作超时触发急停（环境变量可配）
- **远程急停**：`VehicleControlService::requestEmergencyStop` + 状态机协同
- **核心层**：`SystemStateMachine`、`EventBus`、`NetworkQualityAggregator`；`AntiReplayGuard`、`CommandSigner`（控车路径加固，细节见架构文档）

## 源码与资源布局

```
client/
├── CMakeLists.txt          # 工程与可选特性探测（FFmpeg / WebRTC / MQTT / Qt Test）
├── conanfile.py            # 可选：Conan 依赖（FFmpeg、Paho 等）；Qt 由系统或镜像提供
├── .clang-format / .clang-tidy
├── src/
│   ├── main.cpp            # 入口：QML 引擎、上下文属性注册、与分层对象 wiring
│   ├── core/               # EventBus、FSM、配置、日志、线程池、防重放、指令签名等
│   ├── infrastructure/     # ITransportManager、MQTT 适配、WebRTC/UDP、媒体管线、GPU 互操作、输入硬件
│   ├── services/           # Session、VehicleControl、Safety、Degradation、Latency、ErrorRecovery、Diagnostics
│   ├── presentation/       # VideoRenderer(GPU) / VideoMaterial / VideoSGNode；Telemetry/Network/Safety QML 模型
│   ├── utils/              # LockFreeQueue、TripleBuffer、CircularBuffer、PercentileStats、TimeUtils
│   ├── authmanager.* …     # 与 QML 直接交互的遗留/门面类（仍由 main 注册到 QML）
│   ├── webrtc*.cpp …       # WebRTC 拉流与管理（内部可由 WebRTCChannel 等使用）
│   ├── mqttcontroller.*    # MQTT 连接与消息
│   └── vehiclestatus.* …   # 遥测与状态模型
├── qml/
│   ├── main.qml
│   ├── LoginPage.qml / LoginDialog.qml
│   ├── VehicleSelectionPage.qml / VehicleSelectionDialog.qml
│   ├── DrivingInterface.qml / DrivingHUD.qml / ControlPanel.qml / VideoView.qml
│   ├── components/         # 车速表、档位、方向盘指示、网络条、安全遮罩等
│   └── styles/Theme.qml
├── shaders/                # 视频渲染顶点/片段着色器
├── tests/                  # test_systemstatemachine.cpp；unit/*.cpp（启用 Qt6::Test 时）
├── docs/                   # 调用链、GATE A、风险评审
├── 客户端架构设计.md        # 中文架构基线（与 docs/*.md 互参）
├── build.sh                # **推荐**：容器内一键 CMake（宿主机执行会拒绝，见构建策略）
├── run.sh                  # 容器内运行（通常与 build.sh 配套）
└── scripts/
    ├── build.sh            # 备选构建脚本（依赖仓库根目录 deps/）
    └── run.sh              # 备选运行脚本
```

## 与架构文档的对应关系

| 文档 | 用途 |
|------|------|
| [`../docs/CLIENT_ARCHITECTURE.md`](../docs/CLIENT_ARCHITECTURE.md) | 统一架构说明（含分层、调用链、协议与 UI 契约） |
| [`../docs/TELEOP_SIGNAL_CONTRACT.md`](../docs/TELEOP_SIGNAL_CONTRACT.md) | 控车、状态与媒体端到端契约 |

## 依赖要求

### 必需

- **CMake 3.21+**
- **C++20** 编译器（GCC / Clang，与 `CMakeLists.txt` 一致）
- **Qt6**：Core, Network, Gui, Quick, Qml（`find_package(Qt6 REQUIRED …)`）

### 常见可选（由 CMake 自动探测）

- **Qt6**：Multimedia, WebSockets, QuickControls2, **ShaderTools**, OpenGL, **Test**（单测）
- **FFmpeg**（libavcodec, libavutil, libswscale, libavformat）— 软件解码
- **VA-API**（Intel / AMD 等，Linux）— 安装 **`libva-dev`**、**`libdrm-dev`**（及运行时的 `libva-drm2` 等），使 CMake 中 **`ENABLE_VAAPI=ON`**；配置结束时日志含 **`VA-API: ON`** 与 **`Client HW decode (CI grep): VA-API=ON`**。
- **EGL + DRM** — DMA-BUF / 零拷贝呈现（与 VA-API 常同机安装 `libdrm-dev`、`libegl1-mesa-dev`）
- **NVDEC**（NVIDIA）— `cmake -DENABLE_NVDEC=ON`，且 FFmpeg 需 **CUDA/NVDEC** 能力；日志含 **`NVDEC: ON`**。
- **libdatachannel** — WebRTC
- **Paho MQTT C++** — MQTT

### 其他

- **OpenGL** — 视频场景图渲染
- 头文件依赖仍可能使用仓库 [`../deps`](../deps) 中的 **cpp-httplib**、**nlohmann/json** 等（以 `CMakeLists.txt` 与容器镜像为准）

## 独立启动（进程级）

**驾驶舱客户端是可独立运行的 Qt 进程**：不强制与 Backend / Keycloak / ZLM / MQTT 同机或同 Compose 栈同时启动。无后端时仍可启动 UI（登录、选车、布局等会进入降级或失败分支，由日志与 `SessionManager` 错误状态体现）。全功能远驾（鉴权、车辆列表、会话、四路 WebRTC）依赖配置中的各服务端点可达。

- 典型离线/排障：`QT_QPA_PLATFORM=offscreen` 跑无头或 `./scripts/run-client-with-software-rendering.sh` 等脚本（见 `docs/RUN_ENVIRONMENT.md`）；进程级「启动—事件循环—退出」门禁：`./scripts/verify-client-headless-lifecycle.sh`（需已编译 `RemoteDrivingClient`）。
- 单测不拉起全栈：`./scripts/run-client-unit-tests-oneclick.sh` 在 `client-dev` 内跑 CTest（含 `SessionManager` 等异常路径用例）。
- 单元测试「全覆盖」分层说明（L0–L4、QML/真 WebRTC 边界）：[`docs/CLIENT_UNIT_TEST_COVERAGE_TIERS.md`](../docs/CLIENT_UNIT_TEST_COVERAGE_TIERS.md)。

## 编译与运行（必读：仓库策略）

本仓库默认要求 **在 Docker `client-dev` 镜像/容器内** 编译与运行客户端，**不要在宿主机**执行 `client/scripts/build.sh` 作为常规工作流。详见 [`docs/BUILD_AND_RUN_POLICY.md`](../docs/BUILD_AND_RUN_POLICY.md)。

推荐（宿主机只发起命令）：

```bash
# 于仓库根目录（需已具备 Makefile 或等价 CI 目标时）
make build-client
make run-client
# 或
make run
```

在 **已进入 client-dev 容器** 的前提下，在挂载的 `client` 目录执行：

```bash
cd /path/to/mounted/client   # 容器内路径以镜像为准，常见为 /workspace/client
./build.sh                   # 推荐：含 Qt 路径探测与宿主机拒绝逻辑
./run.sh
# 备选：./scripts/build.sh、./scripts/run.sh（脚本逻辑略有差异）
```

手动 CMake 示例（同样在容器或你自管依赖的环境中）：

```bash
cd client
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/RemoteDrivingClient
```

可选 CMake 变量：

- **`ENABLE_NVDEC`**：开启 NVIDIA 硬解路径（默认 OFF）
- **`BUILD_BENCHMARKS`**：性能基准（需 Google Benchmark）

可选 Conan：`conan install` + `CMakeToolchain` 生成 toolchain 后再 `cmake`（见 [`conanfile.py`](./conanfile.py)）。

## 配置与环境变量

**配置文件**：优先 `client/config/client_config.yaml`；兼容根目录 `config/client_config.yaml`（若存在符号链接）；容器内常见为 `/app/config/client_config.yaml`。

| 变量 | 说明 | 默认/备注 |
|------|------|-----------|
| `BACKEND_URL` | 业务 Backend 基地址 | 常由配置或登录界面覆盖 |
| `KEYCLOAK_URL` | Keycloak | 与 OIDC 流程一致 |
| `MQTT_BROKER_URL` | MQTT Broker | |
| `ZLM_WHEP_URL` | ZLMediaKit WHEP API | |
| `QT_QPA_PLATFORM` | Qt 平台插件 | Linux 桌面常见 `xcb`；无头 `offscreen`（见 [Qt offscreen](https://doc.qt.io/qt-6/qpa.html#offscreen)） |
| `CLIENT_SKIP_OPENGL_PROBE` | 置 `1` 跳过启动时 OpenGL 能力探测 | 无头/CI/容器无 GPU 时常用 |
| `CLIENT_HEADLESS_SMOKE_MS` | 大于 `0` 时在 N ms 后由 `main` 主动 `quit` | 默认 `0`（关闭）；用于验证 `app.exec`→`aboutToQuit`→日志关闭链，见 `scripts/verify-client-headless-lifecycle.sh` |
| `DEFAULT_SERVER_URL` / `REMOTE_DRIVING_SERVER` | 默认 Backend（QML `defaultServerUrlFromEnv`） | 先于 `BACKEND_URL` 被 main 读取 |
| `CLIENT_AUTO_CONNECT_VIDEO` | 置 `1` 时 QML 侧自动连视频 | 调试/自动化 |
| `CLIENT_AUTO_CONNECT_TEST_VIN` | 自动连流时注入的 VIN（须与 ZLM `{VIN}_cam_*` 一致） | 默认 `123456789`；CARLA 常用 `carla-sim-001` |
| `CLIENT_LAYOUT_DEBUG` | 置 `1` 开启布局调试 | |
| `CLIENT_DEADMAN_ENABLED` | 死手开关 | 默认 `1` |
| `CLIENT_DEADMAN_TIMEOUT_MS` | 死手超时（ms） | 默认 `3000` |
| `CLIENT_MEDIA_HARDWARE_DECODE` | 是否尝试 **GPU 硬解**（映射 `media.hardware_decode`，优先于 JSON） | 默认等价 `true`；设 `0/false/off/no` 强制软解 |
| `CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE` | 硬解失败时是否 **禁止** 退回 CPU 软解（`media.require_hardware_decode`） | 默认等价 `false`；设为 `1` 可在硬解失败时强制报错，用于生产排查 |
| `CLIENT_WEBRTC_HW_DECODE` | （兼容）仅当 **非空** 且为 `0/false/off/no` 时关闭硬解 | 主开关已改为 `media.hardware_decode` / `CLIENT_MEDIA_HARDWARE_DECODE`；硬解未编译时见 **`[Client][VideoHealth][ERROR]`** |
| `CLIENT_VIDEO_EVIDENCE_FULL_CRC_EVERY` | 证据链 **整图 CRC 采样间隔** | 未设置且 `CLIENT_VIDEO_EVIDENCE_CHAIN` 开启时 **默认 60**（前 `EARLY_MAX` 帧每帧算 `fullCrc`）；`0` 关闭默认采样；与 `CLIENT_VIDEO_FORENSICS` / `CLIENT_VIDEO_EVIDENCE_FULL_CRC` 全开互斥（全开时每条证据行都算 CRC） |
| （启动日志） | `[Client][VideoHealth][Contract]` | **一行** `verdict=OK|ERROR`、`fullCrc_mode=`、`hwDecode_env/comp`、`dmaSg` 等；`ERROR` 时先修契约再查花屏 |
| ~~`CLIENT_LEGACY_CONTROL_ONLY`~~ | （已移除）UI 控车唯一入口：`AppContext.sendUiCommand` → `VehicleControlService` | 见 `scripts/verify-client-contract.sh` |
| `CLIENT_ENABLE_CONTROL_TICKER` | 控制节拍占位 | 默认 `0` |

## Docker 生产镜像

```bash
cd client
docker build -f Dockerfile.prod -t teleop-client:latest .
```

运行示例（桌面转发）：

```bash
docker run -it \
  -e DISPLAY="$DISPLAY" \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -e BACKEND_URL=http://backend:8000 \
  -e MQTT_BROKER_URL=mqtt://mosquitto:1883 \
  teleop-client:latest
```

## 使用说明（操作员视角）

1. 启动客户端，确认 Backend / Keycloak 地址（或使用环境变量默认值）。
2. OIDC 登录成功后加载车辆列表（VIN 权限由后端与 Token 决定）。
3. 选择车辆，建立会话并连接 MQTT；在驾驶界面点击「连接车端」等操作触发推流后，通过 WHEP 拉流。
4. 使用键盘或已映射输入控车；异常时使用急停；死手超时将自动急停（若启用）。

控制映射以当前 QML / 车端协议为准，常见为方向键或 WASD 等（见 `DrivingInterface` / `ControlPanel`）。

## 故障排查

- **编译失败**：在容器内确认 `cmake --version`、`Qt6` 路径、`pkg-config --modversion libavcodec`（若启用 FFmpeg）、libdatachannel 与 Paho 是否安装。
- **黑屏 / 无法显示**：检查 `DISPLAY`、`xhost`、[`docs/RUN_ENVIRONMENT.md`](../docs/RUN_ENVIRONMENT.md)。
- **无视频**：确认 ZLM 与 WHEP URL、车端是否已收到 `start_stream`、WebRTC 日志。
- **MQTT 失败**：Broker 地址、端口、证书与 ACL。

详细日志可设置 `QT_LOGGING_RULES`；客户端关键模块日志前缀见 [`docs/CALLCHAIN_AND_ARCHITECTURE.md`](docs/CALLCHAIN_AND_ARCHITECTURE.md)。

## 测试与验证

```bash
# 架构文件与关键符号合规（仓库根目录）
./scripts/verify-client-architecture.sh
```

启用 **Qt6::Test** 时，`cmake` 会生成 `client_systemstatemachine_test` 与 `tests/unit` 聚合目标；在容器内 `ctest` 或执行对应可执行文件。

全仓变更后建议：`./scripts/build-and-verify.sh`（以仓库脚本为准）。

## 安全与合规（摘要）

- 鉴权：JWT / OIDC；VIN 范围由后端授权。
- 客户端已实现 **防重放与指令签名相关核心类型**（`AntiReplayGuard`、`CommandSigner`），具体启用范围以代码与 MQTT/后端契约为准，不以本文档替代代码审查。
- 生产环境应对外使用 TLS，并对密钥与配置做最小权限管理。

## 相关文档

- [`../project_spec.md`](../project_spec.md)
- [`../docs/DISTRIBUTED_DEPLOYMENT.md`](../docs/DISTRIBUTED_DEPLOYMENT.md)
- [`../docs/CONFIGURATION_GUIDE.md`](../docs/CONFIGURATION_GUIDE.md)
- [`../docs/REFACTORING_GUIDE.md`](../docs/REFACTORING_GUIDE.md)

## 许可证

Copyright © 2026 Remote-Driving Team
