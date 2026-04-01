# 车辆底盘数据流验证指南

## 概述

本文档提供完整的车辆底盘数据流验证步骤，包括日志分析和问题排查。

## 快速验证

### 方法1：使用验证脚本（推荐）

```bash
# 完整验证（包括所有环节）
bash scripts/verify-chassis-data-complete.sh

# 日志分析验证
bash scripts/verify-chassis-data-flow-logs.sh
```

### 方法2：手动验证

#### 步骤1：启动全链路

```bash
bash scripts/start-full-chain.sh manual
```

#### 步骤2：在客户端操作

1. 登录（123 / 123）
2. 选择车辆（123456789）
3. **点击「连接车端」按钮**（重要！这会触发 start_stream 命令）

#### 步骤3：验证数据流

```bash
# 检查车端是否发布数据
docker logs remote-driving-vehicle-1 | grep "CHASSIS_DATA.*发布" | tail -10

# 检查客户端是否接收数据
docker logs teleop-client-dev | grep "CHASSIS_DATA.*接收\|VEHICLE_STATUS.*更新" | tail -10
```

## 详细验证步骤

### 1. 检查车端MQTT连接

```bash
docker logs remote-driving-vehicle-1 | grep -i "连接 MQTT\|MQTT.*连接\|已订阅"
```

**期望输出**：
```
连接 MQTT Broker: mosquitto:1883
已订阅主题: vehicle/control
```

### 2. 检查车端是否收到start_stream

```bash
docker logs remote-driving-vehicle-1 | grep -i "start_stream.*启用\|收到.*start_stream"
```

**期望输出**：
```
[MQTT] 收到 start_stream，启用底盘数据发布
[CHASSIS_DATA] 底盘数据发布已启用，准备开始发布
```

### 3. 检查车端是否发布数据

```bash
docker logs remote-driving-vehicle-1 | grep "CHASSIS_DATA.*发布" | tail -5
```

**期望输出**（每50条或每5秒一条）：
```
[CHASSIS_DATA] 发布 #50 | 主题: vehicle/status | 速度: 35.2 km/h | 电池: 85.3% | 里程: 1234.56 km | 档位: D | 实际频率: 49.8 Hz | 数据大小: 245 bytes
[CHASSIS_DATA] ✓ 已发布到主题: vehicle/status
```

**如果没有输出**：
- 检查是否收到 start_stream：`docker logs remote-driving-vehicle-1 | grep start_stream`
- 重启车端容器：`docker compose restart vehicle`
- 重新编译车端代码（如果修改了代码）

### 4. 检查客户端MQTT连接

```bash
docker logs teleop-client-dev | grep -i "mqtt.*连接\|MQTT connected\|Connecting to"
```

**期望输出**：
```
Connecting to MQTT broker: "mqtt://mosquitto:1883"
[MQTT] 连接成功，开始订阅状态主题
```

### 5. 检查客户端是否订阅了vehicle/status

```bash
docker logs teleop-client-dev | grep -i "subscribe\|订阅\|Subscribed"
```

**期望输出**：
```
[MQTT] 正在订阅主题: vehicle/status
[MQTT] ✓ 已订阅主题: vehicle/status (QoS 1)
[MQTT] 订阅完成，等待接收底盘数据...
```

### 6. 检查客户端是否接收数据

```bash
docker logs teleop-client-dev | grep "CHASSIS_DATA.*接收" | tail -5
```

**期望输出**（每50条或每5秒一条）：
```
[CHASSIS_DATA] 接收 #50 | 主题:vehicle/status | 速度:35.2km/h | 电池:85.3% | 里程:1234.56km | 档位:D | 实际频率:49.8Hz | 数据大小:245bytes
```

### 7. 检查客户端状态更新

```bash
docker logs teleop-client-dev | grep "VEHICLE_STATUS.*更新" | tail -5
```

**期望输出**（每50次或每5秒一条，仅记录有变化的字段）：
```
[VEHICLE_STATUS] 更新 #50 | 变化字段: speed:35.2, battery:85.3, odometer:1234.56 | 实际频率:49.8Hz | 当前状态: 速度35.2km/h, 电池85.3%, 里程1234.56km
```

## 常见问题排查

### 问题1：车端未发布数据

**症状**：没有看到 `[CHASSIS_DATA] 发布 #` 日志

**排查步骤**：

1. **检查是否收到start_stream**：
   ```bash
   docker logs remote-driving-vehicle-1 | grep "start_stream.*启用"
   ```

2. **检查发布是否被禁用**：
   ```bash
   docker logs remote-driving-vehicle-1 | grep "状态发布未启用"
   ```
   如果看到此日志，说明 `m_statusPublishingEnabled` 为 false

3. **检查MQTT连接状态**：
   ```bash
   docker logs remote-driving-vehicle-1 | grep "MQTT.*未连接\|发布失败"
   ```

4. **重启车端容器**（如果修改了代码）：
   ```bash
   docker compose restart vehicle
   ```

5. **重新发送start_stream**：
   在客户端再次点击「连接车端」按钮

### 问题2：客户端未接收数据

**症状**：没有看到 `[CHASSIS_DATA] 接收 #` 日志

**排查步骤**：

1. **检查MQTT连接**：
   ```bash
   docker logs teleop-client-dev | grep -i "mqtt.*连接\|MQTT connected"
   ```

2. **检查订阅状态**：
   ```bash
   docker logs teleop-client-dev | grep -i "subscribe\|订阅"
   ```

3. **检查消息回调**：
   ```bash
   docker logs teleop-client-dev | grep "消息回调\|message.*callback"
   ```

4. **检查MQTT Broker**：
   ```bash
   docker compose logs mosquitto | tail -20
   ```

5. **手动测试订阅**：
   ```bash
   # 使用mosquitto_sub测试是否能接收到数据
   docker compose run --rm --no-deps mosquitto mosquitto_sub -h mosquitto -p 1883 -t "vehicle/status" -v
   ```

### 问题3：数据更新频率过低

**症状**：日志中的实际频率远低于50Hz

**排查步骤**：

1. **检查配置频率**：
   ```bash
   docker logs remote-driving-vehicle-1 | grep "状态上报频率"
   ```
   应该显示：`[Config] 状态上报频率: 50 Hz (间隔: 20 ms)`

2. **检查实际发布频率**：
   ```bash
   docker logs remote-driving-vehicle-1 | grep "CHASSIS_DATA.*发布" | tail -5 | grep -oP '实际频率: [0-9.]+'
   ```

3. **检查主循环是否阻塞**：
   查看车端日志是否有长时间无输出的情况

### 问题4：某些字段未显示

**症状**：客户端界面缺少某些字段（如里程、电压、电流、温度）

**排查步骤**：

1. **检查车端是否生成该字段**：
   ```bash
   docker logs remote-driving-vehicle-1 | grep "CHASSIS_DATA.*发布" | grep -i "odometer\|voltage\|current\|temperature"
   ```

2. **检查客户端是否接收到该字段**：
   ```bash
   docker logs teleop-client-dev | grep "CHASSIS_DATA.*接收" | grep -i "odometer\|voltage\|current\|temperature"
   ```

3. **检查配置文件**：
   ```bash
   docker compose exec vehicle cat /app/config/vehicle_config.json | grep -A 5 "odometer\|voltage\|current\|temperature"
   ```
   确认 `enabled: true`

## 日志频率说明

为了避免日志过多，日志采用智能频率控制：

- **每50条记录一次**：默认每50条消息/更新记录一次
- **时间间隔控制**：如果超过5秒未记录，强制记录一次
- **首次记录**：第一条消息/更新总是记录

这样可以：
- ✅ 减少日志量
- ✅ 保持关键信息可见
- ✅ 及时发现异常（长时间无日志可能表示数据流中断）

## 完整日志查看命令

```bash
# 车端完整日志（底盘数据相关）
docker logs remote-driving-vehicle-1 | grep -i "CHASSIS_DATA\|mqtt\|status\|start_stream"

# 客户端完整日志（底盘数据相关）
docker logs teleop-client-dev | grep -i "CHASSIS_DATA\|VEHICLE_STATUS\|mqtt\|status\|subscribe"

# 实时监控日志
docker compose logs -f vehicle client-dev | grep -i "CHASSIS_DATA\|VEHICLE_STATUS"
```

## 验证检查清单

### ✅ 车端检查
- [ ] MQTT Broker 连接成功
- [ ] 订阅了 `vehicle/control` 主题
- [ ] 收到 `start_stream` 命令
- [ ] 启用了底盘数据发布
- [ ] 正在发布数据到 `vehicle/status`（50Hz）
- [ ] 发布日志显示实际频率接近50Hz

### ✅ 客户端检查
- [ ] MQTT Broker 连接成功
- [ ] 订阅了 `vehicle/status` 主题
- [ ] 正在接收底盘数据
- [ ] 正在更新车辆状态
- [ ] 界面显示所有字段（速度、电池、里程、电压、电流、温度）

## 相关文档

- `docs/CHASSIS_DATA_DISPLAY.md` - 底盘数据显示功能说明
- `docs/CHASSIS_DATA_LOGGING.md` - 日志系统详细说明
- `docs/VERIFY_CHASSIS_DATA_MANUAL.md` - 手动验证指南
