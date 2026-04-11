# 客户端架构落地后 — 阻塞/崩溃/卡死风险核查摘要

## 已缓解

| 风险 | 处理 |
|------|------|
| FSM `fire()` 在持锁时执行 action 导致重入死锁 | `SystemStateMachine::fire` 在更新 `m_current` 后释放互斥锁，再执行 `action` 与 `emit`（见 `systemstatemachine.cpp`）。 |
| 日志 fflush 阻塞 GUI | 既有异步日志队列保留（`main.cpp`）。 |
| Deadman 与 README 不一致 | `SafetyMonitorService` 实现 + 环境变量文档；QML 键盘与 `ControlPanel` 节流发送前调用 `notifyOperatorActivity`。 |
| 控制指令路径分散 | 已收口：QML 仅 `AppContext.sendUiCommand` → `VehicleControlService::sendUiCommand`；`MqttControlEnvelope::buildUiCommandEnvelope` 统一 UI 信封。 |

## 仍须实车/压测验证

- **ControlPanel**：仅 `vehicleControl.sendDriveCommand` / `sendUiCommand` / `requestEmergencyStop`。
- **QML UI 指令**：`AppContext.sendUiCommand`（与 `vehicleControl.sendUiCommand` 同源）。`scripts/verify-client-contract.sh` 禁止 QML `publish("vehicle/control",…)`、`mqttController.send*Command`、`sendUiEnvelopeJson`、legacy 标记。
- **EventBus** 与 **DegradationManager** 与 UI 同线程时，`score` 抖动可能频繁触发降级；已通过滞回与连续采样缓解，仍需线上调参。
- **ControlLoopTicker** 默认关闭；`CLIENT_ENABLE_CONTROL_TICKER=1` 仅为同线程定时占位，非独立 RT 线程。
- **插件**：`CLIENT_PLUGIN_DIR` 指向含 `IPlugin` 的 `.so` 目录；示例：`cmake -DCLIENT_BUILD_EXAMPLE_PLUGIN=ON` 后加载 `build/plugins/`。`PluginContext::eventBus()` 与 `main` / FSM 使用同一 `EventBus::instance()`（不再存在第二套总线实例）。

## 建议监控日志关键字

`[Client][FSM]`、`[Client][Safety]`、`[Client][Control]`、`[Client][Degrade]`、`[Client][EventBus]`
