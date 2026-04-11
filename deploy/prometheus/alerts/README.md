# Prometheus 告警规则（Teleop）

| 文件 | 说明 |
|------|------|
| `client_teleop.yml` | 客户端 `/metrics` 指标：呈现降级、主路相机、WebRTC 断连、安全告警、降级抖动 |

## 接入步骤

1. 在 `deploy/prometheus/prometheus.yml` 顶层增加：

```yaml
rule_files:
  - "alerts/client_teleop.yml"
```

2. 确保 Prometheus 能抓取客户端 HTTP 指标端点（`MetricsCollector` 经 `HttpEndpointServer` 合并导出）。

3. **阈值**：`expr` 中的速率与 `for` 时长请按现场会话量与相机路数调整；示例偏保守，适合单机联调与小规模试点。

## 指标命名说明

- 呈现相关：`client_media_presentation_degraded_total`、`client_media_presentation_degraded_slot_<slot>_total`。
- **全量** `MetricsCollector` 导出（`/metrics` 文本）在 `getPrometheusFormat()` 内将 **`.` 转为 `_`**（例如 `client_auth_login_success_total`），与本目录告警规则一致；JSON 导出仍为代码中注册的原始键名。
