# 车辆底盘数据显示功能说明

## 概述

本功能实现了车辆底盘数据的完整数据流：**车端模拟生成 → Mosquitto MQTT Broker → 客户端主驾驶界面显示**。

## 数据流架构

```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│   车端      │ 发布    │  Mosquitto   │ 订阅    │   客户端    │
│             │────────>│   MQTT       │────────>│             │
│ ChassisData │         │   Broker     │         │ QML 界面    │
│ Generator   │         │              │         │             │
└─────────────┘         └──────────────┘         └─────────────┘
     │                        │                        │
     │ vehicle/status        │ vehicle/status        │
     │ (50Hz)                │ (转发)                 │
     │                        │                        │
     └────────────────────────┴────────────────────────┘
```

## 数据字段

### 基础字段（必需）
- `timestamp`: 时间戳（毫秒）
- `speed`: 速度（km/h）
- `battery`: 电池电量（%）
- `gear`: 档位（-1:倒档, 0:空档, 1:前进）
- `steering`: 转向角度（-1.0 到 1.0）
- `throttle`: 油门（0.0 到 1.0）
- `brake`: 刹车（0.0 到 1.0）

### 扩展字段（可选）
- `odometer`: 累计里程（km）
- `voltage`: 电池电压（V）
- `current`: 电流（A）
- `temperature`: 温度（°C）

## 配置

### 车端配置

配置文件：`Vehicle-side/config/vehicle_config.json`

```json
{
  "status_publish_frequency": 50,
  "chassis_data_fields": [
    {
      "name": "speed",
      "enabled": true,
      "type": "double",
      "min_value": 0.0,
      "max_value": 200.0,
      "description": "车辆速度 (km/h)"
    },
    {
      "name": "odometer",
      "enabled": true,
      "type": "double",
      "min_value": 0.0,
      "max_value": 999999.0,
      "description": "累计里程 (km)"
    },
    // ... 其他字段
  ]
}
```

**关键配置项**：
- `status_publish_frequency`: 状态发布频率（Hz），默认 50Hz
- `chassis_data_fields[].enabled`: 是否启用该字段

### MQTT 主题

- **发布主题**：`vehicle/status`（通用）或 `vehicle/{VIN}/status`（特定车辆）
- **订阅主题**：客户端订阅 `vehicle/status` 和 `vehicle/{VIN}/status`
- **控制主题**：`vehicle/control`（用于发送 `start_stream` 命令）

## 使用流程

### 1. 启动全链路

```bash
bash scripts/start-full-chain.sh manual
```

这将启动：
- Mosquitto MQTT Broker
- 车端（Vehicle-side）
- 客户端（Client）

### 2. 验证数据流

```bash
bash scripts/verify-chassis-data-display.sh
```

该脚本将：
- 检查 Mosquitto Broker 是否运行
- 订阅 `vehicle/status` 主题
- 验证数据格式和字段完整性
- 显示数据摘要

### 3. 在客户端查看数据

1. 启动客户端后，登录并选择车辆
2. 在主驾驶界面点击「连接」按钮
3. 连接成功后，在**控制面板**（右侧）查看车辆状态信息：
   - **速度**：实时显示车辆速度（km/h）
   - **电池**：显示电池电量和进度条
   - **里程**：显示累计里程（km）
   - **电压**：显示电池电压（V）
   - **电流**：显示电流（A）
   - **温度**：显示温度（°C），根据温度值变色

## 界面显示位置

### ControlPanel.qml（控制面板）

位置：客户端右侧控制面板 → 「📊 车辆状态」区域

显示内容：
- 速度：大号绿色数字，单位 km/h
- 电池：百分比 + 进度条（绿色/红色根据电量）
- 里程：蓝色数字，单位 km
- 电压：绿色数字，单位 V
- 电流：橙色数字，单位 A
- 温度：根据温度值变色（高温红色，低温蓝色，正常绿色），单位 °C

### DrivingInterface.qml（主驾驶界面）

位置：主界面中央速度表

显示内容：
- 速度：大号速度表（圆形），实时更新
- 档位：显示当前档位（R/N/D）
- 转向：速度表下方显示转向角度

## 数据生成逻辑

车端使用 `ChassisDataGenerator` 类模拟生成底盘数据：

- **速度**：根据油门和刹车计算，有加速度和减速度模拟
- **电池**：缓慢下降（模拟耗电）
- **里程**：根据速度累计增加
- **电压**：根据电池电量和负载变化
- **电流**：根据油门和速度计算
- **温度**：根据电流和环境温度模拟

## 故障排查

### 问题：客户端未显示底盘数据

**检查步骤**：

1. **检查 Mosquitto Broker**：
   ```bash
   docker compose ps mosquitto
   docker compose logs mosquitto | tail -20
   ```

2. **检查车端是否发布数据**：
   ```bash
   mosquitto_sub -h localhost -t "vehicle/status" -v
   ```

3. **检查车端日志**：
   ```bash
   docker compose logs vehicle | grep -i "status\|mqtt\|publish"
   ```

4. **检查客户端连接**：
   - 确认客户端已连接到 MQTT Broker
   - 查看客户端日志：`docker compose logs client-dev | grep -i "mqtt\|status"`

5. **手动触发数据发布**（仓库根目录；载荷与 `vehicle_control` schema 对齐）：
   ```bash
   source scripts/lib/mqtt_control_json.sh
   mosquitto_pub -h localhost -p 1883 -t "vehicle/control" -m "$(mqtt_json_start_stream "YOUR_VIN")"
   ```

### 问题：数据更新频率过低

**解决方案**：

1. 检查配置文件 `Vehicle-side/config/vehicle_config.json`
2. 确认 `status_publish_frequency` 设置为期望值（默认 50Hz）
3. 重启车端容器：
   ```bash
   docker compose restart vehicle
   ```

### 问题：某些字段未显示

**检查步骤**：

1. 检查配置文件，确认字段已启用（`enabled: true`）
2. 使用验证脚本检查数据是否包含该字段：
   ```bash
   bash scripts/verify-chassis-data-display.sh | grep -A 20 "数据摘要"
   ```
3. 检查 QML 代码，确认界面已添加该字段的显示

## 扩展新字段

如需添加新的底盘数据字段，请参考：

- `docs/VEHICLE_CHASSIS_DATA_EXTENSION.md` - 扩展指南
- `Vehicle-side/src/chassis_data_generator.cpp` - 数据生成器实现
- `client/src/vehiclestatus.h` - 客户端状态类定义
- `client/qml/ControlPanel.qml` - QML 界面显示

## 相关文档

- `docs/VEHICLE_CHASSIS_DATA_UPLOAD.md` - 底盘数据上传功能说明
- `docs/MQTT_BROKER_DEPLOYMENT.md` - MQTT Broker 部署指南
- `docs/VERIFY_CHASSIS_DATA_FLOW.md` - 数据流验证指南
