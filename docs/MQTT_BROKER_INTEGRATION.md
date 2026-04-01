# MQTT Broker 集成到全链路启动脚本

## Executive Summary

**目标**：将 MQTT Broker (Mosquitto) 作为独立节点集成到 `start-full-chain.sh`，实现一键启动和自动验证。

**完成内容**：
- ✅ 更新 `start-full-chain.sh` 集成 mosquitto 服务
- ✅ 创建 `verify-mosquitto.sh` 自动验证脚本
- ✅ 更新 `docker-compose.vehicle.dev.yml` 使用 mosquitto
- ✅ 确保全链路启动时自动验证 MQTT Broker 功能

---

## 1. 集成说明

### 1.1 服务名称统一

**统一使用 `mosquitto` 作为服务名**：
- Docker Compose 服务名：`mosquitto`
- 容器名：`teleop-mosquitto`
- 网络内服务名：`mosquitto`（用于容器间通信）

### 1.2 启动顺序

```
1. postgres, keycloak, coturn
2. zlmediakit, mosquitto  ← MQTT Broker 在此阶段启动
3. backend
4. vehicle, client-dev
```

### 1.3 验证流程

在 `start-full-chain.sh` 的 `verify_chain()` 函数中：
- `[2.5] MQTT Broker` - 调用 `verify-mosquitto.sh` 进行完整验证

---

## 2. 验证脚本功能

### 2.1 `verify-mosquitto.sh` 验证项

1. **检查 Broker 运行状态**
   - 检查容器是否运行
   - 检查容器状态

2. **检查端口监听**
   - 检查端口 1883 是否可连接

3. **测试 MQTT 连接**
   - 测试基本连接功能

4. **测试认证**
   - 测试用户名/密码认证

5. **测试发布/订阅**
   - 测试消息发布和接收

6. **测试 ACL 权限**
   - 测试主题访问控制

### 2.2 使用方法

```bash
# 独立验证 MQTT Broker
bash scripts/verify-mosquitto.sh

# 在全链路启动中自动验证
bash scripts/start-full-chain.sh
```

---

## 3. 配置更新

### 3.1 docker-compose.yml

**已添加 mosquitto 服务**：
```yaml
mosquitto:
  image: docker.1ms.run/eclipse-mosquitto:2.0
  container_name: teleop-mosquitto
  # ... 完整配置
```

### 3.2 docker-compose.vehicle.dev.yml

**更新 vehicle 服务**：
- 移除旧的 `mqtt-broker` 服务定义
- 更新 `MQTT_BROKER_URL` 为 `mqtt://mosquitto:1883`
- 更新 `depends_on` 为 `mosquitto`

### 3.3 start-full-chain.sh

**更新内容**：
- 启动顺序中包含 `mosquitto`
- 节点检查中包含 `teleop-mosquitto`
- 验证流程中调用 `verify-mosquitto.sh`
- 客户端环境变量使用 `mqtt://mosquitto:1883`

---

## 4. 使用示例

### 4.1 一键启动全链路

```bash
# 启动全链路 + 验证 + 启动客户端
bash scripts/start-full-chain.sh

# 仅启动全链路 + 验证，不启动客户端
bash scripts/start-full-chain.sh no-client

# 启动全链路，跳过验证
bash scripts/start-full-chain.sh no-verify
```

### 4.2 独立验证 MQTT Broker

```bash
# 先启动 Broker
docker compose up -d mosquitto

# 运行验证脚本
bash scripts/verify-mosquitto.sh
```

---

## 5. 验证输出示例

```
========================================
MQTT Broker (Mosquitto) 功能验证
========================================

MQTT Broker: 127.0.0.1:1883
用户: client_user
测试主题: test/verify

[1/6] 检查 Broker 运行状态...
  ✓ Broker 正在运行
    状态: Up 2 minutes

[2/6] 检查端口监听...
  ✓ 端口 1883 可连接

[3/6] 测试 MQTT 连接...
  ✓ MQTT 连接成功

[4/6] 测试认证...
  ✓ 认证成功（用户: client_user）

[5/6] 测试发布/订阅功能...
  ✓ 发布/订阅功能正常
    发布消息: {"test":"mqtt_broker_verification","timestamp":1234567890}
    接收消息: {"test":"mqtt_broker_verification","timestamp":1234567890}

[6/6] 测试主题权限（ACL）...
  ✓ ACL 权限正常（可发布到 vehicle/control）

========================================
MQTT Broker 功能验证通过！
========================================
```

---

## 6. 故障排查

### 问题1：Broker 未运行

**症状**：验证脚本提示 "Broker 未运行"

**解决**：
```bash
# 检查容器状态
docker compose ps mosquitto

# 查看日志
docker compose logs mosquitto

# 重启 Broker
docker compose restart mosquitto
```

### 问题2：端口不可连接

**症状**：验证脚本提示 "端口 1883 不可连接"

**解决**：
```bash
# 检查端口映射
docker compose ps mosquitto | grep 1883

# 检查防火墙
sudo ufw status | grep 1883

# 测试连接
telnet localhost 1883
```

### 问题3：认证失败

**症状**：验证脚本提示 "认证失败"

**解决**：
```bash
# 检查密码文件
docker compose exec mosquitto cat /mosquitto/config/passwd

# 重新初始化
docker compose exec mosquitto /mosquitto/config/init-mosquitto.sh

# 检查环境变量
docker compose config | grep MQTT_CLIENT_PASSWORD
```

---

## 7. 相关文件

- `scripts/start-full-chain.sh` - 全链路启动脚本
- `scripts/verify-mosquitto.sh` - MQTT Broker 验证脚本
- `docker-compose.yml` - 主配置文件（包含 mosquitto 服务）
- `docker-compose.vehicle.dev.yml` - 车端开发配置
- `deploy/mosquitto/README.md` - MQTT Broker 部署指南

---

## 8. 总结

✅ **集成完成**：
- MQTT Broker 已作为独立节点集成到全链路启动脚本
- 自动验证功能已实现
- 配置已统一更新

✅ **验证功能**：
- 6 项完整验证（运行状态、端口、连接、认证、发布/订阅、ACL）
- 自动故障排查提示
- 清晰的输出格式

✅ **使用便捷**：
- 一键启动全链路
- 自动验证所有节点
- 独立验证脚本可用
