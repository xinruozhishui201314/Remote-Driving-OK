# 部署配置说明

本目录包含 M0 阶段的基础设施部署配置。

## 目录结构

```
deploy/
├── docker-compose.yml          # Docker Compose 主配置文件
├── .env.example                # 环境变量示例文件
├── keycloak/
│   ├── realm-export.json       # Keycloak Realm 配置（包含角色定义）
│   └── import-realm.sh         # Realm 导入脚本
├── postgres/
│   └── init.sql                # PostgreSQL 初始化脚本
├── zlm/
│   └── config.ini               # ZLMediaKit WebRTC 配置文件
├── coturn/
│   └── turnserver.conf          # Coturn STUN/TURN 服务器配置
└── mosquitto/
    ├── mosquitto.conf           # MQTT Broker 配置文件
    ├── init-mosquitto.sh        # 初始化脚本
    ├── monitor-mosquitto.sh    # 监控脚本
    ├── passwd.example           # 密码文件示例
    ├── acl.example              # ACL 文件示例
    └── README.md                # 部署指南
```

## 服务组件

### 1. PostgreSQL
- **端口**: 5432
- **数据库**: 
  - `teleop_db` - 业务数据库
  - `keycloak_db` - Keycloak 数据库
- **用户**: `teleop_user` / `keycloak_user`

### 2. Keycloak
- **端口**: 8080
- **管理控制台**: http://localhost:8080/admin
- **默认管理员**: admin / admin
- **Realm**: teleop
- **角色定义**:
  - `admin`: 管理用户、车辆、策略、审计、录制
  - `owner`: 账号拥有者，管理自己账号下 VIN 的绑定、授权
  - `operator`: 可申请控制指定 VIN（需被授权）
  - `observer`: 仅拉流查看指定 VIN（需被授权）
  - `maintenance`: 查看故障/诊断，可进行受限操作

### 3. ZLMediaKit
- **HTTP**: 80
- **HTTPS**: 443
- **RTMP**: 1935
- **RTSP**: 554
- **WebRTC Signaling**: 3000 (HTTP), 3001 (HTTPS)
- **WebRTC RTC**: 8000 (UDP/TCP)
- **特性**: 
  - WebRTC 推拉流
  - Hook 鉴权（需后端服务）
  - MP4 录制
  - 低延迟优化

### 4. Coturn
- **STUN/TURN**: 3478 (UDP/TCP)
- **中继端口**: 49152-65535 (UDP)
- **用途**: WebRTC NAT 穿透

### 5. MQTT Broker (Mosquitto)
- **端口**: 1883 (MQTT), 8883 (MQTT over TLS), 9001 (WebSocket)
- **用途**: 车端和客户端通信（控制指令和状态数据）
- **特性**: 
  - 公网部署，支持 NAT 穿透
  - TLS 加密和 WebSocket 支持
  - 认证和授权（用户名/密码 + ACL）
  - 异常处理（自动重启、监控）
- **文档**: `deploy/mosquitto/README.md`

### 6. Client Dev (开发环境)
- **镜像**: docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt
- **用途**: Qt6 客户端开发环境
- **使用**: `docker-compose --profile dev up client-dev`

## 快速开始

### 1. 准备环境变量

```bash
cp .env.example .env
# 编辑 .env 文件，修改密码和配置
```

### 2. 启动服务

```bash
# 启动所有服务
docker-compose up -d

# 查看服务状态
docker-compose ps

# 查看日志
docker-compose logs -f
```

### 3. 导入 Keycloak Realm

```bash
# 方式1: 使用导入脚本（推荐）
cd keycloak
./import-realm.sh

# 方式2: 手动导入
# 访问 http://localhost:8080/admin
# 登录后进入 Realm -> Import -> 选择 realm-export.json
```

### 4. 验证服务

```bash
# 检查 PostgreSQL
docker-compose exec postgres psql -U teleop_user -d teleop_db -c "SELECT version();"

# 检查 Keycloak
curl http://localhost:8080/health/ready

# 检查 ZLMediaKit
curl http://localhost/index/api/getServerConfig

# 检查 Coturn
docker-compose exec coturn turnserver -c /etc/turnserver.conf --listening-port 3478

# 检查 MQTT Broker
mosquitto_sub -h localhost -t 'test' -u client_user -P client_password_change_in_prod
```

## 配置说明

### Keycloak Realm 配置

Realm 配置文件 `keycloak/realm-export.json` 包含：
- Realm 基础配置
- 5 个角色定义（admin/owner/operator/observer/maintenance）
- 2 个客户端配置（teleop-backend / teleop-client）

### ZLMediaKit WebRTC 配置

`zlm/config.ini` 针对远程驾驶场景优化：
- 启用 WebRTC（端口 3000/3001/8000）
- 配置 Coturn 作为 STUN/TURN 服务器
- 启用 Hook 鉴权（需后端服务支持）
- 低延迟优化（关闭合并写、减小缓存）
- 启用 MP4 录制

### Coturn 配置

`coturn/turnserver.conf` 配置：
- STUN/TURN 服务端口 3478
- 中继端口范围 49152-65535
- 认证通过环境变量配置

## 网络配置

所有服务运行在 `teleop-network` 网络中，可以通过服务名互相访问：
- `postgres` - PostgreSQL 服务
- `keycloak` - Keycloak 服务
- `zlmediakit` - ZLMediaKit 服务
- `coturn` - Coturn 服务
- `mosquitto` - MQTT Broker 服务
- `backend` - 后端服务（待实现）

## 生产环境注意事项

1. **密码安全**: 必须修改所有默认密码（`.env` 文件）
2. **TLS/HTTPS**: 配置 SSL 证书，启用 HTTPS
3. **公网 IP**: 配置 `COTURN_EXTERNAL_IP` 和 `ZLM_EXTERN_IP`
4. **防火墙**: 开放必要端口（80, 443, 3000, 3478, 8000, 49152-65535）
5. **数据持久化**: 数据卷已配置，确保备份策略
6. **监控告警**: 配置健康检查和监控
7. **资源限制**: 根据实际负载配置资源限制

## 故障排查

### Keycloak 无法启动
- 检查 PostgreSQL 是否就绪
- 查看日志: `docker-compose logs keycloak`

### ZLMediaKit WebRTC 连接失败
- 检查 Coturn 是否正常运行
- 验证端口映射是否正确
- 检查防火墙规则

### Coturn 无法穿透 NAT
- 配置 `COTURN_EXTERNAL_IP` 环境变量
- 确保端口 3478 和 49152-65535 已开放

## 下一步

M0 阶段完成后，需要实现：
1. 后端服务（backend/）集成到 docker-compose
2. 数据库迁移脚本（创建表结构）
3. Hook 接口实现（ZLMediaKit 鉴权）
4. 客户端集成测试
