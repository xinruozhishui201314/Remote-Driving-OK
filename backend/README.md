# Backend 模块

远程驾驶系统后端服务，提供用户认证、车辆管理、会话控制等功能。

## 功能特性

- ✅ HTTP REST API（FastAPI/C++）
- ✅ JWT 认证（Keycloak集成）
- ✅ 车辆 VIN 管理
- ✅ 会话管理（控制锁、观察者）
- ✅ 数据库持久化（PostgreSQL）
- ✅ MQTT 消息转发
- ✅ 健康检查端点

## 项目结构

```
backend/
├── src/                      # C++ 源代码
│   ├── api/                  # API 处理器
│   │   ├── session_handler.cpp
│   │   └── vin_handler.cpp
│   ├── auth/                 # 认证模块
│   │   └── jwt_validator.cpp
│   ├── common/               # 公共工具
│   ├── db/                   # 数据库访问
│   ├── protocol/             # 协议定义
│   ├── telemetry/            # 遥测数据
│   ├── main.cpp              # 程序入口
│   └── health_handler.cpp   # 健康检查
├── scripts/                  # 构建和运行脚本
│   ├── build.sh             # 编译脚本
│   └── run.sh               # 运行脚本
├── CMakeLists.txt           # CMake 构建配置
├── Dockerfile               # Docker 镜像
└── README.md                # 本文档
```

## 依赖要求

### 必需依赖

- **CMake 3.10+**
- **C++17 编译器** (GCC 7+, Clang 5+)
- **PostgreSQL** (libpq-dev)
- **cpp-httplib** - HTTP 服务器库（从 ../deps 获取）
- **nlohmann/json** - JSON 库（从 ../deps 获取）

### 可选依赖

- **Mosquitto** - MQTT 客户端库

## 编译说明

### 一键编译

```bash
cd backend
./scripts/build.sh
```

### 手动编译

```bash
cd backend
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 编译参数

- `BUILD_TYPE`: 构建类型（Debug/Release，默认 Release）
- `CMAKE_ARGS_EXTRA`: 额外的 CMake 参数

示例：
```bash
BUILD_TYPE=Debug ./scripts/build.sh
```

## 运行说明

### 一键运行

```bash
cd backend
./scripts/run.sh
```

### 手动运行

```bash
cd backend/build
./teleop_backend
```

### 环境变量

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `DATABASE_URL` | PostgreSQL 数据库连接字符串 | `postgresql://postgres:postgres@localhost:5432/teleop` |
| `KEYCLOAK_URL` | Keycloak 服务地址 | `http://keycloak:8080` |
| `KEYCLOAK_ISSUER` | JWT 颁发者 | `http://keycloak:8080/realms/teleop` |
| `ZLM_API_URL` | ZLMediaKit 内部 API 地址 | `http://zlmediakit/index/api` |
| `ZLM_PUBLIC_BASE` | ZLMediaKit 对外访问地址 | `http://localhost:80` |
| `MQTT_BROKER_URL` | MQTT Broker 地址 | `mqtt://mosquitto:1883` |

## Docker 部署

### 构建镜像

```bash
cd backend
docker build -t teleop-backend:latest .
```

### 运行容器

```bash
docker run -d \
  -p 8000:8000 \
  -e DATABASE_URL=postgresql://postgres:postgres@postgres:5432/teleop \
  -e KEYCLOAK_URL=http://keycloak:8080 \
  -e ZLM_API_URL=http://zlmediakit/index/api \
  teleop-backend:latest
```

### 使用 Docker Compose

Backend 目录包含独立的 `docker-compose.yml` 用于独立部署：

```bash
cd backend
docker compose up -d
```

## API 端点

### 健康检查

- `GET /health` - 服务健康状态
- `GET /ready` - 依赖服务就绪状态

### 用户认证

- `GET /api/v1/me` - 获取当前用户信息（需要 JWT）

### 车辆管理

- `GET /api/v1/vins` - 获取车辆列表
- `POST /api/v1/vins` - 添加车辆

### 会话管理

- `POST /api/v1/vins/{vin}/sessions` - 创建会话
- `GET /api/v1/sessions/{sessionId}` - 获取会话信息
- `POST /api/v1/sessions/{sessionId}/lock` - 申请控制锁
- `DELETE /api/v1/sessions/{sessionId}` - 结束会话

## 测试

### 健康检查

```bash
curl http://localhost:8000/health
```

### 数据库连接测试

```bash
curl http://localhost:8000/ready
```

### 完整链路测试

```bash
cd ..
./scripts/verify-all.sh
```

## 故障排查

### 编译失败

1. 检查 CMake 版本：`cmake --version`
2. 检查依赖目录是否存在：`ls ../deps`
3. 检查 PostgreSQL 库是否安装：`dpkg -l | grep postgres`
4. 清理构建目录重试：`rm -rf build && ./scripts/build.sh`

### 运行失败

1. 检查可执行文件是否存在：`ls build/teleop_backend`
2. 检查端口是否被占用：`lsof -i :8000`
3. 检查数据库连接：`psql $DATABASE_URL -c "SELECT 1"`
4. 查看日志输出

### 数据库连接失败

1. 检查 PostgreSQL 是否运行：`docker ps | grep postgres`
2. 检查数据库 URL 格式
3. 检查网络连接：`telnet postgres 5432`

## 开发模式

### 热重载（TODO）

当前不支持热重载，修改代码后需要重新编译。

### 调试

```bash
# 使用 gdb 调试
cd backend/build
gdb ./teleop_backend

(gdb) break main
(gdb) run
```

## 日志

### 日志级别

- `DEBUG` - 详细调试信息
- `INFO` - 一般信息（默认）
- `WARN` - 警告信息
- `ERROR` - 错误信息

### 日志输出

日志输出到标准输出（stdout），可以通过 Docker 日志或 systemd journal 查看。

```bash
# Docker 日志
docker logs -f teleop-backend

# Systemd 日志
journalctl -u teleop-backend -f
```

## 性能优化

### 编译优化

- 使用 Release 模式编译：`BUILD_TYPE=Release ./scripts/build.sh`
- 启用链接时优化（LTO）：添加 `-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`

### 运行时优化

- 调整工作线程数（TODO）
- 启用连接池（TODO）
- 启用缓存（TODO）

## 安全考虑

- ✅ JWT 认证
- ✅ HTTPS/TLS 加密（生产环境）
- ✅ SQL 注入防护
- ⚠️ 速率限制（TODO）
- ⚠️ CORS 配置（生产环境需限制）

## 相关文档

- [工程整理指南](../docs/REFACTORING_GUIDE.md)
- [分布式部署指南](../docs/DISTRIBUTED_DEPLOYMENT.md)
- [配置指南](../docs/CONFIGURATION_GUIDE.md)
- [项目规格](../project_spec.md)

## 许可证

Copyright © 2026 Remote-Driving Team
