# 底盘数据流日志说明与排查指南

## 概述

本文档说明底盘数据流的日志系统，以及如何使用日志进行问题定位和排查。

## 日志架构

底盘数据流包含三个关键环节，每个环节都有详细的日志记录：

```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│   车端      │ 发布    │  Mosquitto   │ 订阅    │   客户端    │
│             │────────>│   MQTT       │────────>│             │
│ [日志1]     │         │   Broker     │         │ [日志2,3]   │
│ 数据生成    │         │              │         │ 数据接收    │
│ 数据发布    │         │  (转发)      │         │ 状态更新    │
└─────────────┘         └──────────────┘         └─────────────┘
```

## 日志分类

### 1. 车端日志（Vehicle-side）

**日志标签**：`[CHASSIS_DATA]`

**日志位置**：
- 标准输出：`docker compose logs vehicle`
- 或直接查看容器日志：`docker compose exec vehicle cat /proc/1/fd/1`

**关键日志点**：

#### 1.1 发布启用日志
```
[MQTT] 收到 start_stream，启用底盘数据发布
[CHASSIS_DATA] 底盘数据发布已启用，准备开始发布
[CHASSIS_DATA] 开始发布底盘数据，频率: 50 Hz, 间隔: 20 ms
```

#### 1.2 数据发布日志（每50条或每5秒）
```
[CHASSIS_DATA] 发布 #50 | 主题: vehicle/status | 速度: 35.2 km/h | 电池: 85.3% | 里程: 1234.56 km | 档位: D | 实际频率: 49.8 Hz | 数据大小: 245 bytes
[CHASSIS_DATA] 同时发布到: vehicle/123456789/status
```

**日志字段说明**：
- `发布 #N`：累计发布次数
- `主题`：MQTT 主题名称
- `速度/电池/里程/档位`：关键数据字段值
- `实际频率`：实际发布频率（Hz），用于验证是否达到配置的 50Hz
- `数据大小`：JSON 消息大小（bytes）

#### 1.3 错误日志
```
[CHASSIS_DATA] 发布状态错误: <错误信息>
[CHASSIS_DATA] 发布计数: <当前计数>
```

### 2. 客户端接收日志（Client）

**日志标签**：`[CHASSIS_DATA]`

**日志位置**：
- 标准输出：`docker compose logs client-dev`
- 或直接查看容器日志：`docker compose exec client-dev cat /proc/1/fd/1`

**关键日志点**：

#### 2.1 开始接收日志
```
[CHASSIS_DATA] 开始接收底盘数据，主题: vehicle/status
```

#### 2.2 数据接收日志（每50条或每5秒）
```
[CHASSIS_DATA] 接收 #50 | 主题:vehicle/status | 速度:35.2km/h | 电池:85.3% | 里程:1234.56km | 档位:D | 实际频率:49.8Hz | 数据大小:245bytes
```

**日志字段说明**：
- `接收 #N`：累计接收次数
- `主题`：接收到的 MQTT 主题
- `速度/电池/里程/档位`：解析出的关键数据字段值
- `实际频率`：实际接收频率（Hz），用于验证数据流是否正常
- `数据大小`：接收到的消息大小（bytes）

#### 2.3 解析错误日志
```
[CHASSIS_DATA] 解析MQTT消息失败: <错误信息> | 主题: <主题> | 消息大小: <大小>bytes | 错误位置: <位置>
```

### 3. 客户端状态更新日志（Client）

**日志标签**：`[VEHICLE_STATUS]`

**关键日志点**：

#### 3.1 开始更新日志
```
[VEHICLE_STATUS] 开始更新车辆状态
```

#### 3.2 状态更新日志（每50次或每5秒，仅记录有变化的字段）
```
[VEHICLE_STATUS] 更新 #50 | 变化字段: speed:35.2, battery:85.3, odometer:1234.56 | 实际频率:49.8Hz | 当前状态: 速度35.2km/h, 电池85.3%, 里程1234.56km
```

**日志字段说明**：
- `更新 #N`：累计更新次数
- `变化字段`：本次更新中值发生变化的字段及其新值
- `实际频率`：实际更新频率（Hz）
- `当前状态`：所有关键字段的当前值

## 日志频率控制

为了避免日志过多影响性能，日志采用**智能频率控制**：

1. **每 N 条记录一次**：默认每 50 条记录一次
2. **时间间隔控制**：如果超过 5 秒未记录，强制记录一次
3. **首次记录**：第一条消息/更新总是记录

这样可以：
- ✅ 减少日志量，避免性能影响
- ✅ 保持关键信息可见
- ✅ 及时发现异常（长时间无日志可能表示数据流中断）

## 排查场景

### 场景 1：客户端未显示底盘数据

**排查步骤**：

1. **检查车端是否发布数据**：
   ```bash
   docker compose logs vehicle | grep -i "CHASSIS_DATA"
   ```
   
   期望看到：
   ```
   [CHASSIS_DATA] 底盘数据发布已启用，准备开始发布
   [CHASSIS_DATA] 开始发布底盘数据，频率: 50 Hz, 间隔: 20 ms
   [CHASSIS_DATA] 发布 #50 | ...
   ```
   
   如果没有看到这些日志：
   - 检查是否发送了 `start_stream` 命令
   - 检查车端是否连接到 MQTT Broker
   - 查看车端错误日志

2. **检查客户端是否接收数据**：
   ```bash
   docker compose logs client-dev | grep -i "CHASSIS_DATA"
   ```
   
   期望看到：
   ```
   [CHASSIS_DATA] 开始接收底盘数据，主题: vehicle/status
   [CHASSIS_DATA] 接收 #50 | ...
   ```
   
   如果没有看到这些日志：
   - 检查客户端是否连接到 MQTT Broker
   - 检查客户端是否订阅了 `vehicle/status` 主题
   - 查看 MQTT 连接日志

3. **检查状态更新**：
   ```bash
   docker compose logs client-dev | grep -i "VEHICLE_STATUS"
   ```
   
   期望看到：
   ```
   [VEHICLE_STATUS] 开始更新车辆状态
   [VEHICLE_STATUS] 更新 #50 | ...
   ```
   
   如果没有看到这些日志：
   - 检查 JSON 解析是否成功
   - 检查 `VehicleStatus::updateStatus()` 是否被调用

### 场景 2：数据更新频率过低

**排查步骤**：

1. **检查车端实际发布频率**：
   ```bash
   docker compose logs vehicle | grep "CHASSIS_DATA.*发布" | tail -5
   ```
   
   查看日志中的 `实际频率` 字段，应该接近配置的 50Hz。
   
   如果频率过低：
   - 检查配置文件中的 `status_publish_frequency` 设置
   - 检查车端主循环是否正常执行
   - 检查是否有阻塞操作

2. **检查客户端实际接收频率**：
   ```bash
   docker compose logs client-dev | grep "CHASSIS_DATA.*接收" | tail -5
   ```
   
   查看日志中的 `实际频率` 字段，应该接近车端发布频率。
   
   如果频率过低：
   - 检查网络延迟
   - 检查 MQTT Broker 性能
   - 检查客户端消息处理是否阻塞

3. **检查状态更新频率**：
   ```bash
   docker compose logs client-dev | grep "VEHICLE_STATUS.*更新" | tail -5
   ```
   
   查看日志中的 `实际频率` 字段，应该接近接收频率。

### 场景 3：某些字段未更新

**排查步骤**：

1. **检查车端是否生成该字段**：
   ```bash
   docker compose logs vehicle | grep "CHASSIS_DATA.*发布" | grep -i "odometer\|voltage\|current\|temperature"
   ```
   
   查看日志中是否包含该字段的值。

2. **检查客户端是否接收到该字段**：
   ```bash
   docker compose logs client-dev | grep "CHASSIS_DATA.*接收" | grep -i "odometer\|voltage\|current\|temperature"
   ```

3. **检查状态更新是否包含该字段**：
   ```bash
   docker compose logs client-dev | grep "VEHICLE_STATUS.*更新" | grep -i "odometer\|voltage\|current\|temperature"
   ```
   
   查看 `变化字段` 中是否包含该字段。

4. **检查配置文件**：
   ```bash
   docker compose exec vehicle cat /app/config/vehicle_config.json | grep -A 5 "odometer\|voltage\|current\|temperature"
   ```
   
   确认该字段的 `enabled` 为 `true`。

### 场景 4：数据值异常

**排查步骤**：

1. **对比车端和客户端日志**：
   ```bash
   # 车端日志
   docker compose logs vehicle | grep "CHASSIS_DATA.*发布" | tail -1
   
   # 客户端接收日志
   docker compose logs client-dev | grep "CHASSIS_DATA.*接收" | tail -1
   
   # 状态更新日志
   docker compose logs client-dev | grep "VEHICLE_STATUS.*更新" | tail -1
   ```
   
   对比三个环节的数据值，找出数据在哪个环节发生变化。

2. **检查 JSON 解析**：
   ```bash
   docker compose logs client-dev | grep "解析MQTT消息失败"
   ```
   
   如果有解析错误，检查消息格式是否正确。

### 场景 5：数据流中断

**排查步骤**：

1. **检查最后一条日志时间**：
   ```bash
   # 车端最后发布
   docker compose logs vehicle | grep "CHASSIS_DATA.*发布" | tail -1
   
   # 客户端最后接收
   docker compose logs client-dev | grep "CHASSIS_DATA.*接收" | tail -1
   ```
   
   如果超过 5 秒没有新日志，可能表示数据流中断。

2. **检查 MQTT 连接状态**：
   ```bash
   # 车端连接状态
   docker compose logs vehicle | grep -i "mqtt.*连接\|mqtt.*断开"
   
   # 客户端连接状态
   docker compose logs client-dev | grep -i "mqtt.*连接\|mqtt.*断开"
   ```

3. **检查 MQTT Broker**：
   ```bash
   docker compose logs mosquitto | tail -20
   ```

## 日志分析脚本

### 统计发布/接收频率

```bash
# 统计车端发布频率
docker compose logs vehicle | grep "CHASSIS_DATA.*发布" | \
  awk -F'实际频率:' '{print $2}' | awk '{print $1}' | \
  awk '{sum+=$1; count++} END {if(count>0) print "平均频率:", sum/count, "Hz"}'

# 统计客户端接收频率
docker compose logs client-dev | grep "CHASSIS_DATA.*接收" | \
  awk -F'实际频率:' '{print $2}' | awk '{print $1}' | \
  awk '{sum+=$1; count++} END {if(count>0) print "平均频率:", sum/count, "Hz"}'
```

### 检查数据流连续性

```bash
# 检查车端发布间隔（应该约20ms）
docker compose logs vehicle | grep "CHASSIS_DATA.*发布" | \
  tail -10 | awk '{print $1, $2}' | \
  while read time1 date1; do
    read time2 date2
    # 计算时间差（需要根据实际日志格式调整）
  done
```

### 提取关键数据值

```bash
# 提取最新的速度值
docker compose logs client-dev | grep "VEHICLE_STATUS.*更新" | \
  tail -1 | grep -oP '速度[0-9.]+' | grep -oP '[0-9.]+'

# 提取最新的电池值
docker compose logs client-dev | grep "VEHICLE_STATUS.*更新" | \
  tail -1 | grep -oP '电池[0-9.]+' | grep -oP '[0-9.]+'
```

## 最佳实践

1. **定期检查日志**：在测试和调试时，定期查看日志确保数据流正常
2. **对比频率**：对比车端发布频率和客户端接收频率，确保数据没有丢失
3. **监控错误**：关注错误日志，及时处理解析错误、连接错误等问题
4. **性能优化**：如果日志过多影响性能，可以调整日志频率（修改代码中的 `50` 和 `5000` 参数）

## 相关文档

- `docs/CHASSIS_DATA_DISPLAY.md` - 底盘数据显示功能说明
- `docs/VERIFY_CHASSIS_DATA_MANUAL.md` - 手动验证指南
- `docs/MQTT_BROKER_DEPLOYMENT.md` - MQTT Broker 部署指南
