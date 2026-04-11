# 客户端可观测性速查（Phase 3）

## 日志

- 进程启动：`[Client][Main] process traceId=…`
- 登录后新 trace：`[Client][Session] new traceId after login=…`
- UI 控制：`[Client][Control][UI] type=… traceId=… vin=… sessionId=…`
- 控制载荷中的 `trace_id` 字段（与 `CommandSigner` 的 HMAC 字段独立，不参与签名）。

## 建议生产配置

- `client_config.yaml` → `logging.json_format: true`（或等价环境变量），便于日志平台解析。
- 指标：`metrics.enabled` 与 `HealthChecker` / HTTP 端点（见 `httpendpointserver`）按部署开启。

## 启动时显示环境（`runDisplayEnvironmentCheck`，/metrics Gauge）

| 指标名 | 含义 |
|--------|------|
| `client_display_gl_probe_skipped` | `CLIENT_SKIP_OPENGL_PROBE=1` 时为 1 |
| `client_display_gl_probe_success` | 离屏 GL 上下文探测成功为 1 |
| `client_display_libgl_always_software` | 进程内 `LIBGL_ALWAYS_SOFTWARE` 视为开启为 1 |
| `client_display_software_raster_detected` | `GL_RENDERER` 判定为 llvmpipe/lavapipe 等为 1 |
| `client_display_hw_presentation_ok` | 非软光栅且未强制 `LIBGL_ALWAYS_SOFTWARE` 时为 1（合成链就绪的保守判据） |

**门禁环境变量**（见 `client_display_runtime_policy.h` 注释）：Linux 交互式 xcb 会话默认启用硬件呈现门禁；或 `CLIENT_REQUIRE_HARDWARE_PRESENTATION=1` / `CLIENT_TELOP_STATION=1`。`CLIENT_GPU_PRESENTATION_OPTIONAL=1` 或 `CLIENT_ALLOW_SOFTWARE_PRESENTATION=1` 跳过。无 `DISPLAY`/`WAYLAND_DISPLAY` 的 Linux 会话跳过（CI/离屏）。

### 运行期呈现 SLO（`WebRtcStreamManager::emitVideoPresent1HzSummary`，默认开启）

| 指标名 | 类型 | 含义 |
|--------|------|------|
| `client_video_present_runtime_slo_breach_total` | Counter | maxQueuedLag≥80ms 连续满 5s 时 +1（每次进入 breach 沿边一次） |
| `client_video_present_runtime_queued_lag_sustained` | Gauge | 1=当前处于 sustained 异常；回落至 ≤35ms 连续 5s 后置 0 |

关闭：`CLIENT_VIDEO_RUNTIME_SLO=0`。

## MetricsCollector 计数器（main.cpp 已连接）

| 指标名 | 含义 |
|--------|------|
| `client.auth.login_success_total` | 登录成功 |
| `client.auth.login_failure_total` | 登出/失败 |
| `client.mqtt.connection_success_total` | MQTT/DataChannel 可用边沿 |
| `client.mqtt.connection_lost_total` | 连接丢失 |
| `client.webrtc.stream_connected_total` | 任一路视频连通 |
| `client.webrtc.stream_disconnected_total` | 视频全断 |
| `client.safety.warning_total` | 安全服务告警 |
| `client.degradation.level_change_total` | 降级等级变化 |
| `client_media_presentation_degraded_total` | 呈现路径降级（DMA-BUF/SG→CPU 等），**HTTP /metrics 导出时点号→下划线** |
| `client_media_presentation_degraded_slot_<slot>_total` | 按槽位（`cam_front` 等）的降级次数 |

**说明**：`MetricsCollector::getPrometheusFormat()` 将指标名中的 **`.` 转为 `_`**，以便与 Prometheus 告警规则及 `deploy/prometheus/alerts/client_teleop.yml` 一致；`/metrics/json` 仍使用注册时的原始键名。

### 四路视频 / 主线程 SLO（`CLIENT_VIDEO_PERF_METRICS_1HZ=1` 或 `CLIENT_PERF_DIAG=1`）

在 `WebRtcStreamManager::emitVideoPresent1HzSummary` 中写入 **`MetricsCollector::instance()`**。  
**全进程仅此单例**：`main.cpp` 中登录 / MQTT / WebRTC / 安全 / 降级等计数也写入同一实例；`HttpEndpointServer::handleMetrics` / `handleMetricsJson` 在导出时会 **合并** 该单例（`getPrometheusFormat()` / `getAllMetrics()`），与 HTTP 内置计数器一并返回。

| 指标名 | 类型 | 含义 |
|--------|------|------|
| `client_video_present_libgl_software` | Gauge | `LIBGL_ALWAYS_SOFTWARE` 是否视为开启（0/1） |
| `client_video_present_total_pending` | Gauge | 四路 pending 合计 |
| `client_video_present_handler_depth_sum` | Gauge | 四路主线程 handler 深度之和 |
| `client_video_present_rtp_ring_sum` | Gauge | 四路 RTP 入环深度之和 |
| `client_video_present_max_queued_lag_ms` | Gauge | 秒内 QueuedConnection 最大延迟 |
| `client_video_present_avg_queued_lag_ms` | Gauge | 四路平均 QueuedLag |
| `client_video_present_coalesced_drops_total` | Counter | 合帧丢弃累计（每秒增量写入） |
| `client_video_present_slow_present_sum` | Gauge | 四路 slowPresent 合计 |
| `client_video_present_max_pending_peak` | Gauge | 秒内队列峰值 maxPending |
| `client_video_present_max_handler_us` | Gauge | 秒内 handler 耗时峰值（µs） |
| `client_video_present_qml_layer_events_1s` | Gauge | 秒内 QML 盖层风险事件计数 |
| `client.video.cpu_present_format_reject_total` | Counter | CPU 纹理路径：非 RGBA8888 且严格模式拒绝 |
| `client.video.cpu_present_stride_reject_total` | Counter | RGBA8888 但 `bytesPerLine < width*4` 拒绝 |
| `client.video.cpu_rgba8888_present_apply_total` | Counter | WebRTC→RemoteSurface 强类型契约呈现次数 |
| `client.video.cpu_rgba8888_producer_violation_total` | Counter | 解码输出不满足 RGBA8888/stride 契约次数 |
| `client.video.cpu_rgba8888_surface_contract_reject_total` | Counter | `applyCpuRgba8888Frame` 入口二次校验失败 |
| `client.video.qvideosink_present_contract_reject_total` | Counter | 严格模式下 QVideoSink 路径：非 RGBA8888 或 stride 不足，拒绝 `setVideoFrame` |
| `client.video.qvideosink_present_convert_fail_total` | Counter | 非严格模式下 `convertToFormat(RGBA8888)` 仍不满足契约 |
| `client.video.present_backend_mutex_violation_total` | Counter | `bindVideoOutput` 与 `bindVideoSurface` 同时指向存活对象（不变式破坏，每次进入非法态计 1） |
| `client_video_present_frames_*` | Gauge | `front/rear/left/right` 每秒 `setVideoFrame` 次数 |
| `client_video_present_queued_lag_ms_sample` | Histogram | 每秒以 maxQueuedLag 为样本 |
| `client_main_thread_watchdog_max_tick_gap_ms` | Gauge | 主线程 tick 最大间隔（stall 诊断开时） |
| `client_main_thread_stall_events_total` | Counter | 主线程 stall 次数累计（每秒增量） |

### 一键排障环境变量（须在启动前设置）

| 变量 | 作用 |
|------|------|
| `CLIENT_PERF_DIAG=1` | 对**未设置**的子变量写入默认：`CLIENT_VIDEO_PRESENT_1HZ_SUMMARY`、`CLIENT_MAIN_THREAD_STALL_DIAG`、`CLIENT_VIDEO_SCENE_GL_LOG`、`CLIENT_VIDEO_PERF_JSON_1HZ`、`CLIENT_VIDEO_PERF_METRICS_1HZ`、`CLIENT_VIDEO_PERF_TRACE_SPAN`、`CLIENT_VIDEO_PERF_RUSAGE_1HZ`（见 `client_perf_diag_env.h`） |
| `CLIENT_VIDEO_PERF_JSON_1HZ=1` | 每秒一行 `[Client][VideoPerf][JSON] {…}`（schema `client.video_present_1hz.v1`） |
| `CLIENT_VIDEO_PERF_TRACE_SPAN=1` | 每秒 `Tracing::beginSpan("client_video","present_1hz_summary")`（受全局采样率影响；会额外打 Tracing 的 debug 日志） |
| `CLIENT_VIDEO_PERF_RUSAGE_1HZ=1` | 在 JSON 中附加 `process_cpu_delta_us`（Unix `getrusage` 累计 CPU 时间差分；首秒无该字段） |

## EventBus 与插件

- 全进程仅使用 **`EventBus::instance()`**（`main` 中 FSM、`PluginContext::eventBus()`、急停等 `EventBus::instance().publish` 为同一实例）。

## Phase 5 延迟微基准

- 构建：`-DBUILD_BENCHMARKS=ON`（需 Google Benchmark）。
- 脚本：`SKIP_CLIENT_BENCHMARK=0 ./scripts/verify-client-benchmark.sh`
