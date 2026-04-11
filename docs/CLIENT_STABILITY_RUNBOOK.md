# 客户端稳定性运维手册（MVP→V1 基线）

本文档与路线图 **MVP（日志+Runbook+媒体回退）/ V1（FSM↔网络与媒体质量绑定+重连恢复）/ V2（SLO+告警+多流隔离+不变式）** 对齐，供研发与运维排障。

## 1. 日志前缀速查（grep）

| 前缀 | 含义 |
|------|------|
| `[Client][FSM]` | 系统状态机迁移 |
| `[Client][NetworkQuality]` | 综合分、`mediaFactor`、呈现惩罚 |
| `[Client][Degrade]` / `[Client][DegradationManager]` | 降级级别、FSM `NETWORK_DEGRADE`/`RECOVER` |
| `[Client][Session]` | 登录、选车、会话、logout |
| `[CLIENT][MQTT]` | Broker 连接、订阅、断线 |
| `[Client][WebRTC]` | 拉流、重连、关键帧请求 |
| `[Client][WebRTC][DmaBufSG]` | DMA-BUF SceneGraph → CPU RGBA 回退 |
| `[Client][UI][RemoteVideoSurface][DmaBufSG]` | 呈现侧回退决策（连续失败/永久错误） |
| `[Client][Safety]` | 死手、延迟、心跳、急停 |

## 2. 关键环境变量

| 变量 | 作用 |
|------|------|
| `CLIENT_MEDIA_HEALTH_AGGREGATE` | `weighted`（默认）：主路 `cam_front` 权重高，辅路单独降级对综合分影响较小；`min`：取各槽位因子最小值（最保守） |
| `CLIENT_MEDIA_HEALTH_WEIGHT_FRONT` | weighted 模式下主路权重（默认 `2`，范围约 0.5–10） |
| `CLIENT_MEDIA_HEALTH_RECOVERY_MS` | 每槽位呈现惩罚持续时间（默认 20000，范围 3000–120000） |
| `CLIENT_DMABUF_SG_FAIL_STREAK_BEFORE_CPU` | 有上一帧时 SG 失败多少次后切 CPU（默认 5） |
| `CLIENT_DEADMAN_ENABLED` / `CLIENT_DEADMAN_TIMEOUT_MS` | 死手 |
| `ZLM_VIDEO_URL` / `MQTT_BROKER_URL` | 数据面/控制面入口 |

## 3. 媒体路径（MVP）

1. **DMA-BUF / SceneGraph 失败**：导入失败时尽量**保留上一帧**；永久错误或连续失败触发 **CPU RGBA**，并 `requestKeyframe`（见 `[Client][WebRTC][DmaBufSG]`）。
2. **网络分联动（V1）**：任一路 `videoPresentationDegraded` → `NetworkQualityAggregator::noteMediaPresentationDegraded` → 综合分乘子暂降 → `DegradationManager` 可能驱动 **FSM DEGRADED**（与 `VehicleStatus` 链路分共同作用）。

## 4. MQTT 重连后会话恢复（V1 检查清单）

自动行为（代码：`MqttController::onConnected`）：

- `automatic_reconnect` + 回调排队到主线程；
- 连接成功后 **重新订阅** `m_statusTopic` 与 `vehicle/<VIN>/status`（VIN 非空时）。

**仍需人工/集成验证的场景：**

- 断线期间是否丢失必须下发的 **会话/锁** 语义（依赖后端与车端契约）；
- 重连后 **控车凭证** 是否仍有效（`VehicleControlService::clearSessionCredentials` 仅在 logout/清 VIN 等路径调用，不断 MQTT 时通常保留）；
- WebRTC 与 MQTT **同时抖动** 时，UI 是否仍与 `SystemStateMachine` 一致。

## 5. 指标与 Prometheus 告警（V2）

- `client_media_presentation_degraded_total`：呈现路径降级总次数。
- `client_media_presentation_degraded_slot_<cam_front|cam_rear|cam_left|cam_right|other>_total`：按逻辑槽位计数。
- 其余计数器经 `/metrics` 导出时 **点号已规范为下划线**（如 `client_auth_login_success_total`），与 `deploy/prometheus/alerts/client_teleop.yml` 一致。

**仓库内可应用配置：**

- `deploy/prometheus/prometheus.yml` 已 `rule_files: [alerts/client_teleop.yml]`。
- `deploy/prometheus/alerts/README.md` — 接入说明与调参提示。

**SLO 建议：** 见告警规则内 `expr` / `for`，按现场会话量调整。

## 6. 自动化验证

```bash
# 稳定性相关单测（本地已配置 Qt/CMake 时）
./scripts/verify-client-stability.sh

# 或一键客户端单测
./scripts/run-client-unit-tests-oneclick.sh
```

## 7. 相关文档

- `docs/CLIENT_SAFETY_INVARIANTS.md` — 驾驶态安全不变式与测试对照（V2）
- `client/docs/CALLCHAIN_AND_ARCHITECTURE.md` — 调用链
