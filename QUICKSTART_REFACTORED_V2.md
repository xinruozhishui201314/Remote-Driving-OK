# Remote-Driving - 快速入门指南（整理后版本v2）

本文档帮助您快速上手使用整理后的项目结构和配置。

## 🚀 5分钟快速开始

### 方式一：使用Docker Compose一键启动完整链路

```bash
# 1. 启动所有服务
docker compose up -d

# 2. 查看服务状态
docker compose ps

# 3. 查看Backend日志
docker compose logs -f backend

# 4. 停止所有服务
docker compose down
```

### 方式二：单独编译和运行各模块

#### Backend

```bash
cd backend
./scripts/build.sh
./scripts/run.sh
# 访问 http://localhost:8000/health
```

#### Client

```bash
cd client
./scripts/build.sh
./scripts/run.sh
```

#### Vehicle-side

```bash
cd Vehicle-side
./build.sh
./run.sh
```

---

## 📁 配置文件位置

### 新旧位置对比

整理后，配置文件从根目录 `config/` 移动到各模块的 `config/` 目录下：

| 配置文件 | 旧位置 | 新位置 | 容器内路径 |
|---------|--------|--------|-----------|
| Backend配置 | `config/backend_config.yaml` | `backend/config/backend_config.yaml` | `/app/config/backend_config.yaml` |
| Client配置 | `config/client_config.yaml` | `client/config/client_config.yaml` | `/app/config/client_config.yaml` |
| Vehicle-side配置 | `config/vehicle_config.yaml` | `Vehicle-side/config/vehicle_config.yaml` | `/app/config/vehicle_config.yaml` |

### 向后兼容

为了保持向后兼容，根目录的 `config/` 目录保留了符号链接：

```bash
config/
├── backend_config.yaml -> ../backend/config/backend_config.yaml
├── client_config.yaml -> ../client/config/client_config.yaml
└── vehicle_config.yaml -> ../Vehicle-side/config/vehicle_config.yaml
```

这意味着：
- ✅ 现有的挂载路径 `./config/backend_config.yaml` 仍然有效
- ✅ 现有的脚本和文档引用仍然有效
- ✅ 可以无缝迁移到新的配置文件位置

---

## 📚 模块独立部署

### Backend 独立部署

**使用 Docker Compose（推荐）**

```bash
cd backend
docker compose up -d
```

**使用独立镜像**

```bash
# 构建镜像
docker build -t teleop-backend:latest backend/

# 运行容器
docker run -d \
  -p 8000:8000 \
  -v $(pwd)/backend/config/backend_config.yaml:/app/config/backend_config.yaml:ro \
  -e DATABASE_URL=postgresql://postgres:postgres@postgres:5432/teleop \
  -e KEYCLOAK_URL=http://keycloak:8080 \
  teleop-backend:latest
```

### Client 独立部署

**使用独立镜像**

```bash
# 构建镜像
docker build -f Dockerfile.prod -t teleop-client:latest client/

# 运行容器（桌面环境）
docker run -it \
  -v $(pwd)/client/config/client_config.yaml:/app/config/client_config.yaml:ro \
  -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -e BACKEND_URL=http://backend:8000 \
  teleop-client:latest

# 运行容器（无头模式）
docker run -d \
  -v $(pwd)/client/config/client_config.yaml:/app/config/client_config.yaml:ro \
  -e QT_QPA_PLATFORM=offscreen \
  -e BACKEND_URL=http://backend:8000 \
  teleop-client:latest
```

### Vehicle-side 独立部署

**使用 Docker Compose（推荐）**

```bash
cd Vehicle-side
docker compose up -d
```

**使用独立镜像**

```bash
# 构建镜像
docker build -f Dockerfile.prod -t teleop-vehicle:latest Vehicle-side/

# 运行容器
docker run -d \
  --privileged \
  -v $(pwd)/Vehicle-side/config/vehicle_config.yaml:/app/config/vehicle_config.yaml:ro \
  -e MQTT_BROKER_URL=mqtt://mosquitto:1883 \
  -e ZLM_RTMP_URL=rtmp://zlmediakit:1935 \
  teleop-vehicle:latest
```

---

## 🔧 全局脚本

### 编译所有模块

```bash
./scripts/build-all.sh
```

### 部署所有模块（构建Docker镜像）

```bash
./scripts/deploy-all.sh
```

### 验证所有模块

```bash
./scripts/verify-all.sh
```

---

## 🌐 分布式部署（跨网络）

### 场景：Backend在云服务器，Client在本地电脑

#### 服务器端（Backend）

```bash
cd backend
docker compose up -d
```

**配置关键环境变量：**
```yaml
ZLM_PUBLIC_BASE=https://media.yourdomain.com  # 对外可访问的ZLM地址
MQTT_BROKER_URL=mqtts://mqtt.yourdomain.com:8883  # 对外可访问的MQTT地址
```

#### 客户端（本地电脑）

```bash
docker run -d \
  -v $(pwd)/client/config/client_config.yaml:/app/config/client_config.yaml:ro \
  -p 8000:8000 \
  -e BACKEND_URL=https://backend.yourdomain.com \
  -e MQTT_BROKER_URL=mqtts://mqtt.yourdomain.com:8883 \
  teleop-client:latest
```

详细说明请参考 [docs/DISTRIBUTED_DEPLOYMENT.md](docs/DISTRIBUTED_DEPLOYMENT.md)

---

## 📖 文档导航

### 整理文档
- [工程整理指南](docs/REFACTORING_GUIDE.md) - 详细的整理计划和设计方案
- [整理完成总结](docs/REFACTORING_SUMMARY.md) - 整理成果和使用说明
- [配置迁移说明](CONFIG_MIGRATION.md) - 配置文件位置迁移详细说明

### 部署文档
- [分布式部署指南](docs/DISTRIBUTED_DEPLOYMENT.md) - 跨网络部署详细说明
- [配置指南](docs/CONFIGURATION_GUIDE.md) - 配置参数详细说明
- [配置总结](docs/CONFIGURATION_SUMMARY.md) - 所有配置参数汇总

### 模块文档
- [Backend README](backend/README.md) - Backend模块详细文档
- [Client README](client/README.md) - Client模块详细文档
- [Vehicle-side README](Vehicle-side/README.md) - Vehicle-side模块详细文档

### 其他文档
- [构建指南](BUILD_GUIDE.md) - 原有构建指南
- [项目规格](project_spec.md) - 系统需求和设计规范

---

## 🔍 验证测试

### 快速验证

```bash
# 1. 编译所有模块
./scripts/build-all.sh

# 2. 验证编译状态
./scripts/verify-all.sh

# 3. 构建Docker镜像
./scripts/deploy-all.sh

# 4. 启动完整链路
docker compose up -d

# 5. 执行E2E测试
./scripts/e2e.sh

# 6. 停止服务
docker compose down
```

### 服务健康检查

```bash
# Backend
curl http://localhost:8000/health

# Vehicle-side
curl http://localhost:9000/health

# Keycloak
curl http://localhost:8080/health/ready

# 完整链路
docker compose ps
```

### 配置文件验证

```bash
# 验证 YAML 语法
yamllint backend/config/backend_config.yaml
yamllint client/config/client_config.yaml
yamllint Vehicle-side/config/vehicle_config.yaml
```

---

## ⚙️ 配置参数

### 关键环境变量

| 模块 | 环境变量 | 默认值 | 说明 |
|------|----------|--------|------|
| Backend | DATABASE_URL | postgresql://postgres:postgres@localhost:5432/teleop | 数据库连接 |
| Backend | KEYCLOAK_URL | http://keycloak:8080 | Keycloak服务 |
| Backend | ZLM_PUBLIC_BASE | http://localhost:80 | ZLM对外地址（分布式部署关键） |
| Client | BACKEND_URL | http://localhost:8000 | Backend服务 |
| Vehicle-side | MQTT_BROKER_URL | mqtt://mosquitto:1883 | MQTT Broker |

### 端口映射

| 服务 | 端口 | 用途 |
|------|------|------|
| Backend | 8000 | HTTP API |
| Keycloak | 8080 | 认证服务 |
| MQTT Broker | 1883 | MQTT |
| ZLMediaKit | 80 | HTTP/WHEP |
| ZLMediaKit | 1935 | RTMP |
| Vehicle-side | 9000 | HTTP健康检查 |

---

## 🐛 常见问题

### Q1: 编译失败

**问题**: 找不到依赖或CMake错误

**解决**:
```bash
# 检查共享依赖
ls deps/

# 清理构建目录
cd <module>
rm -rf build
./scripts/build.sh
```

### Q2: 配置文件找不到

**问题**: 容器启动时提示找不到配置文件

**解决**:
```bash
# 确认配置文件存在
ls backend/config/backend_config.yaml
ls client/config/client_config.yaml
ls Vehicle-side/config/vehicle_config.yaml

# 确认符号链接存在
readlink config/backend_config.yaml

# 检查挂载路径
docker inspect <container_name> | grep -A 10 Mounts
```

### Q3: Docker镜像构建失败

**问题**: 依赖安装失败或编译错误

**解决**:
```bash
# 清理Docker缓存
docker system prune -a

# 重新构建
./scripts/deploy-all.sh
```

### Q4: 服务启动失败

**问题**: 容器无法启动或健康检查失败

**解决**:
```bash
# 查看日志
docker compose logs <service>

# 检查端口占用
lsof -i :<port>

# 检查网络
docker network ls
docker network inspect teleop-network
```

### Q5: 跨网络部署连接失败

**问题**: Client无法连接Backend或MQTT

**解决**:
1. 检查防火墙规则
2. 确认环境变量配置正确（使用公网IP或域名）
3. 检查DNS解析
4. 使用 `telnet <host> <port>` 测试连通性

---

## 📞 获取帮助

### 查看日志

```bash
# Backend日志
docker compose logs -f backend

# Client日志
docker compose logs -f client-dev

# Vehicle-side日志
docker compose logs -f vehicle

# 所有日志
docker compose logs -f
```

### 诊断问题

```bash
# 系统诊断
./scripts/analyze.sh

# 自动诊断
python scripts/auto_diagnose.py
```

### 文档和资源

- [项目规格](project_spec.md) - 系统需求和设计
- [TROUBLESHOOTING_RUNBOOK.md](docs/TROUBLESHOOTING_RUNBOOK.md) - 故障排查手册
- [GitHub Issues](https://github.com/your-repo/issues) - 问题报告

---

## 🎯 下一步

1. ✅ 完成5分钟快速开始
2. ✅ 阅读各模块README了解详细功能
3. ✅ 尝试独立部署各模块
4. ✅ 配置分布式部署（如需要）
5. ✅ 执行完整的E2E测试
6. ✅ 开始开发和定制

---

## 📋 配置文件清单

### 根目录配置（符号链接）

```bash
config/
├── backend_config.yaml -> ../backend/config/backend_config.yaml
├── client_config.yaml -> ../client/config/client_config.yaml
└── vehicle_config.yaml -> ../Vehicle-side/config/vehicle_config.yaml
```

### 各模块配置（实际文件）

```bash
backend/config/
├── backend_config.yaml         # Backend配置

client/config/
├── client_config.yaml          # Client配置

Vehicle-side/config/
├── vehicle_config.yaml         # Vehicle-side配置
```

### 配置优先级

各模块的配置加载优先级（从高到低）：

1. 环境变量（最高优先级）
2. 命令行参数
3. 配置文件（`/app/config/<module>_config.yaml`）
4. 默认值

---

**版本**: v2.0
**更新日期**: 2026-02-28
**项目**: Remote-Driving
**主要变更**: 配置文件迁移到各模块config目录
