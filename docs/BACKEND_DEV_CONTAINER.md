# Backend 容器内编译开发指南

## 概述

本指南描述如何在 Docker 容器内进行 Backend 开发，实现"修改代码 → 自动编译 → 运行"的快速迭代。

## 快速开始

### 1. 首次设置（只需一次）

```bash
# 构建开发模式镜像
docker compose -f docker-compose.yml -f docker-compose.dev.yml build backend
```

### 2. 启动开发环境

```bash
# 启动 Backend（会自动编译并运行）
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d backend

# 查看编译日志
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs -f backend
```

### 3. 修改代码

编辑 `backend/src/` 下的文件，容器会自动检测变化并重新编译。

## 工作原理

### 开发模式特性

- **源码挂载**：`./backend/src` 和 `./backend/CMakeLists.txt` 挂载到容器
- **自动编译**：容器启动时自动编译，源码变化时自动重新编译
- **详细日志**：显示下载进度、网络状态、编译错误等详细信息

### 编译流程

1. **网络检查**：检查 DNS、GitHub 连接、Git 可用性
2. **CMake 配置**：下载依赖项（cpp-httplib, nlohmann_json）
3. **编译**：使用 CMake 编译 Backend
4. **运行**：启动编译好的二进制文件
5. **监控**：使用 `inotifywait` 监控源码变化

## 详细日志说明

### 网络检查日志

```
[12:11:35] === 检查网络连接 ===
[12:11:35] DNS 配置：
  nameserver 127.0.0.11
[12:11:35] 代理配置：
  http_proxy=未设置
  https_proxy=未设置
[12:11:35] ✓ DNS 解析正常
[12:11:35] ✓ GitHub 连接正常
[12:11:35] ✓ Git 可用: git version 2.34.1
```

### CMake 配置日志

```
[12:11:35] === 配置 CMake ===
[12:11:35] 这将下载以下依赖项：
[12:11:35]   - cpp-httplib (v0.14.3) from https://github.com/yhirose/cpp-httplib
[12:11:35]   - nlohmann_json (v3.11.3) from https://github.com/nlohmann/json
[12:11:35] 如果下载缓慢，请耐心等待...

-- 正在下载 cpp-httplib (v0.14.3)...
-- 正在克隆 cpp-httplib 仓库...
-- cpp-httplib 下载完成: /tmp/backend-build/_deps/httplib-src
-- 正在下载 nlohmann_json (v3.11.3)...
-- 正在克隆 nlohmann_json 仓库...
-- nlohmann_json 下载完成: /tmp/backend-build/_deps/nlohmann_json-src
```

### 编译日志

```
[12:11:40] === 构建 Backend ===
[ 33%] Building CXX object CMakeFiles/teleop_backend.dir/src/main.cpp.o
[ 66%] Building CXX object CMakeFiles/teleop_backend.dir/src/auth/jwt_validator.cpp.o
[100%] Linking CXX executable bin/teleop_backend
[12:11:45] ✓ 编译完成
```

## 常见问题

### 1. 网络连接问题

**症状**：DNS 解析失败或无法连接到 GitHub

**解决方案**：

```bash
# 检查容器网络
docker network inspect teleop-network

# 检查 DNS 配置
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend cat /etc/resolv.conf

# 如果需要代理，在 docker-compose.dev.yml 中添加：
services:
  backend:
    environment:
      - http_proxy=http://proxy.example.com:8080
      - https_proxy=http://proxy.example.com:8080
```

### 2. 下载缓慢

**症状**：CMake 配置阶段卡住，长时间无输出

**原因**：从 GitHub 下载依赖项速度慢

**解决方案**：

1. **使用代理**（推荐）：
   ```bash
   # 在 docker-compose.dev.yml 中配置代理
   services:
     backend:
       environment:
         - http_proxy=http://your-proxy:port
         - https_proxy=http://your-proxy:port
   ```

2. **使用 Git 镜像**：
   ```bash
   # 在容器内配置 Git 使用镜像
   docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend \
     git config --global url."https://mirror.ghproxy.com/https://github.com/".insteadOf "https://github.com/"
   ```

3. **手动下载依赖**：
   ```bash
   # 在主机上下载依赖，然后挂载到容器
   git clone https://github.com/yhirose/cpp-httplib.git backend/deps/cpp-httplib
   git clone https://github.com/nlohmann/json.git backend/deps/nlohmann_json
   ```

### 3. 编译失败

**查看详细错误**：

```bash
# 查看完整日志
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs backend

# 查看 CMake 配置日志
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend cat /tmp/cmake-config.log

# 查看编译日志
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend cat /tmp/cmake-build.log
```

**常见错误**：

- **找不到 PostgreSQL**：确保容器内安装了 `libpq-dev`
- **链接错误**：检查 `CMakeLists.txt` 中的库链接配置
- **C++ 标准错误**：确保使用 C++17 标准

### 4. 代码修改未生效

**检查文件监控**：

```bash
# 检查 inotifywait 是否运行
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend ps aux | grep inotifywait

# 手动触发重新编译
docker compose -f docker-compose.yml -f docker-compose.dev.yml restart backend
```

## 调试技巧

### 进入容器调试

```bash
# 进入容器
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend bash

# 手动运行编译
cd /tmp/backend-build
cmake /app -DCMAKE_BUILD_TYPE=Release
cmake --build . --target teleop_backend

# 手动运行程序
/tmp/backend-build/bin/teleop_backend
```

### 查看详细输出

```bash
# 实时查看日志
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs -f backend

# 查看最后 100 行
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs backend --tail=100
```

### 清理重建

```bash
# 清理构建缓存
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend rm -rf /tmp/backend-build

# 重启容器（会重新编译）
docker compose -f docker-compose.yml -f docker-compose.dev.yml restart backend
```

## 性能优化

### 加快编译速度

1. **使用增量编译**：构建目录持久化在容器内
2. **并行编译**：CMake 默认使用多线程
3. **缓存依赖项**：首次下载后，依赖项会缓存

### 减少资源占用

- 开发模式镜像较大（~200MB），包含编译工具
- 如果资源紧张，可以考虑使用外部编译方案

## 与生产模式的区别

| 特性 | 开发模式 | 生产模式 |
|------|---------|---------|
| 镜像大小 | ~200MB | ~85MB |
| 源码挂载 | ✅ 是 | ❌ 否 |
| 自动编译 | ✅ 是 | ❌ 否 |
| 文件监控 | ✅ 是 | ❌ 否 |
| 详细日志 | ✅ 是 | ⚠️ 基础 |
| 适用场景 | 开发、调试 | 生产部署 |

## 最佳实践

1. **首次构建后，尽量不重建镜像**：只在 Dockerfile.dev 变更时重建
2. **监控日志**：使用 `logs -f` 实时查看编译和运行状态
3. **网络问题及时处理**：配置代理或使用镜像源
4. **定期清理**：如果编译出现问题，清理构建缓存

## 相关文件

- `backend/Dockerfile.dev`：开发模式镜像定义
- `backend/docker-entrypoint-dev.sh`：开发模式入口脚本
- `docker-compose.dev.yml`：开发模式配置覆盖
- `backend/CMakeLists.txt`：CMake 配置文件
