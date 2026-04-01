# 车端底盘数据流验证指南

## 概述

本文档说明如何验证车端底盘数据是否正确传输到客户端主驾驶界面。

## 当前架构

### 数据流路径

```
车端 (Vehicle-side)
    ↓ (MQTT发布，50Hz)
MQTT Broker
    ↓ (MQTT订阅)
客户端 (Client)
    ↓ (信号连接)
VehicleStatus (QML属性)
    ↓ (自动绑定)
主驾驶界面显示
```

**注意**：当前实现中，底盘数据通过 **MQTT** 直接传输，**不经过 ZLMediaKit**。ZLMediaKit 主要用于视频流传输。

## 验证步骤

### 1. 准备环境

#### 1.1 启动MQTT Broker

```bash
# 使用Docker
docker run -it -p 1883:1883 eclipse-mosquitto

# 或使用本地安装的mosquitto
mosquitto -c /etc/mosquitto/mosquitto.conf
```

#### 1.2 启动ZLMediaKit（可选，用于视频流）

```bash
cd media/ZLMediaKit
docker compose up -d
```

### 2. 验证MQTT数据流

#### 2.1 使用验证脚本

```bash
# 运行验证脚本
bash scripts/verify-chassis-data-flow.sh mqtt://localhost:1883 http://localhost:8080

# 脚本会：
# 1. 检查依赖（mosquitto_sub, jq等）
# 2. 检查MQTT Broker连接
# 3. 启动MQTT监听
# 4. 模拟发布测试数据
# 5. 验证数据格式
```

#### 2.2 手动验证

**步骤1：订阅MQTT主题**

```bash
# 终端1：订阅所有车辆状态
mosquitto_sub -h localhost -t "vehicle/status" -t "vehicle/+/status" -v

# 或使用jq格式化输出
mosquitto_sub -h localhost -t "vehicle/status" | jq .
```

**步骤2：启动车端**

```bash
cd Vehicle-side

# 确保配置文件存在
cp config/vehicle_config.json.example /app/config/vehicle_config.json

# 启动车端
./run.sh mqtt://localhost:1883
```

**步骤3：发送start_stream指令**

```bash
# 终端2：发送start_stream指令（模拟客户端连接）
mosquitto_pub -h localhost -t "vehicle/control" -m '{
  "type": "start_stream",
  "vin": "123456789",
  "timestamp": '$(date +%s000)'
}'
```

**步骤4：观察数据**

在终端1中应该看到类似以下的数据：

```json
{
  "timestamp": 1234567890123,
  "vin": "123456789",
  "speed": 25.500000,
  "gear": 1,
  "steering": 0.200000,
  "throttle": 0.300000,
  "brake": 0.000000,
  "battery": 95.500000,
  "odometer": 1234.560000,
  "voltage": 48.200000,
  "current": 15.300000,
  "temperature": 28.500000
}
```

### 3. 验证客户端接收

#### 3.1 启动客户端

```bash
cd client/build
./RemoteDrivingClient
```

#### 3.2 连接流程

1. **登录**：使用测试账号 `123/123`
2. **选择车辆**：选择VIN `123456789`
3. **点击连接**：
   - 设置MQTT Broker: `mqtt://localhost:1883`
   - 点击"连接"按钮

#### 3.3 观察主驾驶界面

在主驾驶界面应该看到：

- **状态栏**：
  - 连接状态：已连接（绿色）
  - 速度：实时更新（如 25.5 km/h）
  - 电池：实时更新（如 95%）

- **控制面板**：
  - 速度显示：实时更新
  - 电池电量：实时更新
  - 档位：实时更新（D/N/R）

### 4. 验证数据更新频率

#### 4.1 检查更新频率

车端默认以 **50Hz**（每20ms）发布数据。可以通过以下方式验证：

```bash
# 统计消息频率
mosquitto_sub -h localhost -t "vehicle/status" | \
  while read line; do \
    echo "$(date +%s.%N) $line"; \
  done | \
  awk 'NR>1 {print $1-prev, $0} {prev=$1}' | \
  head -20
```

应该看到时间间隔约为 **0.02秒**（20ms）。

#### 4.2 验证数据连续性

```bash
# 检查数据是否连续
mosquitto_sub -h localhost -t "vehicle/status" | \
  jq -r '.timestamp' | \
  awk 'NR>1 {diff=$1-prev; if(diff>100 || diff<10) print "异常间隔:", diff, "ms"} {prev=$1}' | \
  head -10
```

正常情况下，时间戳间隔应该在 **10-100ms** 之间。

### 5. 验证数据字段完整性

#### 5.1 检查必需字段

```bash
mosquitto_sub -h localhost -t "vehicle/status" | \
  jq '{
    has_timestamp: has("timestamp"),
    has_vin: has("vin"),
    has_speed: has("speed"),
    has_gear: has("gear"),
    has_battery: has("battery"),
    has_odometer: has("odometer"),
    has_voltage: has("voltage"),
    has_current: has("current"),
    has_temperature: has("temperature")
  }'
```

所有字段应该返回 `true`。

#### 5.2 验证数据范围

```bash
mosquitto_sub -h localhost -t "vehicle/status" | \
  jq '{
    speed: .speed | (0 <= . and . <= 200),
    gear: .gear | (-1 <= . and . <= 1),
    battery: .battery | (0 <= . and . <= 100),
    voltage: .voltage | (0 <= . and . <= 100),
    current: .current | (0 <= . and . <= 200),
    temperature: .temperature | (-40 <= . and . <= 100)
  }'
```

所有范围检查应该返回 `true`。

## 故障排查

### 问题1：客户端收不到数据

**可能原因**：
1. MQTT未连接
2. 未订阅正确的主题
3. 车端未启用状态发布

**排查步骤**：
1. 检查客户端日志：查看是否有 "MQTT connected successfully"
2. 检查订阅：查看是否有 "Subscribed to topic: vehicle/status"
3. 检查车端日志：查看是否有 "收到 start_stream，启用底盘数据发布"

### 问题2：数据更新频率不对

**可能原因**：
1. 配置文件未加载
2. 上报频率配置错误

**排查步骤**：
1. 检查车端日志：查看是否有 "[Config] 状态上报频率: 50 Hz"
2. 检查配置文件：`/app/config/vehicle_config.json`
3. 验证配置：`cat /app/config/vehicle_config.json | jq .status_publish_frequency`

### 问题3：数据格式错误

**可能原因**：
1. JSON构建错误
2. 字段缺失

**排查步骤**：
1. 使用 `jq` 验证JSON格式
2. 检查车端日志：查看是否有 "MQTT 发布状态错误"
3. 手动发布测试数据验证格式

## 通过ZLMediaKit转发（未来实现）

### 当前状态

当前实现中，底盘数据**不经过ZLMediaKit**，直接通过MQTT传输。

### 未来实现方案

如果需要通过ZLMediaKit转发底盘数据，可以考虑：

1. **WebRTC DataChannel**：
   - 车端通过WebRTC DataChannel发送底盘数据
   - ZLMediaKit转发DataChannel消息
   - 客户端通过WebRTC DataChannel接收

2. **WebSocket转发**：
   - 车端通过WebSocket发送底盘数据到ZLMediaKit
   - ZLMediaKit转发到客户端
   - 客户端通过WebSocket接收

### 实现建议

参考文档：
- `docs/M1_GATE_X_CONTROL_VIA_ZLM.md` - 控制通道设计
- `Vehicle-side/src/zlm_control_channel.h` - ZLM控制通道占位类

## 验证清单

- [ ] MQTT Broker运行正常
- [ ] 车端可以连接到MQTT Broker
- [ ] 车端可以发布底盘数据到 `vehicle/status`
- [ ] 客户端可以连接到MQTT Broker
- [ ] 客户端可以订阅 `vehicle/status` 主题
- [ ] 客户端可以解析JSON数据
- [ ] VehicleStatus可以更新数据
- [ ] 主驾驶界面可以显示更新的数据
- [ ] 数据更新频率符合配置（默认50Hz）
- [ ] 所有必需字段都存在
- [ ] 数据值在合理范围内

## 相关文档

- `docs/VEHICLE_CHASSIS_DATA_UPLOAD.md` - 底盘数据上传功能实现
- `docs/VEHICLE_CHASSIS_DATA_EXTENSION.md` - 底盘数据扩展指南
- `Vehicle-side/config/vehicle_config.json.example` - 配置文件示例
