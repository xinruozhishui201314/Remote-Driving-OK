# 客户端驾驶态安全不变式与测试对照（V2 基线）

本文档**形式化**列出远程驾驶客户端在关键状态下应满足的不变式，并映射到现有或建议的自动化验证。与车端看门狗、后端会话锁配合时，以**系统级**验收为准。

## 1. 术语

| 术语 | 含义 |
|------|------|
| 控制面 | MQTT / DataChannel 等下达控车指令的通道 |
| 媒体面 | WebRTC 视频拉流 |
| FSM | `SystemStateMachine`（`IDLE`…`DRIVING`…`DEGRADED`…`EMERGENCY`） |

## 2. 不变式列表

### I1 — 非驾驶态不触发「驾驶专用」急停链的误报

- **陈述**：在 `IDLE` / `READY`（未 `DRIVING`/`DEGRADED`）时，`NetworkQualityAggregator` 的低分**不得**单独将 FSM 推入 `DEGRADED`（已由 `DegradationMapping::restrictToFullWhenIdle` 保证）。
- **测试**：`test_degradationmanager`（idle 限制）、`test_networkquality`（连接状态与分数）。

### I2 — 驾驶态断控制通道须进入可观测安全态

- **陈述**：在 `DRIVING` 下若控制面与 DataChannel **均**不可用，则不得静默发送控车指令；须通过 `VehicleControlService` / MQTT 层记录失败并依赖上层 UI/FSM 策略（`CONNECTION_LOST` → `EMERGENCY` 等由 FSM 定义）。
- **测试**：`test_vehiclecontrolservice`、会话级 `test_sessionmanager`；E2E 建议注入 MQTT 断连。

### I3 — Deadman / 心跳缺失须可触发急停或警告

- **陈述**：`SafetyMonitorService` 在配置阈值内检测到无操作或心跳超时，应 `emergencyStopTriggered` / `safetyWarning`（见 `safetymonitorservice.*`）。
- **测试**：`test_safetymonitorservice`。

### I4 — 呈现降级须影响「可观测质量」并可选触发降级 FSM

- **陈述**：DMA-BUF SG 失败切 CPU 时，应产生结构化日志，并发出 `videoPresentationDegraded` → 网络综合分乘子降低，从而**可能**触发 `DegradationManager` → `NETWORK_DEGRADE`（取决于总分与滞回）。
- **测试**：`test_networkquality::test_mediaPresentationPenaltyAndRecovery`；集成：日志 grep `[Client][NetworkQuality] media presentation penalty`。

### I5 — 状态迁移合法性

- **陈述**：非法 `Trigger` 在某一 `SystemState` 下应拒绝（`fire` 返回 false 并打 `[Client][FSM] invalid transition`）。
- **测试**：`client_systemstatemachine_test`。

## 3. 多流故障隔离（当前策略）

- **槽位**：由流名子串映射 `cam_front` / `cam_rear` / `cam_left` / `cam_right` / `other`。
- **聚合**（`CLIENT_MEDIA_HEALTH_AGGREGATE`）：
  - `weighted`（默认）：主路权重 `CLIENT_MEDIA_HEALTH_WEIGHT_FRONT`（默认 2），辅路单独降级对综合分影响较小。
  - `min`：取各槽位呈现因子最小值（一路坏即整体乘子降至 penalty，最保守）。
- **指标**：`client_media_presentation_degraded_slot_*_total` 用于 Prometheus 按路告警（见 `deploy/prometheus/alerts/client_teleop.yml`）。

## 4. 变更评审检查清单

- [ ] 是否影响 I1–I5 任一条？
- [ ] 日志是否含 `vin`/`sessionId`（若适用）？
- [ ] 是否更新 `docs/CLIENT_STABILITY_RUNBOOK.md` 或本文件？
- [ ] 是否增加/更新 CTest 与 `docs/CLIENT_UNIT_TEST_SOURCE_MAP.md`？
