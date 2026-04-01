# 档位选择功能实现文档

## 功能描述

在主驾驶界面上选择档位（P/N/R/D），发送给车端，车端接收并处理档位命令，然后将档位反馈到 MQTT 消息中发给客户端，客户端接收并显示档位。

## 实现流程

```
客户端界面选择档位
    ↓
QML: onCurrentGearChanged
    ↓
发送 MQTT 命令: {"type":"gear","value":1}
    ↓
车端接收: control_protocol.cpp handle_control_json
    ↓
检查远驾接管状态（必须启用）
    ↓
提取 value 字段（档位值）
    ↓
调用 VehicleController::processCommand
    ↓
应用档位: applyGear
    ↓
车端生成底盘数据（包含档位）
    ↓
发布到 vehicle/status 主题
    ↓
客户端接收（mosquitto_sub）
    ↓
解析 JSON，提取 gear 字段
    ↓
更新 VehicleStatus.gear
    ↓
QML 显示档位
```

## 修改的文件

### 1. 客户端

#### `client/qml/DrivingInterface.qml`
- **修改位置**: `onCurrentGearChanged` 处理器
- **功能**: 当档位改变时，发送 MQTT 命令到车端
- **档位映射**:
  - "P" → 0 (空档)
  - "N" → 0 (空档)
  - "R" → -1 (倒档)
  - "D" → 1 (前进档)

#### `client/src/mqttcontroller.cpp`
- **修改位置**: `sendGearCommand` 函数
- **功能**: 增加详细日志，记录档位命令发送过程

#### `client/src/mqttcontroller.cpp`
- **修改位置**: `onMessageReceived` (mosquitto_sub 消息处理)
- **功能**: 检查消息中是否包含档位信息，记录日志

#### `client/src/vehiclestatus.cpp`
- **修改位置**: `updateStatus` 函数中的 gear 处理
- **功能**: 增加详细日志，记录档位变化

### 2. 车端

#### `Vehicle-side/src/control_protocol.cpp`
- **修改位置**: `handle_control_json` 函数
- **功能**: 
  - 添加 `type: "gear"` 消息类型的处理
  - 提取 `value` 字段作为档位值
  - 检查远驾接管状态（必须启用才能处理档位命令）
  - 调用 `VehicleController::processCommand` 应用档位
  - 增加详细日志

#### `Vehicle-side/src/control_protocol.cpp`
- **修改位置**: 通用控制指令处理部分
- **功能**: 在应用控制指令时增加档位日志

#### `Vehicle-side/src/vehicle_controller.cpp`
- **修改位置**: `processCommand` 函数
- **功能**: 增加档位变化日志

#### `Vehicle-side/src/vehicle_controller.cpp`
- **修改位置**: `applyGear` 函数
- **功能**: 增加档位应用日志

#### `Vehicle-side/src/mqtt_handler.cpp`
- **修改位置**: `publishChassisData` 函数
- **功能**: 在日志中显示档位数值

## 日志关键字

### 客户端日志

**档位选择**:
- `[GEAR] ========== [QML] 档位选择变化 ==========`
- `[GEAR] 档位字符串: D`
- `[GEAR] 档位数值: 1`
- `[GEAR] ✓ 已发送档位命令: D (数值: 1)`

**档位命令发送**:
- `[GEAR] ========== [MQTT] 发送档位命令 ==========`
- `[GEAR] 档位数值: 1 (D)`
- `[GEAR] ✓✓✓ 已发送档位命令: gear=1`

**档位消息接收**:
- `[GEAR] [MQTT] 消息中包含档位: D (原始值: 1)`
- `[GEAR] [mosquitto_sub] 消息包含档位信息`

**档位状态更新**:
- `[GEAR] ========== [VEHICLE_STATUS] 档位变化 ==========`
- `[GEAR] 旧档位: N -> 新档位: D`
- `[GEAR] ✓✓✓ 档位已更新: N -> D`

### 车端日志

**档位命令接收**:
- `[GEAR] [Control] 收到消息类型: gear`
- `[GEAR] ========== [Control] 收到档位命令 ==========`
- `[GEAR] ✓ 从 JSON 中提取档位值: 1 (D)`

**档位命令处理**:
- `[GEAR] 当前控制指令: steering=..., gear=...`
- `[GEAR] ✓✓✓ 档位命令已应用: 1 (D)`
- `[GEAR] [VehicleController] 控制指令: ..., 档位=1 (D) [档位变化: N -> D]`
- `[GEAR] [VehicleController] applyGear: 1 (D)`

**档位反馈发送**:
- `[CHASSIS_DATA] 发布 #... | 档位: D (数值: 1)`

## 验证步骤

1. **启动系统**:
   ```bash
   bash scripts/start-full-chain.sh manual
   ```

2. **在客户端界面操作**:
   - 登录（123/123）
   - 选择车辆（123456789）
   - 点击「连接车端」
   - 点击「远驾接管」（必须启用，否则档位命令会被忽略）
   - 在主界面选择档位（P/N/R/D）

3. **检查日志**:
   ```bash
   # 客户端日志
   docker compose logs client-dev -f | grep -E '\[GEAR\]|档位'
   
   # 车端日志
   docker compose logs remote-driving-vehicle-1 -f | grep -E '\[GEAR\]|档位'
   ```

4. **运行自动化验证**:
   ```bash
   bash scripts/verify-gear-flow.sh
   ```

## 注意事项

1. **远驾接管必须启用**: 档位命令只有在远驾接管启用时才会被处理。如果未启用，车端会忽略档位命令并记录警告日志。

2. **档位值映射**:
   - -1: 倒档 (R)
   - 0: 空档 (N) 或停车档 (P)
   - 1: 前进档 (D)

3. **消息格式**:
   - 客户端发送: `{"type":"gear","value":1,"timestamp":...,"vin":"..."}`
   - 车端反馈: `{"timestamp":...,"gear":1,...}`

4. **日志追踪**: 所有关键步骤都有 `[GEAR]` 前缀的日志，便于追踪整个流程。

## 相关文件

- `client/qml/DrivingInterface.qml` - 档位选择 UI 和命令发送
- `client/src/mqttcontroller.cpp` - MQTT 命令发送和消息接收
- `client/src/vehiclestatus.cpp` - 档位状态更新
- `Vehicle-side/src/control_protocol.cpp` - 档位命令接收和处理
- `Vehicle-side/src/vehicle_controller.cpp` - 档位应用
- `Vehicle-side/src/mqtt_handler.cpp` - 档位反馈发送
- `scripts/verify-gear-flow.sh` - 自动化验证脚本
