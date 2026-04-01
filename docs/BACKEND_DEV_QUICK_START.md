# Backend 开发快速开始

## 最简单的开发流程

### 1. 首次设置（只需一次）

```bash
# 构建所有服务
docker compose build
```

### 2. 日常开发流程

```bash
# 修改代码后，快速重建并重启
./scripts/rebuild-backend-fast.sh
```

就这么简单！

## 完整工作流示例

```bash
# 1. 启动所有服务
docker compose up -d

# 2. 修改代码（在编辑器中编辑 backend/src/main.cpp）

# 3. 快速重建
./scripts/rebuild-backend-fast.sh

# 4. 测试
./scripts/test-client-backend-integration.sh
```

## 常用命令

```bash
# 查看日志
docker compose logs -f backend

# 查看服务状态
docker compose ps backend

# 进入容器调试
docker compose exec backend sh

# 停止服务
docker compose stop backend

# 启动服务
docker compose up -d backend
```

## 性能说明

- **首次构建**：约 2-3 分钟（下载依赖项）
- **增量构建**：约 10-30 秒（Docker 缓存 + CMake 增量编译）
- **重启时间**：约 2-3 秒

## 为什么选择这个方案？

1. ✅ **简单**：只需一个命令
2. ✅ **快速**：Docker 缓存使增量构建很快
3. ✅ **可靠**：使用标准 Docker 流程，无权限问题
4. ✅ **一致**：开发和生产使用相同镜像

## 故障排查

如果构建失败，查看详细日志：

```bash
docker compose build backend --progress=plain
```

如果服务启动失败：

```bash
docker compose logs backend
```
