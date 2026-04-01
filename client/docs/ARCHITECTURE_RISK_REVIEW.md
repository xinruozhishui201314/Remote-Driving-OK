# 客户端架构落地后 — 阻塞/崩溃/卡死风险核查摘要

## 已缓解

| 风险 | 处理 |
|------|------|
| FSM `fire()` 在持锁时执行 action 导致重入死锁 | `SystemStateMachine::fire` 在更新 `m_current` 后释放互斥锁，再执行 `action` 与 `emit`（见 `systemstatemachine.cpp`）。 |
| 日志 fflush 阻塞 GUI | 既有异步日志队列保留（`main.cpp`）。 |
| Deadman 与 README 不一致 | `SafetyMonitorService` 实现 + 环境变量文档；QML 键盘与 `ControlPanel` 节流发送前调用 `notifyOperatorActivity`。 |
| 控制指令路径分散 | 默认走 `VehicleControlService::sendUiCommand`；`CLIENT_LEGACY_CONTROL_ONLY=1` 回退旧路径。 |

## 仍须实车/压测验证

- **ControlPanel** 仍直接调用 `mqttController.sendDriveCommand`，未经过 `VehicleControlService`（与 DataChannel 统一封装存在双路径）；后续可改为 C++ 统一入口。
- **EventBus** 与 **DegradationManager** 与 UI 同线程时，`score` 抖动可能频繁触发降级；已通过滞回与连续采样缓解，仍需线上调参。
- **ControlLoopTicker** 默认关闭；`CLIENT_ENABLE_CONTROL_TICKER=1` 仅为同线程定时占位，非独立 RT 线程。

## 建议监控日志关键字

`[Client][FSM]`、`[Client][Safety]`、`[Client][Control]`、`[Client][Degrade]`、`[Client][EventBus]`
