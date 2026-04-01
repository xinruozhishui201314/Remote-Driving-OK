# Backend 开发环境设置指南

## 快速开始（推荐）

### 1. 准备依赖项（只需一次）

```bash
# 在宿主机上下载依赖项到本地
./scripts/prepare-backend-deps.sh
```

这会下载：
- `cpp-httplib` (v0.14.3) → `backend/deps/cpp-httplib`
- `nlohmann_json` (v3.11.3) → `backend/deps/nlohmann_json`

### 2. 启动开发环境

```bash
# 构建开发镜像（只需一次）
docker compose -f docker-compose.yml -f docker-compose.dev.yml build backend

# 启动开发环境
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d backend

# 查看日志（实时监控）
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs -f backend
```

### 3. 开发流程

1. **修改代码**：编辑 `backend/src/` 下的文件
2. **自动编译**：容器会自动检测变化并重新编译
3. **查看日志**：`docker compose -f docker-compose.yml -f docker-compose.dev.yml logs -f backend`
4. **测试 API**：`./scripts/test-client-backend-integration.sh`

## 优势

### 使用本地依赖项的优势

✅ **速度快**：CMake 配置阶段几乎瞬间完成（无需下载）  
✅ **离线可用**：即使网络有问题也能编译  
✅ **可重复使用**：下载一次，多次使用  
✅ **详细日志**：显示使用的是本地还是远程依赖项

### 对比

| 方式 | CMake 配置时间 | 网络依赖 | 适用场景 |
|------|---------------|---------|---------|
| 本地依赖项 | ~1 秒 | 无 | ✅ 推荐 |
| 容器内下载 | 2-10 分钟 | 有 | 网络快时可用 |

## 日志示例

### 使用本地依赖项（快速）

```
-- ========================================
-- 检查依赖项...
-- ========================================
-- 使用本地 cpp-httplib: /app/deps/cpp-httplib
-- 使用本地 nlohmann_json: /app/deps/nlohmann_json
-- ========================================
-- 依赖项检查完成
-- ========================================
-- Configuring done
-- Generating done
[12:28:57] ✓ CMake 配置成功
[12:28:57] === 构建 Backend ===
[ 33%] Building CXX object ...
[ 66%] Building CXX object ...
[100%] Linking CXX executable bin/teleop_backend
[12:29:20] ✓ 编译完成
```

### 从 GitHub 下载（如果本地不存在）

```
-- 本地 cpp-httplib 不存在，从 GitHub 下载...
-- 仓库: https://github.com/yhirose/cpp-httplib
-- 正在克隆 cpp-httplib 仓库...
（等待下载...）
-- cpp-httplib 下载完成
```

## 更新依赖项

如果需要更新依赖项版本：

```bash
# 删除旧版本
rm -rf backend/deps/*

# 重新下载（或修改脚本中的版本号）
./scripts/prepare-backend-deps.sh
```

## 故障排查

### 依赖项未找到

如果看到"本地 xxx 不存在，从 GitHub 下载..."：

```bash
# 检查依赖项是否存在
ls -la backend/deps/cpp-httplib/httplib.h
ls -la backend/deps/nlohmann_json/include/nlohmann/json.hpp

# 如果不存在，重新下载
./scripts/prepare-backend-deps.sh

# 检查容器内挂载
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend ls -la /app/deps/
```

### 编译错误

```bash
# 查看详细编译日志
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend cat /tmp/cmake-build.log

# 清理构建缓存
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend rm -rf /tmp/backend-build
docker compose -f docker-compose.yml -f docker-compose.dev.yml restart backend
```

## 相关文档

- `docs/BACKEND_DEPS_LOCAL.md`：本地依赖项详细说明
- `docs/BACKEND_DEV_CONTAINER.md`：容器内编译完整指南
- `scripts/prepare-backend-deps.sh`：依赖项下载脚本
