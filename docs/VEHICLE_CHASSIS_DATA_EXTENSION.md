# 底盘数据扩展指南

## 概述

本文档说明如何扩展底盘数据字段，添加新的数据上报项。

## 扩展步骤

### 1. 注册数据生成器

在 `chassis_data_generator.cpp` 的构造函数中添加新字段的生成器：

```cpp
ChassisDataGenerator::ChassisDataGenerator() {
    // ... 现有注册 ...
    
    // 注册新字段
    registerGenerator("new_field", [this](const VehicleController::ControlCommand& cmd, double dt) {
        return generateNewField(cmd, dt);
    });
}
```

### 2. 实现生成器函数

在 `chassis_data_generator.h` 中声明：

```cpp
private:
    DataValue generateNewField(const VehicleController::ControlCommand& cmd, double dt);
```

在 `chassis_data_generator.cpp` 中实现：

```cpp
ChassisDataGenerator::DataValue ChassisDataGenerator::generateNewField(
    const VehicleController::ControlCommand& cmd, double dt) {
    // 实现数据生成逻辑
    double value = /* 计算值 */;
    return DataValue(value);
}
```

### 3. 添加到配置

在 `vehicle_config.cpp` 的 `setDefaults()` 中添加字段配置：

```cpp
ChassisDataConfig newField;
newField.name = "new_field";
newField.type = "double";
newField.enabled = true;
newField.min_value = 0.0;
newField.max_value = 100.0;
m_chassisDataFields.push_back(newField);
```

### 4. 更新配置文件示例

在 `config/vehicle_config.json.example` 中添加：

```json
{
  "name": "new_field",
  "enabled": true,
  "type": "double",
  "min_value": 0.0,
  "max_value": 100.0
}
```

### 5. 客户端支持（可选）

如果需要客户端显示新字段，更新 `client/src/vehiclestatus.h` 和 `vehiclestatus.cpp`。

## 示例：添加"电机转速"字段

### 1. 注册生成器

```cpp
registerGenerator("motor_rpm", [this](const VehicleController::ControlCommand& cmd, double dt) {
    return generateMotorRpm(cmd, dt);
});
```

### 2. 实现生成器

```cpp
ChassisDataGenerator::DataValue ChassisDataGenerator::generateMotorRpm(
    const VehicleController::ControlCommand& cmd, double dt) {
    // 根据油门和速度计算电机转速
    double rpm = cmd.throttle * 3000.0 + m_simulatedSpeed * 100.0;
    rpm = std::max(0.0, std::min(5000.0, rpm));
    return DataValue(rpm);
}
```

### 3. 添加到配置

```cpp
ChassisDataConfig motorRpmField;
motorRpmField.name = "motor_rpm";
motorRpmField.type = "double";
motorRpmField.enabled = true;
motorRpmField.min_value = 0.0;
motorRpmField.max_value = 5000.0;
m_chassisDataFields.push_back(motorRpmField);
```

## 注意事项

1. **字段名唯一性**：确保字段名不与现有字段重复
2. **数据类型**：支持 `double`、`int`、`string` 三种类型
3. **性能**：生成器函数应尽量高效，避免阻塞
4. **线程安全**：生成器可能在多线程环境中调用，注意线程安全

## 从真实硬件获取数据

如果要从真实硬件（如CAN总线、ROS2）获取数据，可以：

1. 在生成器中调用硬件接口
2. 使用全局变量或单例存储硬件数据
3. 确保数据获取的线程安全

示例：

```cpp
ChassisDataGenerator::DataValue ChassisDataGenerator::generateRealSpeed(
    const VehicleController::ControlCommand& cmd, double dt) {
    // 从CAN总线读取真实速度
    double realSpeed = canBus->readSpeed();
    return DataValue(realSpeed);
}
```
