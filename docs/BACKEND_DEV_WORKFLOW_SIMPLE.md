# Backend 开发工作流（简化版）

## 推荐方案：外部编译 + 生产模式运行

### 快速开始

```bash
# 1. 首次设置（安装编译工具，只需一次）
# Ubuntu/Debian:
sudo apt-get install cmake build-essential libpq-dev

# macOS:
brew install cmake postgresql

# 2. 编译并运行
./scripts/compile-and-run-backend.sh
```

### 工作流程

1. **修改代码**：编辑 `backend/src/` 下的文件
2. **重新编译运行**：执行 `./scripts/compile-and-run-backend.sh`
3. **测试**：使用 `./scripts/test-client-backend-integration.sh`

### 优点

- ✅ **快速**：在主机上编译，速度快
- ✅ **无需重建镜像**：使用生产模式镜像，只挂载二进制文件
- ✅ **简单**：不需要处理容器内的编译环境
- ✅ **可靠**：不依赖网络下载依赖项（CMake 使用本地依赖）

### 工作原理

1. 在主机上使用本地 CMake 编译代码
2. 将编译好的二进制文件挂载到生产模式容器
3. 容器直接运行挂载的二进制文件

### 文件结构

```
backend/
├── src/              # 源码（修改这里）
├── CMakeLists.txt    # CMake 配置
└── build/            # 编译输出（自动生成）
    └── bin/
        └── teleop_backend  # 编译好的二进制
```

## 备选方案：容器内编译（开发模式）

如果主机上没有编译工具，可以使用容器内编译：

```bash
# 首次构建开发镜像（只需一次）
docker compose -f docker-compose.yml -f docker-compose.dev.yml build backend

# 启动开发模式
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d backend

# 修改代码后重启（会自动重新编译）
docker compose -f docker-compose.yml -f docker-compose.dev.yml restart backend
```

**注意**：容器内编译可能较慢，因为需要从网络下载依赖项。

## 对比

| 方案 | 编译速度 | 镜像大小 | 网络依赖 | 推荐度 |
|------|---------|---------|---------|--------|
| 外部编译 | ⚡⚡⚡ 快 | 小（85MB） | 无 | ⭐⭐⭐⭐⭐ |
| 容器内编译 | 🐌 慢 | 大（200MB） | 有 | ⭐⭐⭐ |

## 故障排查

### 外部编译失败

```bash
# 检查编译工具
cmake --version
g++ --version

# 检查依赖
pkg-config --modversion libpq

# 手动编译调试
cd backend/build
cmake ../backend
cmake --build . --target teleop_backend -v
```

### 容器启动失败

```bash
# 检查二进制文件是否存在
ls -la backend/build/bin/teleop_backend

# 检查文件权限
chmod +x backend/build/bin/teleop_backend

# 查看容器日志
docker compose logs backend
```

## 相关脚本

- `scripts/compile-and-run-backend.sh`：外部编译 + 运行
- `scripts/dev-backend-fast.sh`：容器内编译（开发模式）
- `scripts/rebuild-backend.sh`：强制重新编译（开发模式）
- `scripts/test-client-backend-integration.sh`：端到端测试
