# M0 阶段服务启动验证指南

**目的**: 在 Docker daemon 运行后启动和验证 M0 阶段服务

---

## Executive Summary

**当前状态**: ⚠️ **配置完成，Docker daemon 未运行**

**需要**: Docker 权限启动 Docker daemon 并验证服务

---

## 1. Docker 启动步骤

### 1.1 检查 Docker 环境

```bash
# 检查 Docker 版本
docker --version

# 检查 Docker Compose 版本
docker-compose --version
```

### 1.2 启动 Docker 服务

```bash
# 方式1: systemctl（推荐）
sudo systemctl start docker
sudo systemctl enable docker  # 开机自启

# 方式2: service 命令
sudo service docker start

# 方式3: 手动启动
sudo dockerd &
```

### 1.3 验证 Docker 运行

```bash
docker ps
# 应该看到容器列表（可能为空或只有系统容器）
docker info
# 应该看到 Docker 系统信息
```

---

## 2. 服务启动

### 2.1 进入项目目录

```bash
cd /home/wqs/bigdata/Remote-Driving
```

### 2.2 启动所有服务

```bash
cd deploy
cp .env.example .env  # 如果 .env 不存在

cd ..
docker-compose up -d
```

**预期输出**:
```
Creating network "teleop-network"
Creating volume "..."
Creating teleop-postgres ...
Creating teleop-keycloak ...
Creating teleop-zlmediakit ...
Creating teleop-coturn ...
```

### 2.3 等待服务启动

```bash
# 等待 30-60 秒让服务完全启动
echo "等待服务启动..."
sleep 30

# 检查服务状态
docker-compose ps
```

**预期状态**:
- 所有服务状态为 "Up"
- 应至少看到 postgres, keycloak, zlmediakit, cotrun
- backend 和 client-dev 可选（根据配置）

---

## 3. 服务验证

### 3.1 快速验证

```bash
# 运行端到端测试
cd scripts
bash e2e.sh
```

### 3.2 手动验证

#### PostgreSQL

```bash
# 健康检查
docker-compose exec postgres pg_isready -U teleop_user -d teleop_db

# 连接测试
docker-compose exec -T postgres psql -U teleop_user -d teleop_db -c "\l"
```

#### Keycloak

```bash
# 健康检查
curl -I http://localhost:8080/health/ready

# 访问管理控制台
# http://localhost:8080/admin
# 默认账号: admin / admin
```

#### ZLMediaKit

```bash
# API 测试
curl http://localhost/index/api/getServerConfig

# 视频流测试（如果有）
curl http://localhost/index/api/isOnline
```

#### Coturn

```bash
# STUN 测试
nc -zv localhost 3478
```

---

## 4. Keycloak Realm 导入

### 4.1 自动导入

Keycloak 已通过 `--import-realm` 参数配置自动导入。

### 4.2 手动导入（如果需要）

```bash
cd deploy/keycloak
./import-realm.sh
```

### 4.3 验证 Realm

```bash
# 获取 Token
TOKEN=$(curl -s -X POST "http://localhost:8080/realms/master/protocol/openid-connect/token" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "username=admin" \
    -d "password=admin" \
    -d "grant_type=password" \
    -d "client_id=admin-cli" | grep -o '"access_token":"[^"]*' | cut -d'"' -f4)

# 检查 Realm
curl -H "Authorization: Bearer ${TOKEN}" \
    http://localhost:8080/admin/realms/teleop

# 检查角色
curl -H "Authorization: Bearer ${TOKEN}" \
    http://localhost:8080/admin/realms/teleop/roles
```

---

## 5. 数据库迁移

```bash
# 执行数据库迁移
docker-compose exec postgres psql -U teleop_user -d teleop_db < \
    backend/migrations/001_initial_schema.sql

# 验证表结构
docker-compose exec postgres psql -U teleop_user -d teleop_db -c "\dt"
```

---

## 6. 服务日志

```bash
# 查看所有服务日志
docker-compose logs

# 查看特定服务日志
docker-compose logs postgres
docker-compose logs keycloak
docker-compose logs zlmediakit
docker-compose logs coturn

# 实时跟踪日志
docker-compose logs -f
docker-compose logs -f zlmediakit
```

---

## 7. 停止服务

```bash
# 停止所有服务
docker-compose stop

# 停止并删除容器
docker-compose down

# 停止并删除容器+数据卷
docker-compose down -v
```

---

## 8. 故障排查

### 8.1 Docker 无法启动

```bash
# 检查 Docker 服务状态
sudo systemctl status docker

# 查看 Docker 日志
sudo journalctl -u docker

# 重启 Docker
sudo systemctl restart docker
```

### 8.2 服务无法启动

```bash
# 查看服务日志
docker-compose logs [service_name]

# 检查端口占用
netstat -tuln | grep -E '5432|8080|80|3000|3478'

# 重新启动问题服务
docker-compose restart [service_name]
```

### 8.3 Keycloak Realm 导入失败

```bash
# 检查 Postgres 连接
docker-compose exec postgres pg_isready -U postgres -d postgres_db

# 查看 Keycloak 日志
docker-compose logs keycloak | grep -i "import\|realm"

# 手动导入
cd deploy/keycloak
./import-realm.sh
```

---

## 9. 访问地址

| 服务 | 地址 | 说明 |
|------|------|------|
| Keycloak Admin Console | http://localhost:8080/admin | 默认账号: admin / admin |
| Keycloak Account Console | http://localhost:8080/realms/teleop/account | |
| ZLMediaKit API | http://localhost/index/api/getServerConfig | |
| ZLMediaKit WebRTC | http://localhost:3000 | WebRTC 信令 |
| PostgreSQL | localhost:5432 | 数据库: teleop_db |

---

## 10. 验收检查清单

### 环境检查
- [ ] Docker 版本兼容
- [ ] Docker Compose 版本兼容
- [ ] 端口未被占用

### 服务启动
- [ ] postgres 服务启动
- [ ] keycloak 服务启动
- [ ] zlmediakit 服务启动
- [] coturn 服务启动

### 健康检查
- [ ] PostgreSQL 健康检查通过
- [ ] Keycloak 健康检查通过
- [ ] ZLMediaKit API 可用

### 功能验证
- [ ] Keycloak Realm 导入成功
- [ ] 5 个角色已创建
- [ ] 数据库迁移执行成功
- [ ] e2e 测试通过

---

## 11. e2e 测试内容

`scripts/e2e.sh` 将验证以下内容：

1. **环境检查**
   - Docker 可用性
   - Docker Compose 可用性

2. **配置文件检查**
   - 所有配置文件存在性

3. **配置验证**
   - docker-compose.yml 语法验证

4. **服务运行状态**
   - 各服务容器运行检查

5. **服务健康检查**
   - PostgreSQL 健康
   - Keycloak 健康
   - ZLMediaKit API
   - Coturn 运行

6. **Keycloak Realm**
   - Realm 存在性
   - 5 个角色定义

7. **端口可用性**
   - 所有必要端口状态

---

## 12. 总结

**完成 M0 阶段，启动和验证服务需要以下步骤**：

1. **启动 Docker daemon**（需要管理员权限）
2. **启动服务**: `docker-compose up -d`
3. **验证服务**: `bash scripts/e2e.sh`
4. **导入 Realm**: `cd deploy/keycloak && ./import-realm.sh`
5. **数据库迁移**: 执行数据库迁移脚本

**在 Docker 可用时执行上述步骤即可完成 M0 阶段的服务验证。**
