# 手动验证底盘数据流功能指南

## 概述

本文档说明如何使用 `bash scripts/start-full-chain.sh manual` 进行手动操作验证底盘数据流上传到客户端的功能。

## 快速开始

### 1. 启动全链路（manual 模式）

```bash
bash scripts/start-full-chain.sh manual
```

该命令将：
- ✅ 启动所有节点（Postgres、Keycloak、Backend、ZLMediaKit、Mosquitto、车端、客户端）
- ✅ 执行逐环体验证（包括 MQTT Broker 和底盘数据流基础验证）
- ✅ 跳过自动连接测试（manual 模式）
- ✅ 启动客户端供手动操作验证

### 2. 在客户端界面操作验证

#### 步骤 1：登录
- 使用测试账号：`123` / `123`
- 或使用其他已注册账号

#### 步骤 2：选择车辆
- 在车辆选择对话框中选择车辆（如 `123456789`）
- 点击「确认」进入主驾驶界面

#### 步骤 3：连接车端
- 在主驾驶界面点击「连接车端」按钮
- 等待约 2.5 秒，系统将：
  - 建立 MQTT 连接
  - 发送 `start_stream` 命令到车端
  - 车端开始发布底盘数据（50Hz）
  - 拉取四路视频流

#### 步骤 4：验证视频流
- 确认四路视频（前方、后方、左侧、右侧）有画面
- 视频应实时更新

#### 步骤 5：验证底盘数据流 ⭐

在右侧控制面板的「📊 车辆状态」区域查看实时更新的底盘数据：

| 数据项 | 显示位置 | 验证要点 |
|--------|----------|----------|
| **速度** | 大号绿色数字 | 数值实时变化（0-200 km/h） |
| **电池** | 百分比 + 进度条 | 电量缓慢下降，进度条颜色变化（>20% 绿色，≤20% 红色） |
| **里程** | 蓝色数字 | 累计里程持续增加（单位：km） |
| **电压** | 绿色数字 | 电压值在合理范围内（约 48V，单位：V） |
| **电流** | 橙色数字 | 电流值根据油门变化（0-200A，单位：A） |
| **温度** | 彩色数字 | 温度值变化，颜色根据温度变化（高温红色，低温蓝色，正常绿色，单位：°C） |

**在主界面中央速度表**：
- 速度表：圆形速度表实时更新，显示当前速度
- 档位：显示当前档位（R/N/D）
- 转向角度：速度表下方显示转向角度（如有转向）

#### 步骤 6：验证数据更新频率
- 底盘数据应以 **50Hz** 频率更新（约每 20ms 更新一次）
- 观察速度、里程等数值是否平滑变化

## 验证检查清单

### ✅ 基础验证
- [ ] 客户端成功连接到 MQTT Broker
- [ ] 车端成功连接到 MQTT Broker
- [ ] 四路视频流正常显示

### ✅ 底盘数据验证
- [ ] 速度数据实时显示并更新
- [ ] 电池电量显示正常（带进度条）
- [ ] 里程数据持续增加
- [ ] 电压数据在合理范围内
- [ ] 电流数据根据操作变化
- [ ] 温度数据显示正常（带颜色提示）
- [ ] 数据更新频率约为 50Hz（平滑更新）

### ✅ 界面显示验证
- [ ] 控制面板「📊 车辆状态」区域显示所有字段
- [ ] 主界面速度表显示速度和档位
- [ ] 数据格式正确（单位、精度）

## 故障排查

### 问题：底盘数据未显示

**检查步骤**：

1. **检查 MQTT 连接**：
   ```bash
   # 查看客户端日志
   docker compose logs client-dev | grep -i "mqtt\|status"
   
   # 查看车端日志
   docker compose logs vehicle | grep -i "mqtt\|status\|publish"
   ```

2. **检查车端是否发布数据**：
   ```bash
   # 使用 mosquitto_sub 订阅 vehicle/status 主题
   docker compose run --rm --no-deps mosquitto mosquitto_sub -h mosquitto -p 1883 -t "vehicle/status" -v
   
   # 或使用宿主机（如果已安装 mosquitto-clients）
   mosquitto_sub -h localhost -p 1883 -t "vehicle/status" -v
   ```

3. **手动触发数据发布**：
   ```bash
   # 发送 start_stream 命令
   docker compose run --rm --no-deps mosquitto mosquitto_pub -h mosquitto -p 1883 \
     -t "vehicle/control" -m '{"type":"start_stream","timestamp":'$(date +%s000)'}'
   ```

4. **检查配置文件**：
   ```bash
   # 查看车端配置文件
   docker compose exec vehicle cat /app/config/vehicle_config.json
   
   # 确认 status_publish_frequency 设置为 50
   # 确认所有字段的 enabled 为 true
   ```

### 问题：数据更新频率过低

**解决方案**：

1. 检查配置文件中的 `status_publish_frequency` 设置
2. 确认车端容器正常运行
3. 重启车端容器：
   ```bash
   docker compose restart vehicle
   ```

### 问题：某些字段未显示

**检查步骤**：

1. 使用验证脚本检查数据是否包含该字段：
   ```bash
   bash scripts/verify-chassis-data-display.sh
   ```

2. 检查配置文件，确认字段已启用（`enabled: true`）

3. 检查客户端代码，确认 QML 界面已添加该字段的显示

## 详细验证脚本

如需更详细的验证，可使用专门的验证脚本：

```bash
# 验证底盘数据流（需要 mosquitto-clients）
bash scripts/verify-chassis-data-display.sh

# 验证 MQTT Broker
bash scripts/verify-mosquitto.sh
```

## 数据流架构

```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│   车端      │ 发布    │  Mosquitto   │ 订阅    │   客户端    │
│             │────────>│   MQTT       │────────>│             │
│ ChassisData │         │   Broker     │         │ QML 界面    │
│ Generator   │         │              │         │             │
│  (50Hz)     │         │  (转发)      │         │  (实时显示) │
└─────────────┘         └──────────────┘         └─────────────┘
     │                        │                        │
     │ vehicle/status        │ vehicle/status        │
     │ (JSON 格式)           │ (JSON 格式)            │
     │                        │                        │
     └────────────────────────┴────────────────────────┘
```

## 相关文档

- `docs/CHASSIS_DATA_DISPLAY.md` - 底盘数据显示功能详细说明
- `docs/VEHICLE_CHASSIS_DATA_UPLOAD.md` - 底盘数据上传功能说明
- `docs/MQTT_BROKER_DEPLOYMENT.md` - MQTT Broker 部署指南

## 总结

使用 `bash scripts/start-full-chain.sh manual` 可以：
1. ✅ 一键启动全链路环境
2. ✅ 自动验证基础功能（包括底盘数据流基础检查）
3. ✅ 启动客户端供手动操作验证
4. ✅ 提供详细的操作步骤和验证检查清单

通过手动操作验证，可以确保底盘数据流功能正常工作，并在客户端主驾驶界面正确显示所有底盘数据字段。
