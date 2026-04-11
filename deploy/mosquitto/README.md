# MQTT Broker (Mosquitto) 部署指南

## Executive Summary

**目标**：部署公网 MQTT Broker 节点，支持车端和客户端通信，处理各种异常情况。

**架构**：
```
车端 (内网/NAT后) → MQTT → Broker (公网) → MQTT → 客户端 (内网/NAT后)
```

**关键特性**：
- ✅ 公网部署，支持 NAT 穿透
- ✅ TLS 加密（端口 8883）
- ✅ WebSocket 支持（端口 9001，防火墙穿透）
- ✅ 认证和授权（用户名/密码 + ACL）
- ✅ 异常处理（自动重启、监控、日志）
- ✅ 持久化存储（QoS 1/2 消息）

---

## 1. 快速开始

### 1.1 使用 Docker Compose（推荐）

```bash
# 1. 配置环境变量
cp deploy/.env.example deploy/.env
# 编辑 deploy/.env，修改 MQTT 密码

# 2. 启动 MQTT Broker
docker-compose up -d mosquitto

# 3. 查看日志
docker-compose logs -f mosquitto

# 4. 验证连接
mosquitto_sub -h localhost -t 'test' -u client_user -P client_password_change_in_prod
```

### 1.2 手动部署

```bash
# 1. 创建目录
mkdir -p /opt/mosquitto/{config,data,log}

# 2. 复制配置文件
cp deploy/mosquitto/mosquitto.conf /opt/mosquitto/config/

# 3. 运行初始化脚本
./deploy/mosquitto/init-mosquitto.sh

# 4. 启动 Broker
mosquitto -c /opt/mosquitto/config/mosquitto.conf -d
```

---

## 2. 配置说明

### 2.1 端口配置

| 端口 | 协议 | 用途 | 说明 |
|------|------|------|------|
| 1883 | MQTT (TCP) | 标准 MQTT | 开发环境 |
| 8883 | MQTT over TLS | 加密 MQTT | 生产环境推荐 |
| 9001 | MQTT over WebSocket | WebSocket | 防火墙穿透 |

### 2.2 认证配置

**默认用户**（生产环境必须修改）：
- `vehicle_side` - 车端用户（订阅控制，发布状态）
- `client_user` - 客户端用户（发布控制，订阅状态）
- `admin` - 管理员用户（全部权限）

**密码文件**：
```bash
# 创建密码文件
mosquitto_passwd -c /mosquitto/config/passwd username

# 添加用户
mosquitto_passwd /mosquitto/config/passwd username
```

### 2.3 ACL 配置

**主题权限**：
- `vehicle/control` - 控制指令（客户端→车端）
- `vehicle/<VIN>/control` - 特定车辆控制指令
- `vehicle/status` - 状态发布（车端→客户端）
- `vehicle/<VIN>/status` - 特定车辆状态
- `teleop/client_encoder_hint` - 客户端转发的视频编码提示（客户端→broker→车端/carla-bridge；与 DataChannel 同形 JSON）
- `$SYS/#` - 系统主题（仅管理员）

**ACL 文件格式**：
```
user vehicle_side
topic read vehicle/control
topic read teleop/client_encoder_hint
topic write vehicle/status

user client_user
topic write vehicle/control
topic write teleop/client_encoder_hint
topic read vehicle/status
```

---

## 3. 异常处理

### 3.1 连接异常

**症状**：客户端无法连接到 Broker

**排查步骤**：
1. **检查 Broker 是否运行**：
   ```bash
   docker-compose ps mosquitto
   docker-compose logs mosquitto
   ```

2. **检查端口是否监听**：
   ```bash
   netstat -tuln | grep 1883
   # 或
   ss -tuln | grep 1883
   ```

3. **检查防火墙**：
   ```bash
   # 云服务器安全组：开放 1883, 8883, 9001
   # 本地防火墙：
   sudo ufw allow 1883/tcp
   sudo ufw allow 8883/tcp
   sudo ufw allow 9001/tcp
   ```

4. **测试连接**：
   ```bash
   # 测试 TCP 连接
   telnet broker-ip 1883
   
   # 测试 MQTT 连接
   mosquitto_sub -h broker-ip -t 'test' -u username -P password
   ```

**解决方案**：
- ✅ 确保 Broker 运行正常
- ✅ 检查端口映射（Docker）
- ✅ 检查防火墙规则
- ✅ 检查网络连通性

### 3.2 认证失败

**症状**：连接被拒绝，提示认证失败

**排查步骤**：
1. **检查用户名/密码**：
   ```bash
   # 验证密码文件
   mosquitto_passwd -U /mosquitto/config/passwd
   ```

2. **检查 ACL 权限**：
   ```bash
   # 查看 ACL 文件
   cat /mosquitto/config/acl
   ```

3. **查看日志**：
   ```bash
   docker-compose logs mosquitto | grep -i "auth\|denied\|failed"
   ```

**解决方案**：
- ✅ 验证用户名和密码正确
- ✅ 检查 ACL 文件格式
- ✅ 确保用户有相应主题权限

### 3.3 消息丢失

**症状**：消息未到达目标客户端

**排查步骤**：
1. **检查 QoS 级别**：
   - QoS 0：最多一次（可能丢失）
   - QoS 1：至少一次（可能重复）
   - QoS 2：恰好一次（保证到达）

2. **检查订阅**：
   ```bash
   # 查看活跃连接
   mosquitto_sub -h localhost -t '$SYS/broker/clients/active' -u admin -P admin_password
   ```

3. **检查消息队列**：
   ```bash
   # 查看队列大小
   mosquitto_sub -h localhost -t '$SYS/broker/messages/stored' -u admin -P admin_password
   ```

**解决方案**：
- ✅ 使用 QoS 1 或 2（重要消息）
- ✅ 确保客户端已订阅主题
- ✅ 检查消息队列大小限制
- ✅ 启用持久化存储

### 3.4 连接断开

**症状**：连接频繁断开

**排查步骤**：
1. **检查 Keep-Alive**：
   ```bash
   # 查看 Keep-Alive 配置
   grep keepalive /mosquitto/config/mosquitto.conf
   ```

2. **检查 NAT 超时**：
   - NAT 映射可能超时（通常 60-300 秒）
   - 客户端 Keep-Alive 应小于 NAT 超时时间

3. **检查网络稳定性**：
   ```bash
   # 监控连接数
   watch -n 1 'mosquitto_sub -h localhost -t "$SYS/broker/clients/connected" -u admin -P admin_password -C 1'
   ```

**解决方案**：
- ✅ 设置合适的 Keep-Alive（60 秒）
- ✅ 启用自动重连（客户端）
- ✅ 使用 TLS（更稳定）
- ✅ 检查网络质量

### 3.5 性能问题

**症状**：消息延迟高，连接数过多

**排查步骤**：
1. **检查连接数**：
   ```bash
   mosquitto_sub -h localhost -t '$SYS/broker/clients/connected' -u admin -P admin_password -C 1
   ```

2. **检查消息速率**：
   ```bash
   mosquitto_sub -h localhost -t '$SYS/broker/messages/received' -u admin -P admin_password -C 1
   ```

3. **检查资源使用**：
   ```bash
   docker stats teleop-mosquitto
   ```

**解决方案**：
- ✅ 增加 `max_connections`（配置文件）
- ✅ 优化消息队列大小
- ✅ 使用集群部署（高负载）
- ✅ 监控资源使用

### 3.6 磁盘空间不足

**症状**：持久化消息无法保存

**排查步骤**：
```bash
# 检查磁盘使用
df -h /mosquitto/data

# 检查持久化文件大小
du -sh /mosquitto/data/*
```

**解决方案**：
- ✅ 清理旧消息（配置 `message_expiry_interval`）
- ✅ 增加磁盘空间
- ✅ 使用外部存储（NFS/S3）

---

## 4. 监控脚本

### 4.1 使用监控脚本

```bash
# 启动监控脚本（后台运行）
./deploy/mosquitto/monitor-mosquitto.sh &

# 查看监控日志
tail -f /mosquitto/log/monitor.log
```

**监控功能**：
- ✅ 自动检测 Broker 运行状态
- ✅ 自动重启（异常时）
- ✅ 检查端口监听
- ✅ 检查磁盘空间
- ✅ 检查错误日志

### 4.2 集成到 systemd

```ini
# /etc/systemd/system/mosquitto-monitor.service
[Unit]
Description=MQTT Broker Monitor
After=mosquitto.service

[Service]
Type=simple
ExecStart=/path/to/monitor-mosquitto.sh
Restart=always

[Install]
WantedBy=multi-user.target
```

---

## 5. TLS 配置

### 5.1 使用自签名证书（开发环境）

初始化脚本会自动生成自签名证书。

### 5.2 使用 CA 签名证书（生产环境）

```bash
# 1. 准备证书文件
# - ca.crt (CA 证书)
# - server.crt (服务器证书)
# - server.key (服务器私钥)

# 2. 复制到配置目录
cp ca.crt /mosquitto/config/certs/
cp server.crt /mosquitto/config/certs/
cp server.key /mosquitto/config/certs/

# 3. 设置权限
chmod 600 /mosquitto/config/certs/server.key
chmod 644 /mosquitto/config/certs/server.crt

# 4. 重启 Broker
docker-compose restart mosquitto
```

**客户端连接**：
```bash
# 使用 TLS
mosquitto_sub -h broker-ip -p 8883 --cafile ca.crt -t 'test' -u username -P password
```

---

## 6. 生产环境检查清单

### 6.1 安全配置

- [ ] 修改默认密码（`.env` 文件）
- [ ] 配置 TLS 证书（CA 签名）
- [ ] 配置 ACL（最小权限原则）
- [ ] 禁用匿名连接（`allow_anonymous false`）
- [ ] 配置防火墙规则
- [ ] 启用日志审计

### 6.2 性能配置

- [ ] 设置 `max_connections`（根据负载）
- [ ] 配置消息队列大小
- [ ] 设置资源限制（Docker）
- [ ] 配置持久化存储
- [ ] 监控连接数和消息速率

### 6.3 高可用性

- [ ] 配置自动重启（`restart: unless-stopped`）
- [ ] 启用健康检查
- [ ] 配置监控告警
- [ ] 备份配置文件和数据
- [ ] 考虑集群部署（高负载）

### 6.4 运维配置

- [ ] 配置日志轮转
- [ ] 设置日志级别
- [ ] 配置监控脚本
- [ ] 设置告警规则
- [ ] 准备故障排查文档

---

## 7. 故障排查

### 常见问题

**Q1: Broker 无法启动**

```bash
# 检查配置文件语法
mosquitto -c /mosquitto/config/mosquitto.conf -v

# 查看详细日志
docker-compose logs mosquitto
```

**Q2: 客户端连接超时**

- 检查防火墙规则
- 检查端口映射（Docker）
- 检查网络连通性
- 检查 Broker 是否运行

**Q3: 消息未到达**

- 检查订阅主题是否正确
- 检查 QoS 级别
- 检查 ACL 权限
- 查看 Broker 日志

**Q4: 性能问题**

- 检查连接数限制
- 检查消息队列大小
- 检查资源使用（CPU/内存）
- 考虑集群部署

---

## 8. 相关文档

- `docs/MQTT_NAT_PENETRATION.md` - MQTT NAT 穿透能力分析
- `docs/VERIFY_CHASSIS_DATA_FLOW.md` - 数据流验证指南
- `project_spec.md` - 项目规范文档

---

## 9. 支持与反馈

如有问题，请查看：
1. 日志文件：`/mosquitto/log/mosquitto.log`
2. Docker 日志：`docker-compose logs mosquitto`
3. 监控脚本日志：`/mosquitto/log/monitor.log`
