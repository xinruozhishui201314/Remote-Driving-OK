# Backend 开发模式

## 概述

开发模式允许你在**不重建 Docker 镜像、不重启容器**的情况下修改代码。源码通过 volume 挂载到容器，容器内包含编译工具，**自动监控文件变化并重新编译运行**。

---

## 快速开始

### 1. 首次构建开发镜像（只需一次）

```bash
./scripts/dev-backend.sh build
```

或手动：

```bash
docker compose -f docker-compose.yml -f docker-compose.dev.yml build backend
```

### 2. 启动开发模式

```bash
./scripts/dev-backend.sh up
```

或手动：

```bash
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d backend
```

### 3. 修改代码（自动重新编译运行）

**无需任何操作！** 容器会自动监控 `src/` 和 `CMakeLists.txt` 的变化，检测到修改后自动重新编译并重启 backend。

只需：
1. 保存文件
2. 等待几秒（编译时间）
3. 测试：`curl http://localhost:8081/health`

如需手动重启（例如修改了 CMakeLists.txt）：

```bash
./scripts/dev-backend.sh restart
```

### 4. 查看日志

```bash
./scripts/dev-backend.sh logs
```

---

## 工作流程

1. **修改代码**：编辑 `backend/src/` 下的 `.cpp` 或 `.h` 文件
2. **保存文件**：容器自动检测变化，重新编译并重启 backend（无需手动操作）
3. **查看日志**：`./scripts/dev-backend.sh logs` 查看编译和运行日志
4. **测试**：`curl http://localhost:8081/health`

---

## 原理

- **Dockerfile.dev**：包含编译工具（cmake, g++, libpq-dev）和运行时依赖（libpq5）
- **docker-compose.dev.yml**：挂载源码目录和 CMakeLists.txt 到容器
- **docker-entrypoint-dev.sh**：容器启动时执行编译并运行，使用 `inotifywait` 监控源码变化，自动重新编译并重启 backend
- **backend_build volume**：持久化构建目录，加快增量编译

---

## 优势

- ✅ **无需重建镜像**：修改代码后无需重建 Docker 镜像
- ✅ **无需重启容器**：修改代码后自动检测并重新编译运行
- ✅ **增量编译**：CMake 缓存构建目录，只编译变更文件
- ✅ **环境一致**：编译和运行都在容器内，与生产环境一致
- ✅ **快速迭代**：修改 → 保存 → 自动编译 → 测试，循环极快

---

## 注意事项

- 首次启动会下载依赖（cpp-httplib, nlohmann_json），可能需要几分钟
- 修改 CMakeLists.txt 后建议重建镜像：`./scripts/dev-backend.sh build`
- 构建目录在 volume `backend_build` 中，删除容器不会丢失（除非 `docker compose down -v`）

---

## 对比：生产模式 vs 开发模式

| 模式 | 镜像构建 | 代码修改 | 编译位置 | 使用场景 |
|------|---------|---------|---------|---------|
| **生产模式** | 每次修改需重建 | 需重建镜像 | Docker 构建时 | CI/CD、部署 |
| **开发模式** | 只需一次 | **自动重载**（无需重启） | 容器内监控变化 | 本地开发 |
