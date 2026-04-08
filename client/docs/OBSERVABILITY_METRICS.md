# 客户端可观测性速查（Phase 3）

## 日志

- 进程启动：`[Client][Main] process traceId=…`
- 登录后新 trace：`[Client][Session] new traceId after login=…`
- UI 控制：`[Client][Control][UI] type=… traceId=… vin=… sessionId=…`
- 控制载荷中的 `trace_id` 字段（与 `CommandSigner` 的 HMAC 字段独立，不参与签名）。

## 建议生产配置

- `client_config.yaml` → `logging.json_format: true`（或等价环境变量），便于日志平台解析。
- 指标：`metrics.enabled` 与 `HealthChecker` / HTTP 端点（见 `httpendpointserver`）按部署开启。

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

## EventBus 与插件

- 全进程仅使用 **`EventBus::instance()`**（`main` 中 FSM、`PluginContext::eventBus()`、急停等 `EventBus::instance().publish` 为同一实例）。

## Phase 5 延迟微基准

- 构建：`-DBUILD_BENCHMARKS=ON`（需 Google Benchmark）。
- 脚本：`SKIP_CLIENT_BENCHMARK=0 ./scripts/verify-client-benchmark.sh`
