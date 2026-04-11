# 客户端单元测试：被测源文件 ↔ 测试用例映射（Codecov / 审计）

本文档与 `client/CMakeLists.txt` 中 `ENABLE_QT6_Test` 块保持一致；用于覆盖率组件划分、变更影响审计与「该改动了谁该跑哪条用例」的快速检索。

**全覆盖边界与分层（QML / 真 WebRTC / E2E）**：见 **`docs/CLIENT_UNIT_TEST_COVERAGE_TIERS.md`**。

**UI 与视频显示**：QML 功能项、四路视频与 L1–L4 覆盖分层见 **`docs/CLIENT_UI_FEATURE_COVERAGE_MATRIX.md`**；一键 L1 串联脚本 **`scripts/verify-client-ui-and-video-coverage.sh`**。C++ 单测覆盖媒体管线逻辑，**不**等价于屏上视频回归。

## 一键执行

```bash
# 仓库根目录：配置 + 编译 + 跑完全部 CTest（等价于 build 目录下 run-unit-tests）
./scripts/run-client-unit-tests-oneclick.sh
```

环境变量与选项见脚本内注释。Docker / compose 路径仍可使用 `./scripts/run-all-client-unit-tests.sh`。

**MVP→V2 演进脚本（覆盖率门禁 / 变异入口 / 串联）**：

- `scripts/run-client-coverage-with-thresholds.sh` — `ENABLE_COVERAGE` 构建 + CTest + lcov/genhtml，可选 `CLIENT_COVERAGE_ENFORCE=1` 与 `OVERALL_LINES_MIN` / `CORE_LINES_MIN` 等。
- `scripts/run-client-mutation-sample.sh` — 已安装 [Mull](https://github.com/mull-project/mull) 时调用 `mull-runner`（否则跳过）。
- `scripts/run-client-test-evolution-all.sh` — 先跑一键单测，再按需 `RUN_COVERAGE=1` / `RUN_MUTATION=1` / `RUN_MQTT_FIXTURE=1`。
- `scripts/verify-client-source-map-sync.sh` — **MVP**：CMake 注册名与本文档 CTest 表双向一致（建议 CI 首 job）。
- `scripts/run-client-mqtt-integration-fixture.sh` — **V1**：拉起临时 Mosquitto 后跑 `test_mqtt_paho_broker_smoke`（需已构建 Paho）。

## CTest 名称索引（共 33 项；`test_h264decoder` 仅在 `ENABLE_FFMPEG=ON` 时参与链接；`test_mqtt_paho_broker_smoke` 仅在 `ENABLE_MQTT_PAHO=ON` 时构建进 CTest）

| CTest / 可执行文件名　　　　　　 | 测试源文件　　　　　　　　　　　　　　　　　　　　　　| Qt 测试类　　　　　　　　　　　|
| ----------------------------------| -------------------------------------------------------| --------------------------------|
| `client_systemstatemachine_test` | `client/tests/test_systemstatemachine.cpp`　　　　　　| `TestSystemStateMachine`　　　 |
| `test_configuration`　　　　　　 | `client/tests/unit/test_configuration.cpp`　　　　　　| `TestConfiguration`　　　　　　|
| `test_threadpool`　　　　　　　　| `client/tests/unit/test_threadpool.cpp`　　　　　　　 | `TestThreadPool`　　　　　　　 |
| `test_networkquality`　　　　　　| `client/tests/unit/test_networkqualityaggregator.cpp` | `TestNetworkQualityAggregator` |
| `test_logger`　　　　　　　　　　| `client/tests/unit/test_logger.cpp`　　　　　　　　　 | `TestLogger`　　　　　　　　　 |
| `test_vehiclecontrolservice`　　 | `client/tests/unit/test_vehiclecontrolservice.cpp`　　| `TestVehicleControlService`　　|
| `test_sessionmanager`　　　　　　| `client/tests/unit/test_sessionmanager.cpp`　　　　　　| `TestSessionManager`　　　　　　|
| `test_antireplayguard`　　　　　 | `client/tests/unit/test_antireplayguard.cpp`　　　　　| `TestAntiReplayGuard`　　　　　|
| `test_commandsigner`　　　　　　 | `client/tests/unit/test_commandsigner.cpp`　　　　　　| `TestCommandSigner`　　　　　　|
| `test_vehicle_api_parsers`　　　 | `client/tests/unit/test_vehicle_api_parsers.cpp`　　　| `VehicleApiParsersTest`　　　　|
| `test_errorregistry`　　　　　　 | `client/tests/unit/test_errorregistry.cpp`　　　　　　| `TestErrorRegistry`　　　　　　|
| `test_clientmediabudget`　　　　 | `client/tests/unit/test_clientmediabudget.cpp`　　　　| `TestClientMediaBudget`　　　　|
| `test_rtcpcompoundparser`　　　　| `client/tests/unit/test_rtcpcompoundparser.cpp`　　　 | `TestRtcpCompoundParser`　　　 |
| `test_latencycompensator`　　　　| `client/tests/unit/test_latencycompensator.cpp`　　　 | `TestLatencyCompensator`　　　 |
| `test_rtppacketspscqueue`　　　　| `client/tests/unit/test_rtppacketspscqueue.cpp`　　　 | `TestRtpPacketSpscQueue`　　　 |
| `test_rtptruejitterbuffer`　　　 | `client/tests/unit/test_rtptruejitterbuffer.cpp`　　　| `TestRtpTrueJitterBuffer`　　　|
| `test_webrtcurlresolve`　　　　　| `client/tests/unit/test_webrtcurlresolve.cpp`　　　　　| `TestWebRtcUrlResolve`　　　　　|
| `test_mqttcontrolenvelope`　　　 | `client/tests/unit/test_mqttcontrolenvelope.cpp`　　　　| `TestMqttControlEnvelope`　　　　|
| `test_webrtctransportdispatch`　 | `client/tests/unit/test_webrtctransportdispatch.cpp`　| `TestWebRtcTransportDispatch`　　|
| `test_degradationmanager`　　　　| `client/tests/unit/test_degradationmanager.cpp`　　　 | `TestDegradationManager`　　　　|
| `test_safetymonitorservice`　　　| `client/tests/unit/test_safetymonitorservice.cpp`　　　| `TestSafetyMonitorService`　　　|
| `test_errorrecoverymanager`　　　| `client/tests/unit/test_errorrecoverymanager.cpp`　　　| `TestErrorRecoveryManager`　　　|
| `test_diagnosticsservice`　　　　| `client/tests/unit/test_diagnosticsservice.cpp`　　　 | `TestDiagnosticsService`　　　　|
| `test_httpendpointserver`　　　　| `client/tests/unit/test_httpendpointserver.cpp`　　　 | `TestHttpEndpointServer`　　　 |
| `test_ratelimiter`　　　　　　　 | `client/tests/unit/test_ratelimiter.cpp`　　　　　　　| `TestRateLimiter`　　　　　　　|
| `test_window_frame_policy`　　　 | `client/tests/unit/test_window_frame_policy.cpp`　　　| `TestWindowFramePolicy`　　　　|
| `test_startup_readiness_profile` | `client/tests/unit/test_startup_readiness_profile.cpp` | `TestStartupReadinessProfile` |
| `test_present_health_auto_env`　 | `client/tests/unit/test_present_health_auto_env.cpp`　| `TestPresentHealthAutoEnv`　　　|
| `test_videoframefingerprintcache` | `client/tests/unit/test_videoframefingerprintcache.cpp` | `TestVideoFrameFingerprintCache` |
| `test_clientvideodiagcache`　　　| `client/tests/unit/test_clientvideodiagcache.cpp`　　　| `TestClientVideoDiagCache`　　　|
| `test_h264clientdiag`　　　　　　| `client/tests/unit/test_h264clientdiag.cpp`　　　　　　| `TestH264ClientDiag`　　　　　　|
| `test_h264decoder`　　　　　　　 | `client/tests/unit/test_h264decoder.cpp`　　　　　　　　| `TestH264Decoder`（依赖 FFmpeg） |
| `test_video_interlaced_policy`　 | `client/tests/unit/test_video_interlaced_policy.cpp` | `TestVideoInterlacedPolicy`　　 |
| `test_mqtt_paho_broker_smoke`　　| `client/tests/integration/test_mqtt_paho_broker_smoke.cpp` | `TestMqttPahoBrokerSmoke`（L3；无 `MQTT_TEST_BROKER` 时 QSKIP） |

运行单个用例（示例）：

```bash
cd "$CLIENT_BUILD_DIR"
./test_commandsigner  # 跑该可执行文件内全部 slots
ctest -R test_commandsigner --output-on-failure
ctest -R test_commandsigner -V  # 详细
```

## 被测生产源码 → 测试映射（主覆盖）

「主要」表示该 `.cpp` 的语义由对应测试直接断言；「编译入二进制」表示为链接/单例/桩所需，覆盖率可能计入但非该用例设计目标。

| 被测生产源码（`client/src/`） | 覆盖类型 | CTest 名称 | QTest slots（`Class::slot` 形式，便于 grep） |
|------------------------------|----------|------------|-----------------------------------------------|
| `core/systemstatemachine.cpp` | 主要 | `client_systemstatemachine_test` | `TestSystemStateMachine::transitions_idleToReadyToDriving`, `invalidTransition_emergencyFromIdle` |
| `core/eventbus.cpp` | 编译入二进制 | 同上 | （随状态机用例间接执行） |
| `core/metricscollector.cpp` | 编译入二进制 / 部分间接 | `client_systemstatemachine_test`, `test_httpendpointserver`, `test_ratelimiter` | 见各测试内 HTTP/metrics 与 RateLimiter 依赖 |
| `core/configuration.cpp` | 编译入二进制 | 同上及 `test_httpendpointserver`, `test_ratelimiter` | 配置单测见下行 |
| `core/configuration.cpp` | 主要 | `test_configuration` | `test_singletonAccess`, `test_loadFromFile`, `test_reload`, `test_getVariant`, `test_getString`, `test_getInt`, `test_getDouble`, `test_getBool`, `test_set`, `test_setOverride`, `test_convenienceMethods`, `test_environmentVariable`, `test_validateRequired` |
| `core/threadpool.cpp` | 主要 | `test_threadpool` | `test_singletonAccess`, `test_constructor`, `test_submitSimpleTask`, `test_submitMultipleTasks`, `test_submitWorkerPriority`, `test_submitHighPriority`, `test_submitTimeCriticalPriority`, `test_waitForDone`, `test_poolAccess` |
| `core/networkqualityaggregator.cpp` | 主要 | `test_networkquality` | `test_constructor`, `test_initialScore`, `test_scoreProperty`, `test_degradedProperty`, `test_scoreChangedSignal`, `test_degradedChangedSignal`, `test_recomputeOnStatusChange`, `test_mediaPresentationPenaltyAndRecovery`, `test_mediaHealth_weighted_auxiliaryPenaltyMilder`, `test_mediaHealth_min_isPessimistic`, `test_vehicleStatusNetworkMetrics_exposedOnAggregator` |
| `core/logger.cpp` | 主要 | `test_logger` | `test_singletonAccess`, `test_initialization`, `test_logInfo`, `test_logWarn`, `test_logError`, `test_setMaxQueueSize`, `test_droppedCount`, `test_rotationConfig`, `test_qtMessageHandler` |
| `core/antireplayguard.cpp` | 主要 | `test_antireplayguard` | 含 `large_forward_gap_advances_without_duplicate`、`isOverflowing_true_when_highest_seq_past_three_quarters_threshold`（大跨度窗口 / 溢出风险探测）；其余见既有 slots |
| `core/commandsigner.cpp` | 主要 | `test_commandsigner` | 含 `verify_missing_hmac_fails`、`verify_not_ready_fails`、`verify_malformed_hex_hmac_fails`、`verify_wrong_hmac_byte_length_fails`；其余见既有 slots |
| `core/errorregistry.cpp` | 主要 | `test_errorregistry` | `singleton_same_instance`, `report_and_getErrors`, `duplicate_aggregates_occurrence`, `clearErrors_unknown_clears_all`, `clearErrors_single_category`, `getErrorSummary_is_json`, `category_string_roundtrip`, `level_string_roundtrip`, `stats_counters` |
| `core/httpendpointserver.cpp` | 主要 | `test_httpendpointserver` | `start_stop_ephemeral_port`, `get_health_ok_json`, `get_ready_reflects_setReadyStatus`, `get_metrics_includes_http_counter`, `get_metrics_json_merges_collector`, `custom_handler_and_unregister`, `custom_json_handler`, `not_found`, `bad_request_line`, `setHealthStatus_degraded`, `requestReceived_signal` |
| `infrastructure/network/ratelimiter.cpp` | 主要 | `test_ratelimiter` | `tryAcquire_respects_burst`, `tryAcquire_emits_exceeded_and_rejectedCount`, `refill_restores_tokens_and_released_signal`, `acquire_zero_same_as_tryAcquire`, `acquire_waits_until_refill`, `acquire_times_out`, `setRate_rejects_non_positive`, `setBurst_rejects_non_positive`, `setBurst_caps_tokens`, `resetStats_clears_rejected` |
| `media/ClientMediaBudget.cpp` | 主要 | `test_clientmediabudget`, `test_rtppacketspscqueue`（预算交互） | `TestClientMediaBudget::*`；`TestRtpPacketSpscQueue::push_pop_roundtrip_releases_budget`, `push_fails_when_budget_exhausted`, `discardAll_empties` |
| `media/RtcpCompoundParser.cpp` | 主要 | `test_rtcpcompoundparser` | `null_context_or_short_buffer_returns_false`, `non_rtcp_returns_false`, `sender_report_updates_clock`, `wrong_ssrc_increments_reject_metric`, `property_random_inputs_no_crash_bounded`（属性式随机） |
| `media/RtpStreamClockContext.cpp` | 主要 / 共用 | `test_rtcpcompoundparser`, `test_rtptruejitterbuffer` | 与 RTCP SR、RTP 时钟路径相关 slots |
| `media/RtpPacketSpscQueue.cpp` | 主要 | `test_rtppacketspscqueue` | `push_pop_roundtrip_releases_budget`, `push_fails_when_budget_exhausted`, `discardAll_empties` |
| `media/RtpTrueJitterBuffer.cpp` | 主要 | `test_rtptruejitterbuffer` | `mode_off_enqueue_no_buffer`, `mode_fixed_deadline_and_pop`, `fifo_mode_pops_by_deadline_order`, `overflow_clears_and_returns_drop`, `late_seq_dropped`, `hole_timeout_requests_keyframe`, `millisUntilNextDue`, `clear_resets_state`, `short_rtp_header_rejected`, `adaptive_sets_deadline`, `ntp_mode_uses_sender_report_mapping`, `hybrid_falls_back_when_sr_stale`, `logMetricsIfDue_no_crash` |
| `media/VideoFrameFingerprintCache.cpp` | 主要 | `test_videoframefingerprintcache` | `empty_stream_tag_record_is_ignored`, `empty_stream_tag_peek_fails`, `record_peek_roundtrip`, `clearStream_removes_only_matching_prefix`, `trim_keeps_size_bounded` |
| `media/ClientVideoDiagCache.cpp` | 主要 | `test_clientvideodiagcache` | `set_empty_preserves_previous_wm`, `set_and_read_wm_name`, `renderStackSummary_null_window`, `renderStackSummary_includes_rhi_env` |
| `media/H264ClientDiag.cpp` | 主要 | `test_h264clientdiag`, `test_h264decoder`（链接依赖） | `maybeDump_*`, `logParams_*`（帧落盘、SPS/PPS 诊断与环境门控） |
| `media/ClientVideoStreamHealth.cpp` | 显示栈假设 / DMA 导出有效值 / 软栈解码线程策略 | `test_h264decoder` | `TestH264Decoder::video_stream_health_dma_effective_respects_software_gl`, `video_stream_health_force_single_thread_under_software_gl` |
| `h264decoder.cpp` | RTP 入向 / 生命周期 / 信号（解码全路径依赖 FFmpeg） | `test_h264decoder` | `takeAndReset_emit_diag_starts_zero`, `feedRtp_updates_lifecycle_even_on_short_packet`, `feedRtp_rejects_too_short`, `feedRtp_rejects_bad_version`, `feedRtp_rejects_wrong_payload_type`, `reset_clears_decoder_state`, `drainIngress_without_queue_emits_finished`, `rtp_with_extension_header_no_crash`, `video_stream_health_dma_effective_respects_software_gl`, `video_stream_health_force_single_thread_under_software_gl` |
| `services/latencycompensator.cpp` | 主要：`test_latencycompensator`；编译依赖：`test_vehiclecontrolservice`（VCS 链接，断言以专测为准） | `test_latencycompensator`, `test_vehiclecontrolservice` | `few_samples_returns_low_confidence_and_current_steering`, `reset_clears_history`, `prediction_clamped_to_max_delta` |
| `services/vehiclecontrolservice.cpp` | 主要 + 凭证 API 可重写 | `test_vehiclecontrolservice`, `test_sessionmanager` | `sendUiCommand_buildsEnvelopeWithTraceAndSession`；`virtual setSessionCredentials` / `clearSessionCredentials`（桩与异常） |
| `services/sessionmanager.cpp` | 主要（异常/分支，L2 近全覆盖） | `test_sessionmanager` | **40 个 SessionManager 向 QTest slot**（另 `initTestCase`/`cleanupTestCase`）：五槽全分支 + `std::exception` / `catch(...)`；`onLogout` 内 `disconnectAll`/`fire(LOGOUT)` 均含 `catch(...)` |
| `app/client_app_bootstrap.cpp`（呈现/QML 解析辅助） | 冒烟（与 `main` 同源 API） | `test_sessionmanager` | `clientBootstrap_applyPresentationSurfaceFormatDefaults_smoke`、`clientBootstrap_probeOpenGl_skippedWhenSkipEnvSet`、`clientBootstrap_runDisplayEnvironmentCheck_skipProbeReturnsZero`、`clientBootstrap_resolveQmlMainUrl_findsOrSkip`、`clientBootstrap_lastHardwarePresentationFalseAfterSkippedProbe`；**完整进程退出链**见 L4 `scripts/verify-client-headless-lifecycle.sh` + `CLIENT_HEADLESS_SMOKE_MS` |
| `utils/WebRtcUrlResolve.cpp` | 主要（L3 纯函数） | `test_webrtcurlresolve` | `baseUrlFromWhep` / `resolveBaseUrl` / `appFromWhepQuery`，`property_random_strings_no_crash`（不变式随机）；`WebRtcStreamManager` 委托同实现 |
| `utils/MqttControlEnvelope.cpp` | 主要（L1 纯函数，MQTT 载荷 / 首选通道解析） | `test_mqttcontrolenvelope` | `prepareForSend_*`、`build*`、`parsePreferredChannel_*`；`mqttcontroller.cpp` 生产路径委托 |
| `utils/WebRtcTransportDispatch.cpp` | 主要（L1，`WebRTCChannel::send` / whep URL / camera→通道） | `test_webrtctransportdispatch` | `sendPayload_*`（含 `sendPayload_tryPost_rejected_fails`）、`synthesizeWhepUrl_format`、`videoChannelForCameraId_*`；`WebRTCChannel.cpp` 委托 |
| `mqttcontroller.cpp` | 编排（载荷构造已委托 `MqttControlEnvelope`） | `test_mqttcontrolenvelope`（逻辑）+ L3/L4 真 broker | `publishMessage` / Paho 仍属集成与环境 |
| `infrastructure/network/WebRTCChannel.cpp` | 部分（发送与 URL 纯逻辑已委托 `WebRtcTransportDispatch`） | `test_webrtctransportdispatch` + `test_sessionmanager`（链入） | 真 `connectFourStreams` 仍为 L3/L4 |
| `services/degradationmanager.cpp` + `DegradationMapping` | 主要 | `test_degradationmanager` | 阈值映射、`policyForLevel` 单调性、无 FSM 低分 SAFETY_STOP、FSM+IDLE 保持 FULL、`manager_upgrade_respects_hysteresis_ms`（注入时钟） |
| `services/safetymonitorservice.cpp` | 主要 | `test_safetymonitorservice` | 延迟连击急停、心跳丢失、deadman、操作员失活限速；`setUnitTestNowMsForTesting` 注入时钟 |
| `app/client_window_frame_policy.cpp` | 主要 | `test_window_frame_policy` | 窗口策略单元测试（见该测试文件） |
| `app/client_startup_readiness_gate.cpp` | 主要 | `test_startup_readiness_profile` | `default_tcp_targets_full_has_four`, `default_tcp_targets_standard_has_backend_mqtt`, `parse_explicit_minimal` 等（见该测试文件） |
| `app/client_present_health_auto_env.cpp` | 主要 | `test_present_health_auto_env` | `ci_empty_env_not_ci`, `ci_ci_true`, `ci_github_actions_nonempty` 等（见该测试文件） |
| `services/errorrecoverymanager.cpp` | 主要 | `test_errorrecoverymanager` | 恢复成功清除、无 action、`clearError`、失败升级到 `safeStopRequired` |
| `services/diagnosticsservice.cpp` + `core/performancemonitor.cpp` | 主要 | `test_diagnosticsservice` | `buildSnapshot` 字段、`collect`→`diagnosticsAvailable` |
| `authmanager.cpp` | 部分（单测注入 API） | `test_sessionmanager` | `setUnitTestServerUrl` / `setUnitTestAuthToken` |
| `services/vehicle_api_parsers.cpp` | 纯 JSON 解析（列表 / 建会话） | `test_vehicle_api_parsers` | `vins` 扁平、`data` 透传、会话 media/control、VIN 不一致 |
| `vehiclemanager.cpp` + `vehiclecatalogclient` + `remotesessionclient` + `httpnetworkhelpers` | 编排 + HTTP 子模块 | `test_vehiclecontrolservice`, `test_sessionmanager` | `ThrowingVehicleManager` 覆盖 `addTestVehicle` / `loadVehicleList` 异常；列表/会话 HTTP 在 `VehicleCatalogClient` / `RemoteSessionClient` |
| `webrtcstreammanager.cpp` | 部分（可重写钩子 + `resolveBaseUrl` 委托 `WebRtcUrlResolve`） | `test_sessionmanager`, `test_webrtcurlresolve` | `ThrowingWebRtcStreamManager` 覆盖 `connectFourStreams` / `disconnectAll` 异常；URL 纯逻辑见 `test_webrtcurlresolve` |
| `core/systemstatemachine.cpp` | 部分 | `test_sessionmanager` | `ThrowingFsm` 覆盖 `fire(AUTH_SUCCESS)` 异常 |
| `infrastructure/itransportmanager.cpp` | 编译入二进制 | `test_vehiclecontrolservice` | 抽象基类，无独立单测 |

## 共享编译依赖（`_TEST_DEPS`，非单测目标）

以下文件被 **`test_configuration` / `test_threadpool` / `test_networkquality` / `test_logger` / `test_vehiclecontrolservice`** 共同链接，用于满足类型与构造；**变更时建议至少跑上述 5 个 CTest 全量**（或 `./scripts/run-client-unit-tests-oneclick.sh`）。

- `core/eventbus.cpp`
- `core/systemstatemachine.cpp`
- `core/configuration.cpp`
- `core/logger.cpp`
- `core/threadpool.cpp`
- `core/networkqualityaggregator.cpp`
- `core/tracing.cpp`
- `core/metricscollector.cpp`
- `vehiclestatus.cpp`
- `nodehealthchecker.cpp`

## Codecov 组件建议（可选）

将仓库路径前缀 `client/src/` 与上表「被测生产源码」列对齐，可为 Codecov `component` 或 `flags` 提供规则，例如：

- `client/src/core/*` → 核心模块
- `client/src/media/*` → 媒体/RTP
- `client/src/services/*` → 服务层
- `client/src/infrastructure/*` → 基础设施

覆盖率 **归因**仍以实际链接的 `.gcno/.gcda` 为准；多测试共享同一 `.cpp` 时，合并报告后按行覆盖即可，审计时结合本表判断「应有测试兜底」的模块。

## 维护约定

- 新增 `add_test` / `add_client_unit_test` / `add_client_isolated_module_test` 时：**同步更新本文件**与 `client/CMakeLists.txt` 中 `_CLIENT_ALL_UNIT_TEST_TARGETS`。
- CMake 权威来源：`client/CMakeLists.txt`（约 `L527` 起 `if(ENABLE_QT6_Test)`）。
