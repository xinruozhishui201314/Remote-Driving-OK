# 车辆底盘数据上传功能实现

## Executive Summary

**目标**：在客户端主驾驶界面点击连接时，车端不仅推流，还要上传车辆底盘数据到客户端主驾驶界面。

**实现状态**：✅ 已完成

**最新更新**：
- ✅ 上报频率改为50Hz（默认，可通过配置文件修改）
- ✅ 支持配置文件和环境变量灵活调整上报频率
- ✅ 底盘数据结构可灵活扩展，支持字段注册机制

**主要改动**：
1. 车端增强 `MqttHandler::publishStatus()`，添加模拟底盘数据生成
2. 车端在接收到 `start_stream` 指令后启用状态发布
3. 客户端 `VehicleStatus` 类扩展支持更多底盘数据字段
4. 客户端连接时自动请求车端开始推流和上传数据

---

## 1. 背景与目标

### 1.1 需求
- 客户端点击"连接"按钮时，车端开始推流视频
- 同时，车端开始定期上传车辆底盘数据（速度、里程、电压、电流、温度、电池等）
- 客户端主驾驶界面能实时显示这些底盘数据

### 1.2 非目标
- 真实硬件数据采集（当前为模拟数据）
- 数据持久化存储
- 历史数据回放

---

## 2. 方案设计

### 2.1 数据流设计

```
客户端点击"连接"
    ↓
设置 VIN + 连接 MQTT + 连接 WebRTC
    ↓
发送 start_stream 指令到车端（MQTT）
    ↓
车端接收 start_stream
    ↓
启用状态发布标志 + 启动推流脚本
    ↓
车端主循环每 200ms 发布底盘数据
    ↓
客户端订阅 vehicle/status 和 vehicle/<VIN>/status
    ↓
客户端接收并更新 VehicleStatus
    ↓
QML 界面自动刷新显示
```

### 2.2 底盘数据字段（可扩展）

| 字段 | 类型 | 单位 | 说明 | 可配置 |
|------|------|------|------|--------|
| speed | double | km/h | 车辆速度（模拟，根据控制指令变化） | ✅ |
| gear | int | - | 档位（-1:倒档, 0:空档, 1:前进） | ✅ |
| steering | double | - | 转向角度（-1.0 到 1.0） | ✅ |
| throttle | double | - | 油门（0.0 到 1.0） | ✅ |
| brake | double | - | 刹车（0.0 到 1.0） | ✅ |
| battery | double | % | 电池电量（0-100） | ✅ |
| odometer | double | km | 累计里程 | ✅ |
| voltage | double | V | 电池电压 | ✅ |
| current | double | A | 电流 | ✅ |
| temperature | double | °C | 温度 | ✅ |
| vin | string | - | 车辆 VIN（可选） | - |
| timestamp | long | ms | 时间戳 | - |

**扩展性**：通过 `ChassisDataGenerator` 注册机制，可以轻松添加新字段的数据生成器。

---

## 3. 代码变更清单

### 3.1 车端变更

#### 新增文件

**`Vehicle-side/src/vehicle_config.h` 和 `vehicle_config.cpp`**
- ✅ 配置管理类（单例模式）
- ✅ 支持从JSON配置文件加载
- ✅ 支持从环境变量加载（覆盖文件配置）
- ✅ 可配置上报频率（默认50Hz）
- ✅ 可配置字段启用/禁用

**`Vehicle-side/src/chassis_data_generator.h` 和 `chassis_data_generator.cpp`**
- ✅ 可扩展的数据生成器系统
- ✅ 支持字段注册机制
- ✅ 每个字段有独立的生成器函数
- ✅ 方便后续添加新字段

#### `Vehicle-side/src/mqtt_handler.h`
- ✅ 添加 `setStatusPublishingEnabled()` 方法
- ✅ 添加 `setVin()` 方法
- ✅ 集成 `ChassisDataGenerator` 数据生成器
- ✅ 移除旧的 `generateSimulatedChassisData()` 方法

#### `Vehicle-side/src/mqtt_handler.cpp`
- ✅ 在构造函数中初始化数据生成器
- ✅ 在 `processControlCommand()` 中检测 `start_stream` 并启用状态发布
- ✅ 增强 `publishStatus()` 方法：
  - 使用配置系统获取字段列表
  - 使用数据生成器生成数据
  - 只发布启用的字段
  - 发布到 `vehicle/status` 和 `vehicle/<VIN>/status` 两个主题

#### `Vehicle-side/src/main.cpp`
- ✅ 加载配置文件（从文件或环境变量）
- ✅ 使用配置的上报频率（默认50Hz，每20ms）
- ✅ 优化主循环，使用时间戳控制发布频率

### 3.2 客户端变更

#### `client/src/vehiclestatus.h`
- ✅ 添加 Q_PROPERTY：`odometer`, `voltage`, `current`, `temperature`
- ✅ 添加对应的 getter 方法
- ✅ 添加对应的 setter slots
- ✅ 添加对应的 signal
- ✅ 添加对应的私有成员变量

#### `client/src/vehiclestatus.cpp`
- ✅ 实现 `setOdometer()`, `setVoltage()`, `setCurrent()`, `setTemperature()` 方法
- ✅ 在 `updateStatus()` 中解析并更新新增字段

#### `client/qml/ConnectionsDialog.qml`
- ✅ 在连接按钮点击时，延迟调用 `mqttController.requestStreamStart()`

---

## 4. 模拟数据生成逻辑

### 4. JSON 数据格式

底盘数据以标准 JSON 格式发送，确保兼容性：

```json
{
  "timestamp": 1234567890123,
  "vin": "LSGBF53M8DS123456",
  "speed": 25.5,
  "gear": 1,
  "steering": 0.2,
  "throttle": 0.3,
  "brake": 0.0,
  "battery": 95.5,
  "odometer": 1234.56,
  "voltage": 48.2,
  "current": 15.3,
  "temperature": 28.5
}
```

**JSON 格式特点**：
- 使用标准 JSON 格式，兼容所有 JSON 解析器
- 浮点数使用固定精度（6位小数），避免科学计数法
- 字符串字段自动转义特殊字符
- 字段顺序：timestamp → vin → 其他字段（按配置顺序）

### 4.1 速度模拟
- 根据油门指令加速：`speed += throttle * 2.0 * dt`（最大 30 km/h）
- 根据刹车指令减速：`speed -= brake * 5.0 * dt`
- 自然减速：`speed -= 0.5 * dt`

### 4.2 里程模拟
- 速度积分：`odometer += speed * dt / 3600.0`（转换为公里）

### 4.3 电压模拟
- 基础电压：48V
- 油门时下降：-2V
- 高速时下降：-1V
- 范围：42V - 52V

### 4.4 电流模拟
- 根据油门和速度：`current = throttle * 50.0 + speed * 2.0`
- 范围：0A - 100A

### 4.5 温度模拟
- 基础温度：25°C
- 根据电流和速度上升
- 范围：20°C - 60°C

### 4.6 电池模拟
- 缓慢下降：`battery -= dt * 0.001`（每小时约下降 3.6%）

---

## 5. MQTT 主题设计

### 5.1 发布主题
- `vehicle/status`：所有车辆的状态（通用主题）
- `vehicle/<VIN>/status`：特定车辆的状态（如果设置了 VIN）

### 5.2 订阅主题（客户端）
- `vehicle/status`：订阅所有车辆状态
- `vehicle/<VIN>/status`：订阅当前选择的车辆状态

---

## 6. 编译/部署/运行说明

### 6.1 编译车端

```bash
cd Vehicle-side
./build.sh
```

### 6.2 编译客户端

```bash
cd client
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 6.3 运行车端

```bash
cd Vehicle-side
./run.sh mqtt://localhost:1883
```

### 6.4 运行客户端

```bash
cd client/build
./RemoteDrivingClient
```

### 6.5 配置说明

#### 6.5.1 配置文件方式

创建配置文件 `/app/config/vehicle_config.json`（或通过环境变量 `VEHICLE_CONFIG_PATH` 指定路径）：

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
      "name": "gear",
      "enabled": true,
      "type": "int",
      "min_value": -1.0,
      "max_value": 1.0,
      "description": "档位 (-1:倒档, 0:空档, 1:前进)"
    },
    {
      "name": "odometer",
      "enabled": false,
      "type": "double",
      "description": "累计里程 (km)"
    }
  ]
}
```

**配置文件路径优先级**：
1. 环境变量 `VEHICLE_CONFIG_PATH` 指定的路径
2. 默认路径 `/app/config/vehicle_config.json`
3. 如果都不存在，使用默认配置（不报错）

**注意**：当前版本仅支持配置文件方式，不支持环境变量覆盖配置项。

### 6.6 验证步骤

1. **启动 MQTT Broker**（如果未运行）
   ```bash
   # 使用 Docker
   docker run -it -p 1883:1883 eclipse-mosquitto
   ```

2. **启动车端**（使用配置）
   ```bash
   # 方式1：使用自定义配置文件路径
   export VEHICLE_CONFIG_PATH=/path/to/vehicle_config.json
   ./run.sh mqtt://localhost:1883
   
   # 方式2：使用默认配置文件路径（/app/config/vehicle_config.json）
   ./run.sh mqtt://localhost:1883
   ```
   - 车端连接到 MQTT Broker
   - 订阅 `vehicle/control` 主题
   - 日志显示：`[Config] 状态上报频率: 50 Hz (间隔: 20 ms)`

3. **启动客户端**
   - 登录并选择车辆
   - 点击"连接"按钮
   - 观察日志：
     - 客户端发送 `start_stream` 指令
     - 车端接收并启用状态发布
     - 车端开始每 20ms（50Hz）发布底盘数据

4. **验证数据接收**
   - 在客户端主界面查看车辆状态
   - 速度、电池、里程等数据应实时更新（50Hz）
   - 可以通过 MQTT 客户端工具（如 `mosquitto_sub`）订阅主题验证：
     ```bash
     mosquitto_sub -h localhost -t "vehicle/status" -t "vehicle/+/status"
     ```

---

## 7. 验证与回归测试清单

### 7.1 功能测试

- [ ] 客户端点击连接后，车端开始发布底盘数据
- [ ] 底盘数据包含所有必需字段（speed, gear, steering, throttle, brake, battery, odometer, voltage, current, temperature）
- [ ] 数据更新频率约为 5Hz（每 200ms）
- [ ] 客户端能正确接收并解析数据
- [ ] QML 界面能实时显示更新的数据
- [ ] 模拟数据根据控制指令合理变化（速度、电压、电流等）

### 7.2 边界测试

- [ ] 车端未连接 MQTT 时，不发布数据
- [ ] 车端未收到 `start_stream` 时，不发布数据
- [ ] 客户端断开连接后，车端继续发布（可配置是否停止）
- [ ] 数据字段缺失时的容错处理

### 7.3 性能测试

- [ ] MQTT 发布延迟 < 10ms
- [ ] 客户端接收延迟 < 50ms
- [ ] 数据更新不影响控制指令发送

---

## 8. 风险与回滚方案

### 8.1 风险清单

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 模拟数据不准确 | 低 | 后续替换为真实硬件数据 |
| MQTT 发布频率过高 | 中 | 可配置发布间隔 |
| 客户端解析失败 | 中 | 添加字段验证和默认值 |
| 车端未收到 start_stream | 低 | 添加手动启用接口 |

### 8.2 回滚方案

如果需要回滚到之前版本：

1. **车端回滚**：
   ```bash
   git checkout HEAD~1 Vehicle-side/src/mqtt_handler.h Vehicle-side/src/mqtt_handler.cpp
   ```

2. **客户端回滚**：
   ```bash
   git checkout HEAD~1 client/src/vehiclestatus.h client/src/vehiclestatus.cpp client/qml/ConnectionsDialog.qml
   ```

---

## 9. 后续演进路线图

### MVP（当前版本）
- ✅ 模拟底盘数据生成
- ✅ MQTT 发布机制
- ✅ 客户端接收和显示

### V1（短期）
- [ ] 从真实硬件（CAN/ROS2）获取底盘数据
- [ ] 添加数据验证和异常处理
- [ ] 优化发布频率（可配置）
- [ ] 添加数据统计和监控

### V2（中期）
- [ ] 数据持久化存储
- [ ] 历史数据查询和回放
- [ ] 数据分析和告警
- [ ] 多车辆数据聚合

---

## 10. 观测性与运维

### 10.1 日志

**车端日志**：
- `[MQTT] 收到 start_stream，启用底盘数据发布`
- `MQTT 发布状态错误: ...`

**客户端日志**：
- `MQTT connected successfully`
- `Subscribed to topic: vehicle/status`
- `Subscribed to vehicle status topic: vehicle/<VIN>/status`

### 10.2 监控指标

建议添加的 Prometheus 指标：
- `vehicle_status_publish_rate`：状态发布频率
- `vehicle_status_publish_latency`：发布延迟
- `vehicle_status_fields_missing`：缺失字段计数

### 10.3 故障排查 Runbook

**问题**：客户端收不到底盘数据

**排查步骤**：
1. 检查车端是否连接 MQTT：查看车端日志
2. 检查是否收到 `start_stream`：查看车端日志
3. 检查 MQTT 主题订阅：使用 `mosquitto_sub` 验证
4. 检查客户端 MQTT 连接：查看客户端日志
5. 检查数据格式：验证 JSON 格式是否正确

---

## 11. 参考资料

- [项目规范文档](./project_spec.md)
- [MQTT 集成文档](../client/INTEGRATION.md)
- [车辆端 README](../../Vehicle-side/README.md)
