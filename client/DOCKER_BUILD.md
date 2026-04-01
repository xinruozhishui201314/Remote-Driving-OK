# Docker 容器内编译和运行指南

## Executive Summary

所有编译和运行脚本已更新，支持在 Docker 容器（`docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt`）内进行编译和运行。脚本会自动检测容器环境并使用容器内的 Qt6 路径。

---

## 1. 在 Dev Container 中使用

### 1.1 启动容器

1. **打开 Cursor**
2. **按 F1** → 选择 `Dev Containers: Reopen in Container`
3. **等待容器启动**

### 1.2 编译 Client 工程

```bash
cd /workspace/client
./build.sh
```

**脚本会自动**:
- ✅ 检测容器环境
- ✅ 使用容器内 Qt6 路径（`/opt/Qt/6.8.0/gcc_64`）
- ✅ 设置 Qt 环境变量
- ✅ 并行编译

### 1.3 运行 Client 应用

```bash
cd /workspace/client
./run.sh
```

**脚本会自动**:
- ✅ 检测容器环境
- ✅ 设置 Qt 库路径
- ✅ 设置 X11 DISPLAY
- ✅ 配置 Qt 平台插件（xcb）

### 1.4 调试 Client 应用

```bash
cd /workspace/client
./debug.sh
```

---

## 2. 手动启动 Docker 容器

### 2.1 启动容器

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

### 2.2 在容器内编译和运行

```bash
# 进入 client 目录
cd /workspace/client

# 编译
./build.sh

# 运行
./run.sh
```

---

## 3. 脚本自动检测功能

### 3.1 容器环境检测

脚本会自动检测是否在容器内：
- 检查 `/.dockerenv` 文件
- 检查 `$CONTAINER_ID` 环境变量

### 3.2 Qt6 路径优先级

1. **容器内路径**（优先）: `/opt/Qt/6.8.0/gcc_64`
2. **环境变量**: `$QT_GCC`
3. **用户目录**: `$HOME/Qt/6.8.0/gcc_64`

### 3.3 环境变量自动设置

在容器内运行时，脚本会自动设置：
- `LD_LIBRARY_PATH`: Qt6 库路径
- `PATH`: Qt6 工具路径
- `QT_PLUGIN_PATH`: Qt 插件路径
- `QML2_IMPORT_PATH`: QML 导入路径
- `DISPLAY`: X11 显示（`:0`）
- `QT_QPA_PLATFORM`: Qt 平台插件（`xcb`）

---

## 4. 验证容器环境

### 4.1 检查容器环境

```bash
# 检查是否在容器内
[ -f /.dockerenv ] && echo "在容器内" || echo "不在容器内"

# 检查 Qt6
ls -la /opt/Qt/6.8.0/gcc_64/bin/qmake

# 检查环境变量
echo "QT_GCC: $QT_GCC"
echo "DISPLAY: $DISPLAY"
```

### 4.2 验证 GUI 环境

```bash
# 运行 GUI 验证脚本
bash .devcontainer/verify-gui.sh
```

---

## 5. 常见问题

### Q1: 编译失败 - 找不到 Qt6

**解决**:
```bash
# 检查容器内 Qt6 路径
ls -la /opt/Qt/6.8.0/gcc_64

# 手动设置（如果需要）
export CMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64
export QT_GCC=/opt/Qt/6.8.0/gcc_64
./build.sh
```

### Q2: GUI 应用无法显示

**检查**:
1. X11 socket 是否挂载: `ls -la /tmp/.X11-unix/`
2. DISPLAY 环境变量: `echo $DISPLAY`
3. X11 权限: 在宿主机运行 `xhost +local:docker`

**解决**:
```bash
# 在容器内设置
export DISPLAY=:0
export QT_QPA_PLATFORM=xcb

# 验证 X11 连接
xdpyinfo -display $DISPLAY
```

### Q3: 库加载失败

**解决**:
```bash
# 设置库路径
export LD_LIBRARY_PATH=/opt/Qt/6.8.0/gcc_64/lib:$LD_LIBRARY_PATH

# 检查库是否存在
ldd build/RemoteDrivingClient | grep Qt
```

---

## 6. 完整工作流

### 6.1 在 Dev Container 中

```bash
# 1. 启动 Cursor，容器会自动启动

# 2. 编译
cd /workspace/client
./build.sh

# 3. 运行
./run.sh

# 4. 调试（如果需要）
./debug.sh
```

### 6.2 手动 Docker 容器

```bash
# 1. 在宿主机设置 X11 权限
xhost +local:docker

# 2. 启动容器
docker run -it --rm \
  --privileged \
  --network=host \
  -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -v $(pwd):/workspace \
  -w /workspace \
  docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt \
  /bin/bash

# 3. 在容器内
cd /workspace/client
./build.sh
./run.sh
```

---

## 7. 脚本特性

### ✅ 自动检测容器环境
- 检测 `/.dockerenv`
- 检测 `$CONTAINER_ID`
- 自动使用容器内路径

### ✅ 自动设置环境变量
- Qt6 库路径
- X11 显示
- Qt 平台插件

### ✅ 错误处理
- 检查可执行文件是否存在
- 检查依赖是否安装
- 清晰的错误提示

---

## 8. 注意事项

1. **必须在容器内运行**: 脚本设计为在 Docker 容器内使用
2. **X11 权限**: 宿主机需要设置 `xhost +local:docker`
3. **网络模式**: 使用 `--network=host` 以便访问网络服务
4. **工作目录**: 代码挂载到 `/workspace`

---

## 9. 验证清单

- [x] 脚本支持容器环境检测
- [x] 脚本使用容器内 Qt6 路径
- [x] 脚本自动设置环境变量
- [x] GUI 环境配置正确
- [ ] 实际编译测试（需要容器环境）
- [ ] 实际运行测试（需要容器环境）

---

**所有脚本已更新，支持在 Docker 容器内编译和运行！** 🐳
