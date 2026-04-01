# start-full-chain.sh 重启功能说明

## 概述

`start-full-chain.sh` 脚本现在具备完整的重启功能，可以自动停止并清理旧容器，确保每次启动都在干净的环境中运行，避免之前运行的程序影响。

## 功能特性

### 1. 自动清理功能（默认启用）

脚本在启动前会自动：

- ✅ **停止所有相关容器**：包括 compose 管理的容器和残留容器
- ✅ **删除已停止的容器**：清理所有相关容器
- ✅ **检查端口占用**：检查关键端口是否被占用
- ✅ **等待资源释放**：确保端口和资源完全释放

### 2. 清理的容器列表

脚本会自动清理以下容器：

- `teleop-postgres` - 数据库服务
- `teleop-keycloak` - 认证服务
- `teleop-coturn` - TURN 服务器
- `teleop-zlmediakit` - 流媒体服务器
- `teleop-backend` - 后端服务
- `teleop-mosquitto` - MQTT Broker
- `teleop-client-dev` - 客户端开发容器
- `teleop-mqtt` - 旧的 MQTT 容器（如果存在）
- `remote-driving-vehicle*` - 所有车端容器（名称匹配）

### 3. 检查的端口

脚本会检查以下关键端口：

- `1883` - MQTT Broker
- `8080` - Keycloak
- `8081` - Backend
- `5432` - PostgreSQL
- `80` - ZLMediaKit
- `3478` - CoTURN

## 使用方法

### 默认行为（自动清理）

```bash
# 默认会清理旧容器后启动
bash scripts/start-full-chain.sh manual

# 或者
bash scripts/start-full-chain.sh
```

### 跳过清理（快速重启）

如果确定环境干净，可以跳过清理步骤以加快启动速度：

```bash
bash scripts/start-full-chain.sh manual no-cleanup
```

### 强制清理

如果需要强制清理（默认行为，但可以显式指定）：

```bash
bash scripts/start-full-chain.sh manual cleanup
```

## 组合使用

可以组合多个参数：

```bash
# 清理 + 手动模式 + 跳过验证
bash scripts/start-full-chain.sh manual no-verify cleanup

# 不清理 + 手动模式 + 不启动客户端
bash scripts/start-full-chain.sh manual no-client no-cleanup
```

## 清理过程示例

运行脚本时会看到类似输出：

```
========== 0. 停止并清理旧容器 ==========
停止所有相关服务...
查找并停止残留容器...
  停止容器: teleop-postgres
  删除容器: teleop-postgres
  停止容器: teleop-keycloak
  删除容器: teleop-keycloak
  ...
清理车端容器...
  停止容器: remote-driving-vehicle-1
  删除容器: remote-driving-vehicle-1
检查关键端口占用...
清理完成

========== 1. 启动全链路节点 ==========
...
```

## 为什么需要清理？

### 问题场景

1. **端口冲突**：旧容器占用端口，新容器无法启动
2. **状态残留**：旧容器的状态可能影响新启动的容器
3. **资源泄漏**：未正确停止的容器占用资源
4. **配置冲突**：旧容器的配置可能与新配置冲突

### 解决方案

通过自动清理，确保：

- ✅ 每次启动都在干净的环境中
- ✅ 避免端口冲突
- ✅ 避免状态残留
- ✅ 避免资源泄漏
- ✅ 确保配置一致性

## 注意事项

### 1. 数据持久化

清理容器**不会删除数据卷**（volumes），以下数据会保留：

- PostgreSQL 数据库数据
- Keycloak 配置和用户数据
- Mosquitto 持久化消息
- ZLMediaKit 录制文件（如果配置了）

如果需要完全清理数据，需要手动删除 volumes：

```bash
docker compose down -v
```

### 2. 清理时间

清理过程通常需要 5-10 秒，取决于：

- 容器数量
- 容器停止时间
- 资源释放时间

### 3. 强制清理

如果遇到端口被占用但进程不是 Docker 容器的情况，脚本会提示但不会强制终止，需要手动处理：

```bash
# 查看端口占用
lsof -i :1883

# 手动终止进程（谨慎操作）
kill -9 <PID>
```

## 故障排查

### 问题1：清理后容器仍无法启动

**排查步骤**：

1. **检查端口占用**：
   ```bash
   lsof -i :1883
   lsof -i :8080
   ```

2. **检查 Docker 网络**：
   ```bash
   docker network ls
   docker network prune  # 清理未使用的网络
   ```

3. **检查 Docker Compose 状态**：
   ```bash
   docker compose ps -a
   docker compose down --remove-orphans
   ```

### 问题2：清理时间过长

**解决方案**：

- 使用 `no-cleanup` 参数跳过清理（如果确定环境干净）
- 或者手动清理特定容器：
  ```bash
  docker stop <container_name>
  docker rm <container_name>
  ```

### 问题3：清理后数据丢失

**注意**：清理容器不会删除 volumes，数据应该保留。如果数据丢失，检查：

```bash
# 检查 volumes
docker volume ls | grep teleop

# 检查 volume 内容
docker volume inspect <volume_name>
```

## 最佳实践

1. **开发环境**：使用默认清理功能，确保每次启动干净
2. **生产环境**：谨慎使用清理功能，确保数据安全
3. **快速迭代**：使用 `no-cleanup` 加快启动速度
4. **问题排查**：使用 `cleanup` 强制清理，排除环境问题

## 相关文档

- `docs/CHASSIS_DATA_VERIFICATION.md` - 底盘数据流验证指南
- `docs/CHASSIS_DATA_LOGGING.md` - 日志系统说明
- `scripts/start-full-chain.sh` - 脚本源码
