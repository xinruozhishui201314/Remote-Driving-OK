# 档位选择功能实现总结

## 功能概述

在主驾驶界面上选择档位（P/N/R/D），发送给车端，车端接收并处理档位命令，然后将档位反馈到 MQTT 消息中发给客户端，客户端接收并显示档位。

## 实现状态

### ✅ 已完成的修改

1. **客户端档位选择发送**
   - `client/qml/DrivingInterface.qml`: 档位选择时自动发送 MQTT 命令
   - `client/src/mqttcontroller.cpp`: 增加档位命令发送日志

2. **车端档位命令接收和处理**
   - `Vehicle-side/src/control_protocol.cpp`: 添加 `type: "gear"` 消息处理
   - `Vehicle-side/src/vehicle_controller.cpp`: 增加档位处理日志

3. **车端档位反馈**
   - `Vehicle-side/src/mqtt_handler.cpp`: 在底盘数据中包含档位，日志显示档位数值

4. **客户端档位接收和显示**
   - `client/src/mqttcontroller.cpp`: 增加档位消息接收日志
   - `client/src/vehiclestatus.cpp`: 增加档位状态更新日志

## 验证结果

### 车端验证 ✅

运行 `bash scripts/test-gear-complete-flow.sh` 的结果：

1. ✅ **车端接收档位命令**: 
   ```
   [GEAR] [Control] 收到消息类型: gear
   [GEAR] ========== [Control] 收到档位命令 ==========
   [GEAR] ✓ 从 JSON 中提取档位值: 1 (D)
   ```

2. ✅ **车端处理档位命令**:
   ```
   [GEAR] ✓✓✓ 档位命令已应用: 1 (D)
   [GEAR] [VehicleController] applyGear: 1 (D)
   [GEAR] [VehicleController] 控制指令: ..., 档位=1 (D) [档位变化: N -> D]
   ```

3. ✅ **车端发送档位反馈**:
   ```
   [CHASSIS_DATA] 发布 #250 | ... | 档位: D (数值: 1) | ...
   ```

4. ✅ **MQTT 消息验证**:
   ```json
   {"timestamp":...,"speed":0.0,"gear":1,"steering":0.0,...}
   ```
   档位字段 `gear: 1` 已正确包含在消息中。

### 客户端验证 ⚠️

- ⚠️ **mosquitto_sub 进程未运行**: 客户端需要手动点击「连接车端」按钮才能启动 mosquitto_sub 进程接收消息。

## 关键日志关键字

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
- `[CHASSIS_DATA] 发布 #... | 档位: D (数值: 1) | ...`

## 档位值映射

- **-1**: 倒档 (R)
- **0**: 空档 (N) 或停车档 (P)
- **1**: 前进档 (D)

## 消息格式

### 客户端发送（档位命令）
```json
{
  "type": "gear",
  "value": 1,
  "timestamp": 1770634500000,
  "vin": "123456789"
}
```

### 车端反馈（底盘数据）
```json
{
  "timestamp": 1770634504551,
  "speed": 0.0,
  "gear": 1,
  "steering": 0.0,
  "throttle": 0.0,
  "brake": 1.0,
  "battery": 99.8,
  "odometer": 0.0,
  "voltage": 48.0,
  "current": 0.0,
  "temperature": 25.0,
  "remote_control_enabled": true,
  "driving_mode": "远驾"
}
```

## 注意事项

1. **远驾接管必须启用**: 档位命令只有在远驾接管启用时才会被处理。如果未启用，车端会忽略档位命令并记录警告日志：`[GEAR] ⚠ 远驾接管未启用，忽略档位命令`

2. **启动顺序**:
   - 先发送 `start_stream` 命令（启动底盘数据发布）
   - 再发送 `remote_control` 命令（启用远驾接管）
   - 然后发送 `gear` 命令（选择档位）

3. **客户端连接**: 客户端需要点击「连接车端」按钮才能启动 mosquitto_sub 进程接收车端反馈。

## 验证步骤

1. **启动系统**:
   ```bash
   bash scripts/start-full-chain.sh manual
   ```

2. **在客户端界面操作**:
   - 登录（123/123）
   - 选择车辆（123456789）
   - 点击「连接车端」（启动 mosquitto_sub）
   - 点击「远驾接管」（启用远驾接管）
   - 在主界面选择档位（P/N/R/D）

3. **检查日志**:
   ```bash
   # 客户端日志
   docker compose logs teleop-client-dev -f | grep -E '\[GEAR\]|档位'
   
   # 车端日志
   docker compose logs remote-driving-vehicle-1 -f | grep -E '\[GEAR\]|档位'
   ```

4. **运行自动化测试**:
   ```bash
   bash scripts/test-gear-complete-flow.sh
   ```

## 修改的文件清单

### 客户端
- `client/qml/DrivingInterface.qml` - 档位选择时发送命令
- `client/src/mqttcontroller.cpp` - 档位命令发送和消息接收日志
- `client/src/vehiclestatus.cpp` - 档位状态更新日志

### 车端
- `Vehicle-side/src/control_protocol.cpp` - 档位命令接收和处理
- `Vehicle-side/src/vehicle_controller.cpp` - 档位应用日志
- `Vehicle-side/src/mqtt_handler.cpp` - 档位反馈日志

### 测试脚本
- `scripts/test-gear-complete-flow.sh` - 完整流程测试脚本
- `scripts/verify-gear-flow.sh` - 验证脚本

## 相关文档

- `docs/GEAR_SELECTION_FEATURE.md` - 详细功能文档
