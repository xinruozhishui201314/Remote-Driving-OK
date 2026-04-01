# 远驾接管确认消息接收修复

## 问题描述

客户端点击"远驾接管"按钮后，按钮状态没有变化，无法显示"远驾已接管"状态。

## 根本原因

1. **客户端没有编译 MQTT 支持**：客户端容器中没有安装 Paho MQTT C++ 库，`ENABLE_MQTT_PAHO` 未定义。
2. **客户端无法接收消息**：在 `#else` 分支中，`onConnected()` 只是打印警告，并没有实际订阅主题或启动 `mosquitto_sub` 进程来接收消息。
3. **车端确认消息无法到达客户端**：即使车端成功发送了 `remote_control_ack` 消息，客户端也无法接收。

## 解决方案

在没有编译 MQTT 支持的情况下，使用 `mosquitto_sub` 作为临时解决方案来接收消息：

### 修改内容

1. **`client/src/mqttcontroller.h`**：
   - 添加 `QProcess* m_mosquittoSubProcess` 成员变量，用于运行 `mosquitto_sub` 进程。

2. **`client/src/mqttcontroller.cpp`**：
   - 在 `connectToBroker()` 的 `#else` 分支中，延迟连接并调用 `onConnected()`。
   - 在 `onConnected()` 的 `#else` 分支中：
     - 启动 `mosquitto_sub` 进程订阅 `vehicle/status` 主题。
     - 如果设置了 VIN，也订阅 `vehicle/{vin}/status` 主题。
     - 连接 `readyReadStandardOutput` 信号，解析输出并调用 `onMessageReceived`。
     - 连接 `finished` 信号，处理进程退出。
   - 在 `disconnectFromBroker()` 的 `#else` 分支中，停止 `mosquitto_sub` 进程。
   - 在析构函数中，确保 `mosquitto_sub` 进程被正确清理。
   - 增加详细日志，便于追踪消息接收流程。

### 关键实现细节

1. **消息解析**：
   - `mosquitto_sub` 默认输出格式：每行 `"主题 消息内容"`。
   - 解析时，第一个空格前是主题，之后是消息内容。

2. **进程管理**：
   - 使用 `QProcess` 管理 `mosquitto_sub` 进程。
   - 在重新订阅时，先停止旧进程，再启动新进程。
   - 在断开连接时，优雅地终止进程（先 `terminate()`，等待 3 秒，如果未退出则 `kill()`）。

3. **错误处理**：
   - 检查 `mosquitto_sub` 是否启动成功。
   - 处理进程崩溃和异常退出。
   - 记录详细的错误日志。

## 验证步骤

### 自动化验证脚本

运行自动化测试脚本：
```bash
bash scripts/test-remote-control-flow.sh
```

该脚本会：
1. 检查服务状态（客户端、车端、MQTT Broker）
2. 检查客户端进程和 mosquitto_sub 进程
3. 发送测试 remote_control 消息
4. 检查车端响应
5. 检查客户端消息接收

### 手动验证步骤

1. **启动系统**：
   ```bash
   bash scripts/start-full-chain.sh manual
   ```

2. **检查客户端进程**：
   ```bash
   docker compose exec client-dev ps aux | grep mosquitto_sub
   ```
   - 如果看到 mosquitto_sub 进程，说明已连接 MQTT。

3. **在客户端界面操作**：
   - 登录（123/123）
   - 选择车辆（123456789）
   - 点击「连接车端」按钮（这会触发 `connectToBroker()`）
   - 等待 mosquitto_sub 进程启动（可通过 `ps aux | grep mosquitto_sub` 检查）

4. **检查客户端日志**（通过启动脚本的输出或容器日志）：
   - 确认看到 `[MQTT] 使用 mosquitto_sub 订阅状态主题`。
   - 确认看到 `[MQTT] ✓ mosquitto_sub 已启动`。
   - 确认看到 `[CHASSIS_DATA] 开始接收 MQTT 消息`。

5. **点击"远驾接管"按钮**：
   - 确认客户端发送了 `remote_control` 指令。
   - 确认车端发送了 `remote_control_ack` 消息。
   - 确认客户端收到了 `remote_control_ack` 消息（日志中应看到 `[REMOTE_CONTROL] ========== 收到远驾接管确认消息 ==========`）。
   - 确认 `vehicleStatus.remoteControlEnabled` 更新为 `true`。
   - 确认按钮文本变为"远驾已接管"。

6. **检查车端日志**：
   ```bash
   docker compose logs remote-driving-vehicle-1 --tail 200 | grep -E "remote_control|publishRemoteControlAck"
   ```
   - 确认车端成功发送了 `remote_control_ack` 消息。
   - 确认消息包含 `remote_control_enabled: true` 和 `driving_mode: "远驾"`。

## 日志关键字

### 客户端日志（成功时）：
- `[MQTT] 使用 mosquitto_sub 订阅状态主题`
- `[MQTT] ✓ mosquitto_sub 已启动`
- `[MQTT] [mosquitto_sub] 收到消息，主题: vehicle/status`
- `[REMOTE_CONTROL] ========== 收到远驾接管确认消息 ==========`
- `[REMOTE_CONTROL] ✓✓✓ [VEHICLE_STATUS] 远驾接管状态变化: false -> true`
- `[REMOTE_CONTROL] ✓✓✓ [VEHICLE_STATUS] 驾驶模式变化: 自驾 -> 远驾`

### 车端日志（成功时）：
- `[REMOTE_CONTROL] ========== [publishRemoteControlAck] 开始发送确认消息 ==========`
- `[REMOTE_CONTROL] ✓✓✓✓✓ 已成功发送远驾接管确认消息到主题: vehicle/status`

## 后续改进建议

1. **安装 Paho MQTT C++**：
   - 在客户端 Dockerfile 中安装 Paho MQTT C++ 库。
   - 编译时启用 `ENABLE_MQTT_PAHO`，使用原生 MQTT 客户端库，性能更好。

2. **消息格式优化**：
   - 考虑使用 MQTT v5 的 User Properties 来传递元数据。
   - 统一消息格式，便于解析和扩展。

3. **错误恢复**：
   - 如果 `mosquitto_sub` 进程崩溃，自动重启。
   - 添加重连机制，确保消息不丢失。

## 相关文件

- `client/src/mqttcontroller.h`
- `client/src/mqttcontroller.cpp`
- `client/src/vehiclestatus.cpp`
- `Vehicle-side/src/mqtt_handler.cpp`
- `Vehicle-side/src/control_protocol.cpp`
- `Vehicle-side/src/vehicle_controller.cpp`
