# 客户端单元测试：分层全覆盖策略

## 结论（Executive Summary）

「整个 client 目录每一行在纯单元测试中 100% 覆盖」在工程上**不成立**：QML/UI、OpenGL 场景图、libdatachannel 真 ICE、车端/后端网络与 ZLM 推流依赖外部环境。本仓库采用 **L0–L4 分层**，在 **L1–L2 用 CTest 把可确定逻辑与异常路径压满**，**L3–L4 用 verify-*.sh / 功能脚本 / 人工或录屏** 兜底。

**Qt Test 实践参考**（组织用例、避免在测试中阻塞 GUI）：[Qt Test Best Practices](https://doc.qt.io/qt-6/qttest-best-practices.html)。无头平台说明：[QPA offscreen](https://doc.qt.io/qt-6/qpa.html#offscreen)。

**手段 ↔ 实现对照（覆盖率 / Mull / 属性测试 / fuzz 等）**：见 **`docs/CLIENT_TEST_QUALITY_MATRIX.md`**。

## 分层定义

| 层级 | 范围 | 目标 | 工具 |
|------|------|------|------|
| **L0** | 纯算法/无 Qt | 行覆盖尽量高 | 隔离 `add_client_isolated_module_test` |
| **L1** | Qt Core/Network、无 GUI | 行为 + 异常 + 边界 | CTest + `QT_QPA_PLATFORM=offscreen` |
| **L2** | 服务编排、状态机、与 UI 解耦的 C++ | 分支/异常全覆盖 | 可重写 `virtual` + 桩（如 `test_sessionmanager`） |
| **L3** | Quick/Multimedia/WebRTC 类型链接 | 编译与关键 API 冒烟 | 与主程序同源的可执行体、少量用例 |
| **L4** | QML 绑定、真视频、E2E、**进程启动/退出链** | 场景与观测 | `scripts/verify-*.sh`、compose 栈、手动；**`CLIENT_HEADLESS_SMOKE_MS` + `scripts/verify-client-headless-lifecycle.sh`** 覆盖 `main` 中 `QGuiApplication::exec` → `aboutToQuit` → 日志关闭（`docker-compose.vehicle.dev.yml` 已挂载 `./scripts` → `/workspace/scripts` 供 `client-dev` 调用） |

## SessionManager（L2 目标）

`client/tests/unit/test_sessionmanager.cpp` 对 `sessionmanager.cpp` 的**每个公共槽**做系统化用例：

- 空指针/缺省依赖（无 `VehicleManager` / 无 `Auth` / 无 `WebRtcStreamManager` / 无 FSM）；
- 正常路径（test token 注入车辆、`loadVehicleList` 调用次数）；
- `std::exception` 与 **非标准异常**（`throw int`）触发的 `catch(...)` 分支；
- `VehicleControlService` 凭证 API（`setSessionCredentials` / `clearSessionCredentials`）与登出、清空 VIN 的先后顺序；
- `onSessionCreated`：空 whep、长 URL 日志分支、`VehicleManager` VIN 不一致告警、仅凭证无 WebRTC 等。

未覆盖的 SessionManager 行主要来自：**仅能通过篡改 `Tracing` 或 `QProcessEnvironment` 副作用**才能触发的极端路径；此类归入 **L3/L4** 或后续注入抽象接口。

## 5 Whys：为何「单测仍无法宣称覆盖整个 client」？

| 问 | 答 |
|----|-----|
| 1. 为何不能一条 CTest 证明所有功能？ | 功能分布在 **QML、GPU、ICE/DTLS、Broker、ZLM、车端**，单进程单测无法挂载真实时空环境。 |
| 2. 为何不能只靠扩大 CTest？ | **链接成本**：全量 `WebRtcStreamManager` 拉齐 Multimedia/Quick；**时间成本**：ICE/重连用例分钟级且 flaky。 |
| 3. 为何仍要加 L2/L3 单测？ | **回归确定性**：阈值映射、URL 解析、恢复状态机错误会直接导致 **误降级 / 连错 ZLM / 安全停车风暴**，应在毫秒级用例内锁住。 |
| 4. 为何还要 verify-*.sh 与矩阵？ | **契约与文件存在性**可在无 GPU 下秒级失败；**L4** 像素与多指交互仍须人或专用 GUI 自动化。 |
| 5. 根因？ | 「全覆盖」是 **分层 SLO**：L0–L2 锁纯逻辑，L3 锁可拆边界，L4 锁观测与人工；**不是**单一指标。 |

## MVP → V1 → V2 落地清单（与脚本/CI 对齐）

| 阶段 | 目标 | 本仓库实现 |
|------|------|------------|
| **MVP** | L0–L4 文档策略；新增单测必更新映射表；CI 分路径行覆盖率门禁 | `docs/CLIENT_UNIT_TEST_COVERAGE_TIERS.md`（本文）+ `scripts/run-client-coverage-with-thresholds.sh`（`OVERALL_LINES_MIN` / `CORE_LINES_MIN` / `MEDIA_LINES_MIN` / `SERVICES_LINES_MIN` / `UTILS_LINES_MIN` / `INFRA_LINES_MIN` / `APP_LINES_MIN`）+ **`scripts/verify-client-source-map-sync.sh`** + **`.github/workflows/client-ci.yml`**（push/PR 校验映射表） |
| **V1** | `SafetyMonitorService`、降级迟滞、`MqttController` 侧集成夹具、核心路径 Mull 定期 | `test_safetymonitorservice` + 注入时钟 API；`test_degradationmanager::manager_upgrade_respects_hysteresis_ms`；`test_mqtt_paho_broker_smoke` + **`scripts/run-client-mqtt-integration-fixture.sh`**；**`.github/workflows/client-mutation-weekly.yml`** + `scripts/run-client-mutation-sample.sh` |
| **V2** | 解析 fuzz、QML 录屏回归资产、覆盖率/变异看板 | **`ENABLE_CLIENT_FUZZ`** 目标 `fuzz_rtcp_compound`（Clang）；**`client/qml/regression/README.md`** + **`scripts/verify-qml-recorded-regression.sh`**；**`docs/CLIENT_CI_COVERAGE_DASHBOARD.md`**（Codecov/Artifacts/Sonar 等接入说明） |

## 全模块 backlog（按优先级，滚动更新）

1. **已较强**：`core/*` 多数、`media/*` RTP/jitter、`SessionManager`、`DegradationManager`（含迟滞单测与 `DegradationMapping`）、`SafetyMonitorService`（`test_safetymonitorservice`）、`ErrorRecoveryManager`、`DiagnosticsService`、`WebRtcUrlResolve`。
2. **建议下一批 L2**：`SafetyMonitorService` 与真实 `SystemStateMachine::fire(EMERGENCY_STOP)` / `EventBus` 订阅的联合断言（当前多为 `nullptr` FSM 路径）；`MqttController::sendControlCommand` 在 Paho 真连接下的往返（依赖 broker fixture）。
3. **L3**：`MqttController` 全类链接单测（重依赖 WebRTC 类型）；`WebRtcStreamManager::connectFourStreams` 除 URL 外的分支（**日志/集成脚本** 为主）。
4. **L4**：四路视频、`DrivingInterface` 绑定，见 `docs/CLIENT_UI_FEATURE_COVERAGE_MATRIX.md`；`verify-client-ui-and-video-coverage.sh` **步骤 1/6** 校验 L2 CTest 已在 CMake 注册。

## 覆盖率命令（可选）

在 `client` 构建目录启用 `ENABLE_COVERAGE=ON` 后构建并跑 CTest，再用 `lcov/genhtml`（`client/CMakeLists.txt` 已含 `coverage-report` 目标依赖列表）。

一键（含可选行覆盖率门禁）：**`scripts/run-client-coverage-with-thresholds.sh`**（见脚本内 `CLIENT_COVERAGE_ENFORCE`、`OVERALL_LINES_MIN` 等）。

## 维护约定

- 修改 `sessionmanager.cpp` 任意 `try/catch` 或分支：**同步增减** `test_sessionmanager` 用例。
- 新增 L2 服务：**优先** `virtual` 或接口注入，避免在单测中链接半套 GUI。
