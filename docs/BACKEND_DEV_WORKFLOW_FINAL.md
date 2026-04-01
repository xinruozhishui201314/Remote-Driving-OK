# Backend 开发工作流（最终方案）

## 推荐方案：使用生产模式镜像 + 外部编译

### 快速开始

```bash
# 1. 确保生产模式镜像已构建（只需一次）
docker compose build backend

# 2. 启动所有服务（使用生产模式）
docker compose up -d

# 3. 修改代码后，重新构建并重启
docker compose build backend && docker compose restart backend
```

### 工作流程

1. **修改代码**：编辑 `backend/src/` 下的文件
2. **重新构建镜像**：`docker compose build backend`（只构建 backend 服务）
3. **重启服务**：`docker compose restart backend`
4. **测试**：使用 `./scripts/test-client-backend-integration.sh`

### 优点

- ✅ **简单可靠**：使用标准的 Docker 构建流程
- ✅ **无需额外配置**：不需要处理权限、挂载等问题
- ✅ **快速迭代**：只构建 backend 服务，其他服务不受影响
- ✅ **生产一致性**：开发环境和生产环境使用相同的镜像

### 构建时间优化

- **首次构建**：约 2-3 分钟（下载依赖项）
- **增量构建**：约 10-30 秒（只编译变更的文件）
- **Docker 缓存**：未变更的层会被缓存，加快构建速度

### 常用命令

```bash
# 查看编译日志
docker compose build backend --progress=plain

# 查看运行日志
docker compose logs -f backend

# 进入容器调试
docker compose exec backend sh

# 测试 API
./scripts/test-client-backend-integration.sh
```

## 备选方案对比

### 方案 A：生产模式 + 外部编译（当前推荐）

```bash
docker compose build backend && docker compose restart backend
```

**优点**：
- 简单可靠
- 无需处理权限问题
- 生产一致性

**缺点**：
- 需要重建镜像（但 Docker 缓存使其很快）

### 方案 B：开发模式 + 容器内编译

```bash
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d backend
```

**优点**：
- 自动监控文件变化
- 无需手动重建

**缺点**：
- 首次编译慢（下载依赖）
- 镜像较大
- 可能遇到网络问题

### 方案 C：外部编译 + 挂载二进制（实验性）

```bash
./scripts/compile-and-run-backend.sh
```

**优点**：
- 编译速度快
- 无需重建镜像

**缺点**：
- 权限问题复杂
- 需要主机有编译工具
- 架构兼容性问题

## 推荐工作流

### 日常开发

```bash
# 1. 启动所有服务
docker compose up -d

# 2. 修改代码（在编辑器中）

# 3. 重新构建并重启
docker compose build backend && docker compose restart backend

# 4. 查看日志确认启动
docker compose logs backend --tail=20

# 5. 测试
./scripts/test-client-backend-integration.sh
```

### 快速迭代脚本

创建一个 `rebuild.sh`：

```bash
#!/bin/bash
cd "$(dirname "$0")"
echo "构建 Backend..."
docker compose build backend
echo "重启 Backend..."
docker compose restart backend
echo "等待启动..."
sleep 3
echo "查看日志..."
docker compose logs backend --tail=10
```

## 故障排查

### 构建失败

```bash
# 查看详细构建日志
docker compose build backend --progress=plain --no-cache

# 清理构建缓存
docker builder prune

# 重新构建
docker compose build --no-cache backend
```

### 服务启动失败

```bash
# 查看日志
docker compose logs backend

# 检查容器状态
docker compose ps backend

# 进入容器调试
docker compose exec backend sh
```

### 代码修改未生效

```bash
# 确认代码已保存
# 确认镜像已重新构建
docker compose build backend

# 确认服务已重启
docker compose restart backend

# 查看运行日志
docker compose logs backend -f
```

## 性能建议

1. **使用 Docker BuildKit**：加速构建
   ```bash
   export DOCKER_BUILDKIT=1
   docker compose build backend
   ```

2. **利用缓存**：按依赖顺序组织 Dockerfile，将不常变更的层放在前面

3. **并行构建**：如果有多个服务，可以并行构建
   ```bash
   docker compose build --parallel
   ```

4. **增量编译**：CMake 会自动进行增量编译，只编译变更的文件

## 总结

**推荐使用方案 A（生产模式 + 外部编译）**，因为：
- 最简单可靠
- 构建速度快（Docker 缓存）
- 与生产环境一致
- 无需额外配置

对于大多数开发场景，`docker compose build backend && docker compose restart backend` 已经足够快速和方便。
