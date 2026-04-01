# Backend 开发工作流

## 概述

本文档描述如何在**不频繁重建 Docker 镜像**的情况下进行 Backend 开发，实现"修改代码 → 编译 → 运行"的快速迭代。

## 快速开始

### 1. 首次设置（只需一次）

```bash
# 构建开发模式镜像（包含编译工具和运行时依赖）
docker compose -f docker-compose.yml -f docker-compose.dev.yml build backend
```

### 2. 日常开发流程

#### 方式 A：使用便捷脚本（推荐）

```bash
# 启动开发环境并查看日志
./scripts/dev-backend-fast.sh

# 修改代码后，强制重新编译
./scripts/rebuild-backend.sh

# 或者清理缓存后重新编译
./scripts/rebuild-backend.sh --clean
```

#### 方式 B：手动操作

```bash
# 启动服务（如果未运行）
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d backend

# 修改代码后，重启容器（会自动重新编译）
docker compose -f docker-compose.yml -f docker-compose.dev.yml restart backend

# 查看编译日志
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs -f backend
```

## 工作原理

### 开发模式配置

- **镜像**：`Dockerfile.dev` 构建的镜像，包含：
  - 编译工具：`cmake`, `make`, `g++`
  - 运行时依赖：`libpq5`
  - 开发工具：`inotify-tools`（用于文件监控）

- **源码挂载**：
  - `./backend/src` → `/app/src`（可写）
  - `./backend/CMakeLists.txt` → `/app/CMakeLists.txt`（可写）

- **构建目录**：
  - `backend_build` volume → `/app/build`（持久化，加快增量编译）

- **入口脚本**：
  - `/docker-entrypoint-dev.sh`：自动编译并运行，监控文件变化自动重新编译

### 编译流程

1. 容器启动时执行 `/docker-entrypoint-dev.sh`
2. 脚本检测到 `/app/src` 或 `/app/CMakeLists.txt` 变化
3. 在 `/tmp/backend-build` 目录执行 CMake 配置和编译
4. 编译成功后启动 `/tmp/backend-build/bin/teleop_backend`
5. 使用 `inotifywait` 监控源码变化，自动重新编译

## 常见操作

### 查看编译状态

```bash
# 查看容器进程
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend ps aux | grep teleop_backend

# 查看编译日志
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs backend --tail=50

# 检查可执行文件是否存在
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend ls -la /tmp/backend-build/bin/
```

### 手动触发重新编译

```bash
# 方法1：重启容器（推荐）
docker compose -f docker-compose.yml -f docker-compose.dev.yml restart backend

# 方法2：在容器内手动编译
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend sh -c "
  cd /tmp/backend-build && \
  cmake /app -DCMAKE_BUILD_TYPE=Release && \
  cmake --build . --target teleop_backend
"

# 方法3：清理缓存后重新编译
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend sh -c "rm -rf /tmp/backend-build"
docker compose -f docker-compose.yml -f docker-compose.dev.yml restart backend
```

### 测试 API

```bash
# 运行端到端测试
./scripts/test-client-backend-integration.sh

# 手动测试会话创建
TOKEN=$(curl -s -X POST "http://localhost:8080/realms/teleop/protocol/openid-connect/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password" \
  -d "client_id=teleop-client" \
  -d "username=e2e-test" \
  -d "password=e2e-test-password" | \
  python3 -c "import sys, json; print(json.load(sys.stdin).get('access_token', ''))")

curl -s -X POST \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  "http://localhost:8081/api/v1/vins/E2ETESTVIN0000001/sessions" | \
  python3 -m json.tool
```

## 故障排查

### 编译卡住

如果 CMake 配置阶段卡住（通常在下载依赖项时），可以：

1. **检查网络连接**：CMake 需要从 GitHub 下载 `cpp-httplib` 和 `nlohmann_json`
2. **使用代理**：在容器内设置代理环境变量
3. **手动下载依赖**：修改 `CMakeLists.txt` 使用本地依赖

### 编译失败

```bash
# 查看详细错误信息
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs backend

# 进入容器调试
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend bash
cd /tmp/backend-build
cmake /app -DCMAKE_BUILD_TYPE=Release
cmake --build . --target teleop_backend
```

### 代码修改未生效

1. **确认源码已挂载**：
   ```bash
   docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend ls -la /app/src/main.cpp
   ```

2. **检查文件监控是否工作**：
   ```bash
   docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend ps aux | grep inotifywait
   ```

3. **手动触发重新编译**：重启容器

### 端口冲突

如果 8081 端口被占用：

```bash
# 修改 docker-compose.yml 中的端口映射
# 将 "8081:8080" 改为其他端口，如 "8082:8080"
```

## 性能优化

### 加快编译速度

1. **使用增量编译**：构建目录持久化在 `backend_build` volume 中
2. **并行编译**：CMake 默认使用多线程编译
3. **减少依赖下载**：首次编译后，依赖项会缓存

### 减少资源占用

- 开发模式镜像较大（包含编译工具），如果资源紧张，可以考虑：
  - 使用生产模式镜像 + 外部编译
  - 或者使用更轻量的编译工具链

## 与生产模式的区别

| 特性 | 开发模式 | 生产模式 |
|------|---------|---------|
| 镜像大小 | ~200MB（包含编译工具） | ~85MB（仅运行时） |
| 源码挂载 | ✅ 是 | ❌ 否 |
| 自动编译 | ✅ 是 | ❌ 否 |
| 文件监控 | ✅ 是 | ❌ 否 |
| 构建速度 | 首次慢（下载依赖），后续快 | N/A |
| 适用场景 | 开发、调试 | 生产部署 |

## 最佳实践

1. **首次构建后，尽量不重建镜像**：只在 `Dockerfile.dev` 或依赖项变更时重建
2. **使用增量编译**：让构建目录持久化，加快编译速度
3. **监控日志**：使用 `logs -f` 实时查看编译和运行状态
4. **定期清理**：如果编译出现问题，使用 `--clean` 选项清理缓存

## 相关文件

- `backend/Dockerfile.dev`：开发模式镜像定义
- `backend/docker-entrypoint-dev.sh`：开发模式入口脚本
- `docker-compose.dev.yml`：开发模式配置覆盖
- `scripts/dev-backend-fast.sh`：快速开发脚本
- `scripts/rebuild-backend.sh`：强制重新编译脚本
