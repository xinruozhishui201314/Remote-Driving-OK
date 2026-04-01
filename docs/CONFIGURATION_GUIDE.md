# 远程驾驶系统配置指南

## 概述

本文档说明如何使用远程驾驶系统的配置文件来高效管理项目配置，避免硬编码，提高可维护性和部署灵活性。

## 配置文件架构

### 配置文件位置

```
Remote-Driving/
├── config/
│   ├── backend_config.yaml      # Backend 服务配置
│   ├── client_config.yaml       # Client 客户端配置
│   └── vehicle_config.yaml     # Vehicle-side 车端配置
```

### 配置优先级

1. **环境变量**（最高优先级）
2. **配置文件**（YAML 文件）
3. **代码默认值**（最低优先级）

## 配置文件说明

### 1. Backend 配置文件 (`backend_config.yaml`)

#### 服务器配置
- `server.port`: HTTP 服务监听端口（默认：8080）
- `server.host`: 监听地址（默认：0.0.0.0）

#### Keycloak 认证配置
- `keycloak.url`: Keycloak 服务器地址（环境变量：KEYCLOAK_URL）
- `keycloak.realm`: Realm 名称（环境变量：KEYCLOAK_REALM）
- `keycloak.client_id`: 客户端 ID（环境变量：KEYCLOAK_CLIENT_ID）
- `keycloak.client_secret`: 客户端密钥（环境变量：KEYCLOAK_CLIENT_SECRET）

#### 数据库配置
- `database.url`: PostgreSQL 连接字符串（环境变量：DATABASE_URL）
- `database.connection_timeout`: 连接超时（秒）
- `database.query_timeout`: 查询超时（秒）

#### ZLMediaKit 配置
- `zlm.api_url`: ZLM API 地址（环境变量：ZLM_API_URL）
- `zlm.public_base`: ZLM 公网访问地址（环境变量：ZLM_PUBLIC_BASE）
- `zlm.app`: 应用名称（默认：teleop）

#### MQTT 配置
- `mqtt.broker_url`: MQTT Broker 地址（环境变量：MQTT_BROKER_URL）
- `mqtt.client_id`: 客户端 ID
- `mqtt.qos`: QoS 级别（0, 1, 2）
- `mqtt.keep_alive`: Keep-Alive 间隔（秒）

#### 会话管理配置
- `session.ttl`: 会话 TTL（秒，默认：1800）
- `session.lock_ttl`: 锁 TTL（秒，默认：1800）
- `session.control_secret_length`: 控制密钥长度（字节，默认：32）

#### 日志配置
- `logging.level`: 日志级别
- `logging.format`: 日志格式（json/text）
- `logging.output`: 日志输出（stdout/file/both）
- `logging.file_path`: 日志文件路径

### 2. Client 配置文件 (`client_config.yaml`)

#### 应用配置
- `app.name`: 应用名称
- `app.version`: 应用版本
- `app.organization`: 组织名称

#### 日志配置
- `logging.file_path`: 日志文件路径（环境变量：CLIENT_LOG_FILE）
- `logging.level`: 日志级别
- `logging.console_enabled`: 启用控制台输出

#### 认证配置
- `auth.backend_url`: 后端服务器地址（环境变量：BACKEND_URL 或 REMOTE_DRIVING_SERVER）
- `auth.default_server_url`: 默认服务器地址（环境变量：DEFAULT_SERVER_URL）

#### MQTT 配置
- `mqtt.broker_url`: MQTT Broker 地址（环境变量：MQTT_BROKER_URL）
- `mqtt.client_id`: 客户端 ID
- `mqtt.qos`: QoS 级别

#### 控制通道配置
- `control_channel.preferred_channel`: 首选通道类型（环境变量：CONTROL_CHANNEL_PREFERRED）
  - `auto`: 自动选择
  - `data_channel`: 强制 DataChannel
  - `mqtt`: 强制 MQTT
  - `websocket`: 强制 WebSocket

#### WebRTC 配置
- `webrtc.codec_preference`: 视频编解码器优先级
- `webrtc.video_resolution`: 视频分辨率（默认：1280x720）
- `webrtc.video_fps`: 视频帧率（默认：30）
- `webrtc.stun_servers`: STUN 服务器列表
- `webrtc.turn_servers`: TURN 服务器配置

#### 界面配置
- `ui.fonts.chinese`: 中文字体列表
- `ui.fonts.default`: 默认字体
- `ui.quick_style`: QML 样式

### 3. Vehicle-side 配置文件 (`vehicle_config.yaml`)

#### 车辆标识
- `vehicle.vin`: 车辆 VIN（环境变量：VEHICLE_VIN）
- `vehicle.model`: 车辆型号
- `vehicle.id`: 车辆唯一标识

#### MQTT 配置
- `mqtt.broker_url`: MQTT Broker 地址（环境变量：MQTT_BROKER_URL）
- `mqtt.client_id`: 客户端 ID
- `mqtt.qos`: QoS 级别
- `mqtt.reconnect_interval`: 重连间隔（秒）

#### ZLMediaKit 配置
- `zlm.host`: ZLM 主机地址（环境变量：ZLM_HOST）
- `zlm.rtmp_port`: ZLM RTMP 端口（环境变量：ZLM_RTMP_PORT）
- `zlm.app`: ZLM 应用名称（环境变量：ZLM_APP）

#### 推流配置
- `streaming.script_path`: 推流脚本路径（环境变量：VEHICLE_PUSH_SCRIPT）
- `streaming.pidfile_dir`: PID 文件目录（环境变量：PIDFILE_DIR）
- `streaming.health_check_enabled`: 启用推流健康检查
- `streaming.health_check_interval`: 健康检查间隔（秒）

#### 状态发布配置
- `status_publish.frequency`: 发布频率（Hz，默认：50）
- `status_publish.chassis_data_fields`: 启用的底盘数据字段

#### 控制协议配置
- `control_protocol.schema_version`: 消息版本号
- `control_protocol.timestamp_window_ms`: 时间戳窗口（毫秒，默认：2000）
- `control_protocol.enable_seq_validation`: 启用序列号验证
- `control_protocol.enable_timestamp_validation`: 启用时间戳验证

#### 安全配置
- `safety.watchdog_timeout_ms`: 看门狗超时（毫秒，默认：500）
- `safety.severe_degradation_rtt_ms`: 严重降级 RTT 阈值（毫秒，默认：300）
- `safety.light_degradation_rtt_ms`: 轻度降级 RTT 阈值（毫秒，默认：150）
- `safety.light_degradation_throttle_limit`: 轻度降级油门限制（0.0-1.0，默认：0.2）

#### 网络配置
- `network.dual_nic_enabled`: 启用双网卡冗余
- `network.primary_nic`: 主网卡
- `network.secondary_nic`: 备用网卡
- `network.switch_rtt_threshold`: 链路切换 RTT 阈值（毫秒，默认：200）
- `network.switch_packet_loss_threshold`: 链路切换丢包阈值（百分比，默认：10）

#### 日志配置
- `logging.level`: 日志级别
- `logging.format`: 日志格式
- `logging.output`: 日志输出

#### CARLA 仿真配置（可选）
- `carla.enabled`: 启用 CARLA 仿真（环境变量：ENABLE_CARLA）
- `carla.host`: CARLA 主机地址（环境变量：CARLA_HOST）
- `carla.port`: CARLA 端口（环境变量：CARLA_PORT）
- `carla.map`: 地图名称（环境变量：CARLA_MAP）
- `carla.show_window`: 显示仿真窗口（环境变量：CARLA_SHOW_WINDOW）

#### ROS2 配置（可选）
- `ros2.enabled`: 启用 ROS2（环境变量：ENABLE_ROS2）
- `ros2.namespace`: ROS 命名空间
- `ros2.control_topic`: 控制命令话题
- `ros2.status_topic`: 状态话题

## 使用方法

### 1. 环境变量覆盖配置

在启动服务时，可以通过环境变量覆盖配置文件中的任何值：

```bash
# Backend
export PORT=9090
export KEYCLOAK_URL=http://keycloak:8080
export DATABASE_URL=postgresql://user:pass@localhost:5432/teleop
./backend

# Client
export MQTT_BROKER_URL=mqtt://mqtt-server:1883
export CLIENT_LOG_FILE=/var/log/client.log
./client

# Vehicle-side
export VEHICLE_VIN=VIN123456
export ZLM_HOST=zlmediakit
export MQTT_BROKER_URL=mqtt://mqtt-server:1883
./vehicle
```

### 2. Docker Compose 环境变量

在 `docker-compose.yml` 中定义环境变量：

```yaml
services:
  backend:
    environment:
      - PORT=8080
      - KEYCLOAK_URL=http://keycloak:8080
      - DATABASE_URL=postgresql://teleop:password@postgres:5432/teleop
    volumes:
      # 配置文件可以从根目录 config/ 或模块 config/ 挂载
      - ./backend/config/backend_config.yaml:/app/config/backend_config.yaml:ro

  client:
    environment:
      - MQTT_BROKER_URL=mqtt://teleop-mosquitto:1883
      - CLIENT_LOG_FILE=/tmp/remote-driving-client.log
    volumes:
      - ./client/config/client_config.yaml:/app/config/client_config.yaml:ro

  vehicle:
    environment:
      - VEHICLE_VIN=VIN123456
      - ZLM_HOST=zlmediakit
      - MQTT_BROKER_URL=mqtt://teleop-mosquitto:1883
    volumes:
      - ./Vehicle-side/config/vehicle_config.yaml:/app/config/vehicle_config.yaml:ro
```

### 3. 配置文件挂载

将配置文件挂载到容器内：

**方式一：从模块config目录挂载（推荐，独立部署时）**

```bash
# Backend（独立部署）
docker run -v $(pwd)/backend/config/backend_config.yaml:/app/config/backend_config.yaml:ro backend

# Client（独立部署）
docker run -v $(pwd)/client/config/client_config.yaml:/app/config/client_config.yaml:ro client

# Vehicle-side（独立部署）
docker run -v $(pwd)/Vehicle-side/config/vehicle_config.yaml:/app/config/vehicle_config.yaml:ro vehicle
```

**方式二：从根目录config/挂载（完整链路部署时）**

为了方便完整链路部署，可以在根目录保留配置文件的符号链接或副本：

```bash
# 创建符号链接
ln -s ../backend/config/backend_config.yaml config/
ln -s ../client/config/client_config.yaml config/
ln -s ../Vehicle-side/config/vehicle_config.yaml config/

# 然后使用原来的路径挂载
docker run -v $(pwd)/config/backend_config.yaml:/app/config/backend_config.yaml:ro backend
docker run -v $(pwd)/config/client_config.yaml:/app/config/client_config.yaml:ro client
docker run -v $(pwd)/config/vehicle_config.yaml:/app/config/vehicle_config.yaml:ro vehicle
```

### 4. 多环境配置

为不同环境（开发、测试、生产）创建不同的配置文件：

```bash
config/
├── backend_dev.yaml
├── backend_test.yaml
├── backend_prod.yaml
├── client_dev.yaml
├── client_test.yaml
├── client_prod.yaml
├── vehicle_dev.yaml
├── vehicle_test.yaml
└── vehicle_prod.yaml
```

在启动时指定配置文件：

```bash
# Backend
./backend --config /app/config/backend_prod.yaml

# 或通过环境变量
export BACKEND_CONFIG=/app/config/backend_prod.yaml
./backend
```

## 配置最佳实践

### 1. 安全性

- **不要在配置文件中硬编码敏感信息**
- **使用环境变量存储密钥和密码**
- **生产环境必须修改默认密钥**
  ```yaml
  # 不要这样做
  keycloak:
    client_secret: "change-me-in-production"
  
  # 应该使用环境变量
  keycloak:
    client_secret: ""  # 从环境变量 KEYCLOAK_CLIENT_SECRET 读取
  ```

### 2. 部署灵活性

- **使用相对路径和模板**
- **通过环境变量覆盖部署特定配置**
- **配置文件与代码分离**

### 3. 调试和日志

- **开发环境启用详细日志**
  ```yaml
  logging:
    level: "debug"
    console_enabled: true
  ```
- **生产环境使用 JSON 结构化日志**
  ```yaml
  logging:
    level: "info"
    format: "json"
    output: "file"
  ```

### 4. 性能调优

- **根据网络条件调整发布频率**
  ```yaml
  status_publish:
    frequency: 50  # 低延迟场景
    # 或
    frequency: 10  # 低带宽场景
  ```

- **调整安全阈值**
  ```yaml
  safety:
    watchdog_timeout_ms: 500  # 正常场景
    # 或
    watchdog_timeout_ms: 300  # 高安全要求场景
  ```

## 常见场景配置

### 场景 1: 高延迟网络环境

```yaml
# client_config.yaml
webrtc:
  video_fps: 15  # 降低帧率
  video_resolution: "854x480"  # 降低分辨率

# vehicle_config.yaml
status_publish:
  frequency: 20  # 降低发布频率

safety:
  light_degradation_rtt_ms: 250  # 提前触发降级
```

### 场景 2: 高安全要求场景

```yaml
# vehicle_config.yaml
safety:
  watchdog_timeout_ms: 300  # 缩短看门狗超时
  severe_degradation_rtt_ms: 200  # 提前触发安全停车
  seq_reset_window_ms: 1000  # 缩短序列号重置窗口

control_protocol:
  enable_signature_validation: true  # 启用签名验证
```

### 场景 3: 低带宽网络环境

```yaml
# client_config.yaml
webrtc:
  video_fps: 10
  video_resolution: "640x360"
  codec_preference:
    - "H264"  # 优先使用高效编解码器

log_sampling:
  mqtt_message_sample_rate: 100  # 减少日志采样
```

### 场景 4: 仿真测试环境

```yaml
# vehicle_config.yaml
carla:
  enabled: true
  host: "localhost"
  port: 2000
  map: "Town01"
  show_window: true

hardware:
  type: "carla"  # 使用仿真硬件接口
```

## 配置验证

### 验证配置文件语法

使用 `yamllint` 验证 YAML 语法：

```bash
# 安装 yamllint
pip install yamllint

# 验证配置文件
yamllint backend/config/backend_config.yaml
yamllint client/config/client_config.yaml
yamllint Vehicle-side/config/vehicle_config.yaml
```

### 验证配置值

在应用启动时，添加配置验证逻辑：

```cpp
// Backend 示例
if (config.keycloak.url.empty()) {
    throw std::runtime_error("Keycloak URL is required");
}

if (config.database.url.empty()) {
    LOG_WARN("DATABASE_URL not configured, running without DB");
}
```

## 故障排查

### 问题 1: 配置文件未加载

**症状**: 应用使用默认值，而非配置文件中的值

**解决方案**:
1. 检查配置文件路径是否正确
2. 检查文件权限
3. 查看日志中的配置加载错误
4. 确认环境变量未覆盖配置

### 问题 2: 环境变量不生效

**症状**: 环境变量设置的值未被应用

**解决方案**:
1. 确认环境变量在应用启动前已设置
2. 检查环境变量名称拼写
3. 在 Docker Compose 中确认 `environment` 部分正确

### 问题 3: YAML 语法错误

**症状**: 应用启动失败，提示配置解析错误

**解决方案**:
1. 使用 `yamllint` 验证 YAML 语法
2. 检查缩进（使用空格，不要使用 Tab）
3. 检查引号匹配
4. 检查冒号后的空格

## 配置示例

### 开发环境配置

```yaml
# backend_dev.yaml
server:
  port: 8081

logging:
  level: "debug"
  console_enabled: true

database:
  url: "postgresql://teleop:dev@localhost:5432/teleop_dev"
```

### 测试环境配置

```yaml
# backend_test.yaml
server:
  port: 8080

logging:
  level: "info"
  console_enabled: false

database:
  url: "postgresql://teleop:test@localhost:5432/teleop_test"
```

### 生产环境配置

```yaml
# backend_prod.yaml
server:
  port: 8080

logging:
  level: "warn"
  format: "json"
  output: "file"

database:
  url: ""  # 从环境变量 DATABASE_URL 读取

security:
  rate_limit_enabled: true
  rate_limit_requests_per_minute: 60
```

## 配置迁移

### 从旧版本迁移

如果从使用环境变量的旧版本迁移到新配置文件系统：

1. **备份当前环境变量配置**
   ```bash
   # 导出当前环境变量
   env | grep -E '(PORT|KEYCLOAK|DATABASE|MQTT|ZLM)' > old_env.txt
   ```

2. **创建对应的配置文件**
   ```bash
   # 根据旧环境变量创建配置文件
   cp config/backend_config.yaml config/backend_custom.yaml
   ```

3. **测试新配置**
   ```bash
   # 使用新配置文件启动
   ./backend --config config/backend_custom.yaml
   ```

4. **逐步切换**
   - 先在开发环境测试
   - 再在测试环境验证
   - 最后在生产环境部署

## 相关文档

- [project_spec.md](../project_spec.md) - 项目规范
- [FEATURE_ADD_CHECKLIST.md](FEATURE_ADD_CHECKLIST.md) - 功能添加检查清单
- [TROUBLESHOOTING_RUNBOOK.md](TROUBLESHOOTING_RUNBOOK.md) - 故障排查手册

## 更新日志

### v1.0 (2026-02-28)
- 初始版本
- 定义 Backend、Client、Vehicle-side 配置文件
- 建立配置优先级机制
- 提供配置最佳实践
