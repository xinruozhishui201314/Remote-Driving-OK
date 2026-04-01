# Backend 依赖项本地化方案

## 概述

为了避免在容器内下载依赖项（特别是网络较慢时），可以在宿主机上下载依赖项，然后挂载到容器中使用。

## 快速开始

### 1. 下载依赖项到本地

```bash
# 在宿主机上下载依赖项
./scripts/prepare-backend-deps.sh
```

这个脚本会：
- 下载 `cpp-httplib` (v0.14.3) 到 `backend/deps/cpp-httplib`
- 下载 `nlohmann_json` (v3.11.3) 到 `backend/deps/nlohmann_json`

### 2. 启动开发环境

```bash
# 启动 Backend（会自动使用本地依赖项）
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d backend

# 查看日志（应该看到"使用本地 xxx"的消息）
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs -f backend
```

## 工作原理

### CMakeLists.txt 逻辑

CMakeLists.txt 会按以下顺序检查依赖项：

1. **优先使用本地依赖项**：
   - 检查 `backend/deps/cpp-httplib/httplib.h` 是否存在
   - 检查 `backend/deps/nlohmann_json/include/nlohmann/json.hpp` 是否存在

2. **如果本地不存在，则从 GitHub 下载**：
   - 使用 `FetchContent` 从 GitHub 下载
   - 显示详细的下载进度

### docker-compose.dev.yml 配置

```yaml
volumes:
  # 挂载本地依赖项到容器
  - ./backend/deps:/app/deps:ro
```

依赖项目录以只读模式挂载，避免意外修改。

## 日志示例

### 使用本地依赖项

```
-- ========================================
-- 检查依赖项...
-- ========================================
-- 使用本地 cpp-httplib: /app/deps/cpp-httplib
-- 使用本地 nlohmann_json: /app/deps/nlohmann_json
-- ========================================
-- 依赖项检查完成
-- ========================================
```

### 从 GitHub 下载（如果本地不存在）

```
-- 本地 cpp-httplib 不存在，从 GitHub 下载...
-- 仓库: https://github.com/yhirose/cpp-httplib
-- 正在克隆 cpp-httplib 仓库...
-- cpp-httplib 下载完成: /tmp/backend-build/_deps/httplib-src
```

## 优势

1. **速度快**：宿主机下载通常比容器内下载快
2. **可重复使用**：下载一次，多次使用
3. **离线可用**：如果网络有问题，可以使用已下载的依赖项
4. **版本控制**：可以将 `backend/deps` 添加到 `.gitignore`，或提交到仓库

## 更新依赖项

如果需要更新依赖项版本：

```bash
# 删除旧版本
rm -rf backend/deps/cpp-httplib backend/deps/nlohmann_json

# 重新下载
./scripts/prepare-backend-deps.sh
```

或者修改 `scripts/prepare-backend-deps.sh` 中的版本号，然后重新运行。

## 目录结构

```
backend/
├── deps/                    # 本地依赖项目录
│   ├── cpp-httplib/        # cpp-httplib 源码
│   │   └── httplib.h
│   └── nlohmann_json/      # nlohmann_json 源码
│       └── include/
│           └── nlohmann/
│               └── json.hpp
├── src/                     # 源码
└── CMakeLists.txt          # CMake 配置
```

## 故障排查

### 依赖项未找到

如果看到"本地 xxx 不存在，从 GitHub 下载..."，检查：

```bash
# 检查依赖项是否存在
ls -la backend/deps/cpp-httplib/httplib.h
ls -la backend/deps/nlohmann_json/include/nlohmann/json.hpp

# 检查挂载是否正确
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend ls -la /app/deps/
```

### 编译错误

如果使用本地依赖项后编译失败：

1. **检查依赖项版本**：
   ```bash
   cd backend/deps/cpp-httplib && git describe --tags
   cd backend/deps/nlohmann_json && git describe --tags
   ```

2. **清理并重新下载**：
   ```bash
   rm -rf backend/deps/*
   ./scripts/prepare-backend-deps.sh
   ```

3. **查看编译错误**：
   ```bash
   docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend cat /tmp/cmake-build.log
   ```

## 最佳实践

1. **首次设置时下载依赖项**：在启动开发环境前运行 `prepare-backend-deps.sh`
2. **版本控制**：考虑将 `backend/deps` 添加到 `.gitignore`（依赖项较大）
3. **定期更新**：定期检查依赖项是否有安全更新
4. **文档化**：在 README 中说明依赖项版本

## 相关文件

- `scripts/prepare-backend-deps.sh`：下载依赖项脚本
- `backend/CMakeLists.txt`：CMake 配置（支持本地依赖项）
- `docker-compose.dev.yml`：开发模式配置（挂载依赖项目录）
