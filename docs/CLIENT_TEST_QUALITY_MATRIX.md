# 客户端测试质量手段 ↔ 本仓库实现对照

与 Google/Meta 等工程文化一致：**覆盖率是必要非充分条件**；更可靠的是 **分层 SLO + 变异测试 + 关键属性/不变式**。下表标明每项在本仓库的**具备程度**与入口。

| 手段 | 作用 | 本仓库状态 | 入口 / 说明 |
|------|------|------------|-------------|
| **分层 SLO（L0–L4）** | 避免用错误层级解决错误问题 | **已具备** | `docs/CLIENT_UNIT_TEST_COVERAGE_TIERS.md` |
| **行覆盖率 + 分路径门禁** | 防止整体裸奔 | **已具备** | `scripts/run-client-coverage-with-thresholds.sh`：`OVERALL_LINES_MIN`、`CORE_LINES_MIN`、`MEDIA_LINES_MIN`、`SERVICES_LINES_MIN`、`UTILS_LINES_MIN`、`INFRA_LINES_MIN`、`APP_LINES_MIN` |
| **分支覆盖率门禁** | 补「行到但未判分支」盲区 | **已具备**（可选） | 同上：`OVERALL_BRANCHES_MIN`、`CORE_BRANCHES_MIN`、`MEDIA_BRANCHES_MIN`、`SERVICES_BRANCHES_MIN`（依赖 lcov `--summary` 中 `branches` 行） |
| **变异测试（Mull）** | 检验测试是否真在查错 | **部分具备** | `scripts/run-client-mutation-sample.sh` + `docs/MULL_CLIENT.md`；需本机 Mull 工具链与 `MULL_BUILD_DIR`；**`.github/workflows/client-mutation-weekly.yml`** 作定期提醒 |
| **可测试性（接缝 / 注入）** | IO/时钟/网络可替换 | **部分具备** | `SafetyMonitorService` / `DegradationManager`：`setUnitTestNowMsForTesting`；Auth 等见 `test_sessionmanager`；其余模块按 backlog 逐步注入 |
| **契约测试 / 假 Broker / 假 HTTP** | 不连生产验证编排 | **部分具备** | **HTTP**：`test_httpendpointserver`（本机 ephemeral 端口，属契约烟测）；**MQTT**：`test_mqtt_paho_broker_smoke` + `scripts/run-client-mqtt-integration-fixture.sh`（Docker Mosquitto，非进程内桩）；**WireMock 级 HTTP 桩**：未引入第三方，需要时可加独立 mock server 脚本 |
| **属性测试（QuickCheck 风格）** | 随机输入 + 不变式 | **部分具备（轻量）** | `test_rtcpcompoundparser::property_random_inputs_no_crash_bounded`、`test_webrtcurlresolve::property_random_strings_no_crash`；未引入 RapidCheck，复杂属性可后续加依赖 |
| **QML / GUI** | 绑定与视觉回归 | **部分具备** | `docs/CLIENT_UI_FEATURE_COVERAGE_MATRIX.md`、`scripts/verify-client-ui-and-video-coverage.sh`、`scripts/verify-qml-recorded-regression.sh`、`client/qml/regression/`；**Squish / QQuickTest 专用套件**：未默认引入 |
| **模糊测试（fuzz）** | 二进制协议边界 | **部分具备** | **RTCP**：`ENABLE_CLIENT_FUZZ` → `fuzz_rtcp_compound`（Clang libFuzzer）；**H264/FFmpeg 全路径**：未做进程内 fuzz（易与解码器内部断言交互）；建议子进程或专用 harness，见下文 |

## H264 / NAL 与解码器 fuzz 说明

`H264Decoder::feedRtp` 走 **FFmpeg libavcodec**，对任意字节可能触发 native 断言或耗时路径，**不适合**与 RTCP 同级的 in-process libFuzzer 默认可选目标。推荐演进顺序：

1. 若有独立 **NAL 解析/拆包** 纯函数，为其单独建 `fuzz_h264_nal_*` 并与解码线程隔离。  
2. 或对解码器 fuzz 使用 **子进程 + 超时**（AFL++/honggfuzz 常见模式）。  

当前仓库将 **RTCP compound** 作为默认 fuzz 入口，与「纯解析、无解码状态机」原则一致。

## 与看板 / CI 的关系

覆盖率与变异结果上屏：`docs/CLIENT_CI_COVERAGE_DASHBOARD.md`。  
GitHub Actions：**映射表必过**见 `.github/workflows/client-ci.yml`；完整 Qt 链下覆盖率门禁建议在 **client-dev** 或自托管 runner 执行。
