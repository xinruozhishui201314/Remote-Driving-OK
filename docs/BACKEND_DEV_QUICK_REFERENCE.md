# Backend 容器内编译开发 - 快速参考

## 🚀 快速开始

```bash
# 1. 构建开发镜像（只需一次）
docker compose -f docker-compose.yml -f docker-compose.dev.yml build backend

# 2. 启动开发环境
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d backend

# 3. 查看日志（实时监控编译进度）
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs -f backend
```

## 📊 日志说明

### 正常流程日志

1. **网络检查阶段**：
   ```
   [时间] === 检查网络连接 ===
   [时间] DNS 配置：...
   [时间] ✓ DNS 解析正常
   [时间] ✓ GitHub 连接正常
   [时间] ✓ Git 可用
   ```

2. **CMake 配置阶段**：
   ```
   [时间] 开始 CMake 配置（这可能需要几分钟下载依赖项）...
   -- 正在下载 cpp-httplib (v0.14.3)...
   -- cpp-httplib 下载完成
   -- 正在下载 nlohmann_json (v3.11.3)...
   -- nlohmann_json 下载完成
   ```

3. **编译阶段**：
   ```
   [时间] === 构建 Backend ===
   [ 33%] Building CXX object ...
   [ 66%] Building CXX object ...
   [100%] Linking CXX executable bin/teleop_backend
   [时间] ✓ 编译完成
   ```

4. **启动阶段**：
   ```
   [时间] === 启动 Backend ===
   [时间] === Backend PID: <pid> ===
   ```

### 下载缓慢时的日志

如果下载缓慢，你会看到：

```
-- 正在克隆 nlohmann_json 仓库...
```

此时 Git 进程正在后台下载，日志可能暂时没有新输出。这是正常的，请耐心等待。

**检查下载进度**：
```bash
# 查看 Git 进程
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend ps aux | grep git

# 查看下载目录大小变化
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend du -sh /tmp/backend-build/_deps/nlohmann_json-src
```

## ⚠️ 常见问题

### 1. 下载卡住

**症状**：日志显示"正在克隆 nlohmann_json 仓库..."但长时间无进展

**检查**：
```bash
# 检查 Git 进程是否还在运行
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend ps aux | grep git

# 检查网络连接
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend curl -I https://github.com
```

**解决**：
- 如果网络正常，继续等待（nlohmann_json 仓库较大）
- 如果网络异常，配置代理或使用镜像源

### 2. 网络检查失败但实际可以下载

**症状**：显示"DNS 解析失败"但后续下载成功

**说明**：这是正常的，脚本会尝试多种方法（nslookup → getent → 直接连接）

### 3. 编译失败

**查看详细错误**：
```bash
# CMake 配置错误
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend cat /tmp/cmake-config.log

# 编译错误
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend cat /tmp/cmake-build.log
```

## 🔧 常用命令

```bash
# 重启容器（重新编译）
docker compose -f docker-compose.yml -f docker-compose.dev.yml restart backend

# 清理构建缓存后重新编译
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend rm -rf /tmp/backend-build
docker compose -f docker-compose.yml -f docker-compose.dev.yml restart backend

# 进入容器调试
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend bash

# 检查服务状态
docker compose -f docker-compose.yml -f docker-compose.dev.yml ps backend

# 测试 API
./scripts/test-client-backend-integration.sh
```

## 📝 修改代码后的流程

1. **编辑代码**：修改 `backend/src/` 下的文件
2. **自动检测**：容器内的 `inotifywait` 会自动检测文件变化
3. **自动重新编译**：检测到变化后自动重新编译
4. **自动重启**：编译成功后自动重启 Backend

**手动触发**（如果需要）：
```bash
docker compose -f docker-compose.yml -f docker-compose.dev.yml restart backend
```

## 🎯 性能提示

- **首次编译**：需要下载依赖项，可能需要 5-10 分钟
- **后续编译**：只编译变更的文件，通常 10-30 秒
- **下载速度**：取决于网络，如果慢可以配置代理

## 📚 更多信息

- 完整指南：`docs/BACKEND_DEV_CONTAINER.md`
- 开发总结：`docs/BACKEND_DEV_SUMMARY.md`
