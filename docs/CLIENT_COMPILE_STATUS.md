# 客户端编译状态

## 当前问题

### 问题描述

客户端在容器内编译时遇到 Qt6Gui 依赖问题：

```
Qt6Gui could not be found because dependency WrapOpenGL could not be found.
```

### 原因分析

1. **OpenGL 依赖**：Qt6Gui 需要 OpenGL 库（WrapOpenGL）
2. **容器环境**：`docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt` 镜像可能没有安装 OpenGL 开发库
3. **权限问题**：build 目录是 volume 挂载，可能存在权限问题

### 解决方案

#### 方案 1：禁用 OpenGL 依赖（推荐）

修改 `client/CMakeLists.txt`，在查找 Qt6 之前禁用 OpenGL：

```cmake
# 禁用 OpenGL 依赖检查
set(QT_FEATURE_opengl OFF CACHE BOOL "Disable OpenGL" FORCE)
set(QT_FEATURE_opengles2 OFF CACHE BOOL "Disable OpenGL ES2" FORCE)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Quick
    Network
    Qml
    Gui
)
```

#### 方案 2：安装 OpenGL 库

在容器内安装 OpenGL 开发库：

```bash
docker compose exec client-dev bash -c "apt-get update && apt-get install -y libgl1-mesa-dev libglu1-mesa-dev"
```

**注意**：需要 root 权限，可能需要修改 Dockerfile 或使用 `docker exec -u root`。

#### 方案 3：使用临时构建目录

避免 volume 权限问题，使用容器内的临时目录：

```bash
docker compose exec client-dev bash -c "
    mkdir -p /tmp/client-build
    cd /tmp/client-build
    cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64
    make -j4
"
```

## 当前状态

- ✅ CMakeLists.txt 已修改（禁用 OpenGL）
- ⏳ 等待验证编译结果

## 验证步骤

```bash
# 1. 清理并重新配置
docker compose exec client-dev bash -c "cd /tmp/client-build && rm -rf * && cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64 -DCMAKE_BUILD_TYPE=Debug -DQT_FEATURE_opengl=OFF -DQT_FEATURE_opengles2=OFF"

# 2. 编译
docker compose exec client-dev bash -c "cd /tmp/client-build && make -j4"

# 3. 验证
docker compose exec client-dev bash -c "test -f /tmp/client-build/RemoteDrivingClient && ls -lh /tmp/client-build/RemoteDrivingClient"
```

## 相关文件

- `client/CMakeLists.txt`：CMake 配置文件
- `scripts/run-client-dev.sh`：客户端开发启动脚本
- `docs/CLIENT_UI_VERIFICATION.md`：UI 功能验证指南
