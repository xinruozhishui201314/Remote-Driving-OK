# Backend 容器内编译开发 - 总结

## ✅ 已完成的功能

### 1. 详细的日志输出

- **网络检查日志**：显示 DNS 配置、代理设置、连接测试结果
- **下载进度日志**：显示每个依赖项的下载状态
- **编译日志**：显示详细的编译过程和错误信息

### 2. 网络诊断功能

- 自动检查 DNS 解析
- 测试 GitHub 连接
- 显示网络配置信息
- 提供故障排查建议

### 3. 错误处理

- 详细的错误信息输出
- 日志文件保存（`/tmp/cmake-config.log`, `/tmp/cmake-build.log`）
- 常见问题提示

## 📋 使用方式

### 启动开发环境

```bash
# 构建开发镜像（只需一次）
docker compose -f docker-compose.yml -f docker-compose.dev.yml build backend

# 启动开发环境
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d backend

# 查看日志（实时）
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs -f backend
```

### 修改代码

编辑 `backend/src/` 下的文件，容器会自动检测变化并重新编译。

### 查看详细日志

```bash
# 查看完整日志
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs backend

# 查看 CMake 配置日志
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend cat /tmp/cmake-config.log

# 查看编译日志
docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend cat /tmp/cmake-build.log
```

## 🔍 日志示例

### 网络检查日志

```
[12:14:35] === 检查网络连接 ===
[12:14:35] DNS 配置：
nameserver 127.0.0.11
[12:14:35] 代理配置：
  http_proxy=未设置
  https_proxy=未设置

[12:14:35] ❌ DNS 解析失败：无法解析 github.com
[12:14:35] 尝试使用 getent...
[12:14:35] ✓ 使用 getent 解析成功
[12:14:35] 测试 GitHub 连接（超时 10 秒）...
[12:14:42] ✓ GitHub 连接正常
[12:14:42] ✓ Git 可用: git version 2.34.1
```

### CMake 配置日志

```
[12:14:42] 开始 CMake 配置（这可能需要几分钟下载依赖项）...
-- ========================================
-- 开始下载依赖项...
-- ========================================
-- 正在下载 cpp-httplib (v0.14.3)...
-- 仓库: https://github.com/yhirose/cpp-httplib
-- 正在克隆 cpp-httplib 仓库...
-- cpp-httplib 下载完成: /tmp/backend-build/_deps/httplib-src
-- 正在下载 nlohmann_json (v3.11.3)...
-- 仓库: https://github.com/nlohmann/json
-- 正在克隆 nlohmann_json 仓库...
-- nlohmann_json 下载完成: /tmp/backend-build/_deps/nlohmann_json-src
```

## ⚠️ 已知问题

### 下载缓慢

如果依赖项下载缓慢（特别是 nlohmann_json），可能的原因：

1. **网络速度慢**：GitHub 在某些地区访问较慢
2. **仓库较大**：nlohmann_json 仓库包含大量历史记录

**解决方案**：

1. **使用代理**：
   ```yaml
   # docker-compose.dev.yml
   services:
     backend:
       environment:
         - http_proxy=http://your-proxy:port
         - https_proxy=http://your-proxy:port
   ```

2. **使用 Git 镜像**：
   ```bash
   docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend \
     git config --global url."https://mirror.ghproxy.com/https://github.com/".insteadOf "https://github.com/"
   ```

3. **使用浅克隆**：已在 CMakeLists.txt 中配置 `GIT_SHALLOW TRUE`

## 📚 相关文档

- `docs/BACKEND_DEV_CONTAINER.md`：完整的开发指南
- `backend/docker-entrypoint-dev.sh`：开发模式入口脚本
- `backend/CMakeLists.txt`：CMake 配置文件

## 🎯 下一步优化

1. **添加下载超时**：如果下载超过一定时间，显示警告
2. **添加下载进度**：显示 Git clone 的进度
3. **支持离线模式**：如果依赖项已存在，跳过下载
4. **优化错误信息**：提供更具体的错误原因和解决方案
