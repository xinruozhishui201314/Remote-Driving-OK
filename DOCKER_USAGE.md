# Docker 容器内使用指南

## 概述

所有工程（client、media、Vehicle-side）的编译和运行脚本已更新，支持在 Docker 容器内执行。**所有编译和运行操作必须在容器内进行，不能在宿主机上运行。**

---

## 1. 快速开始

### 1.1 使用 Dev Container（推荐）

```bash
# 1. 在 Cursor 中打开项目
cursor /home/wqs/bigdata/Remote-Driving

# 2. 按 F1 → "Dev Containers: Reopen in Container"

# 3. 等待容器启动

# 4. 在容器内编译和运行
cd /workspace/client
./build.sh
./run.sh
```

### 1.2 手动启动 Docker 容器

```bash
# 在宿主机上设置 X11 权限
xhost +local:docker

# 启动容器
docker run -it --rm \
  --privileged \
  --network=host \
  -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -v $(pwd):/workspace \
  -w /workspace \
  docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt \
  /bin/bash
```

---

## 2. Client 工程（容器内）

### 2.1 编译

```bash
cd /workspace/client
./build.sh
```

**脚本会自动**:
- ✅ 检测容器环境
- ✅ 使用 `/opt/Qt/6.8.0/gcc_64`（容器内路径）
- ✅ 设置 Qt 环境变量

### 2.2 运行

```bash
cd /workspace/client
./run.sh
```

**脚本会自动**:
- ✅ 设置 Qt 库路径
- ✅ 设置 X11 DISPLAY
- ✅ 配置 Qt 平台插件

### 2.3 调试

```bash
cd /workspace/client
./debug.sh
```

---

## 3. Media 工程（容器内）

### 3.1 编译

```bash
cd /workspace/media
./build.sh
```

### 3.2 运行

```bash
cd /workspace/media
./run.sh
```

---

## 4. Vehicle-side 工程（容器内）

### 4.1 编译

```bash
cd /workspace/Vehicle-side
./build.sh
```

### 4.2 运行

```bash
cd /workspace/Vehicle-side
./run.sh mqtt://192.168.1.100:1883
```

---

## 5. 使用 Makefile（容器内）

```bash
# 在容器内
cd /workspace

# 编译客户端
make build-client

# 运行客户端
make run-client

# 编译所有工程
make build-all
```

---

## 6. 容器环境验证

### 6.1 检查容器环境

```bash
# 检查是否在容器内
[ -f /.dockerenv ] && echo "✓ 在容器内" || echo "✗ 不在容器内"

# 检查 Qt6
ls -la /opt/Qt/6.8.0/gcc_64/bin/qmake

# 检查环境变量
echo "QT_GCC: $QT_GCC"
echo "DISPLAY: $DISPLAY"
```

### 6.2 验证 GUI 环境

```bash
# 运行验证脚本
bash .devcontainer/verify-gui.sh
```

---

## 7. 重要提示

### ⚠️ 必须在容器内运行

- ✅ **正确**: 在容器内运行 `./build.sh` 和 `./run.sh`
- ❌ **错误**: 在宿主机上运行脚本

### ⚠️ X11 权限设置

在宿主机上运行（容器启动前）:
```bash
xhost +local:docker
```

### ⚠️ 网络配置

容器使用 `--network=host`，可以直接访问：
- MQTT Broker（localhost:1883）
- ZLMediaKit 服务器（localhost:8080）

---

## 8. 故障排查

### Q1: 脚本提示"未找到 Qt6"

**解决**:
```bash
# 确认在容器内
[ -f /.dockerenv ] && echo "在容器内" || echo "不在容器内，请启动容器"

# 检查容器内 Qt6
ls -la /opt/Qt/6.8.0/gcc_64

# 手动设置（如果需要）
export QT_GCC=/opt/Qt/6.8.0/gcc_64
./build.sh
```

### Q2: GUI 应用无法显示

**解决**:
```bash
# 1. 在宿主机设置 X11 权限
xhost +local:docker

# 2. 在容器内设置 DISPLAY
export DISPLAY=:0
export QT_QPA_PLATFORM=xcb

# 3. 验证
xdpyinfo -display $DISPLAY
```

### Q3: 库加载失败

**解决**:
```bash
# 设置库路径
export LD_LIBRARY_PATH=/opt/Qt/6.8.0/gcc_64/lib:$LD_LIBRARY_PATH

# 检查依赖
ldd build/RemoteDrivingClient | grep Qt
```

---

## 9. 完整示例

### 示例 1: Dev Container 工作流

```bash
# 1. 打开 Cursor，容器自动启动

# 2. 在容器终端中
cd /workspace/client

# 3. 编译
./build.sh

# 4. 运行（GUI 窗口会显示在宿主机）
./run.sh
```

### 示例 2: 手动 Docker 容器

```bash
# 1. 宿主机：设置 X11 权限
xhost +local:docker

# 2. 宿主机：启动容器
docker run -it --rm \
  --privileged \
  --network=host \
  -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -v $(pwd):/workspace \
  docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt \
  /bin/bash

# 3. 容器内：编译和运行
cd /workspace/client
./build.sh
./run.sh
```

---

## 10. 验证清单

- [x] 脚本支持容器环境检测
- [x] 脚本使用容器内 Qt6 路径
- [x] 脚本自动设置环境变量
- [x] GUI 环境配置正确
- [ ] 在容器内实际编译测试
- [ ] 在容器内实际运行测试

---

**所有脚本已更新，必须在 Docker 容器内编译和运行！** 🐳
