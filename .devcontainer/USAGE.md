# Dev Container 使用指南

## 🚀 一键启动流程

### 完整流程

```
1. 打开 Cursor
   ↓
2. 自动设置 X11 权限 ✓
   ↓
3. 检查 Docker 服务 ✓
   ↓
4. 按 F1 → "Dev Containers: Reopen in Container"
   ↓
5. 容器启动 ✓
   ↓
6. 自动初始化环境 ✓
   ↓
7. 自动验证 GUI ✓
   ↓
8. 可以运行 GUI 应用 ✓
```

## 📋 详细步骤

### 步骤 1: 打开项目

```bash
cd /home/wqs/bigdata/Remote-Driving
cursor .
```

**自动执行：**
- ✅ 运行预启动脚本（设置 X11 权限）
- ✅ 检查 Docker 服务
- ✅ 验证 Docker 镜像

### 步骤 2: 启动容器

1. 按 `F1` 或 `Ctrl+Shift+P`
2. 输入：`Dev Containers: Reopen in Container`
3. 选择该命令
4. 等待容器启动（首次可能需要几分钟）

**容器启动后自动执行：**
- ✅ 初始化开发环境（setup.sh）
- ✅ 验证 GUI 环境（verify-gui.sh）
- ✅ 设置工作目录权限
- ✅ 安装常用工具

### 步骤 3: 验证环境

容器启动后，在终端运行：

```bash
# 检查 Qt
qmake --version

# 检查 CMake
cmake --version

# 检查 GUI 环境
bash .devcontainer/verify-gui.sh

# 测试 X11（如果安装了 x11-apps）
xeyes &
```

### 步骤 4: 构建和运行

```bash
# 创建构建目录
mkdir -p build && cd build

# 配置 CMake
cmake .. \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64 \
  -DCMAKE_BUILD_TYPE=Debug

# 编译
make -j$(nproc)

# 运行 GUI 应用
./bin/your_app
```

## 🎯 关键特性

### ✅ 自动 X11 权限设置

每次打开 Cursor 时自动运行 `setup-x11.sh`：
- 自动检测 DISPLAY 环境变量
- 设置 `xhost +local:docker`
- 验证 X11 连接

### ✅ GUI 应用支持

容器配置已包含：
- X11 socket 挂载
- DISPLAY 环境变量传递
- Qt 平台插件配置（xcb）
- 自动 GUI 环境验证

### ✅ 开发工具集成

- C++ 智能提示（已配置 Qt 头文件路径）
- CMake 工具集成
- 调试支持（GDB）
- 代码格式化

## 🔧 手动操作（如果需要）

### 手动设置 X11 权限

```bash
bash .devcontainer/setup-x11.sh
```

### 手动验证 GUI 环境

```bash
# 在容器内
bash .devcontainer/verify-gui.sh
```

### 手动运行预启动脚本

```bash
bash .devcontainer/pre-start.sh
```

## 🐛 故障排查

### GUI 应用无法显示

**检查清单：**
1. X11 权限：`xhost`（应显示 local:docker）
2. DISPLAY 变量：`echo $DISPLAY`（应为 :0 或类似）
3. X11 socket：`ls -la /tmp/.X11-unix/`
4. Qt 平台：`echo $QT_QPA_PLATFORM`（应为 xcb）

**解决方案：**
```bash
# 在主机上
bash .devcontainer/setup-x11.sh

# 在容器内
export DISPLAY=:0
export QT_QPA_PLATFORM=xcb
```

### 容器启动失败

**检查：**
- Docker 服务：`sudo systemctl status docker`
- Docker 镜像：`docker images | grep qt6`
- 用户权限：`groups | grep docker`

### 自动任务未运行

**检查 Cursor 设置：**
- `task.allowAutomaticTasks` 应为 `on`
- 或手动运行：`bash .devcontainer/pre-start.sh`

## 📚 相关文档

- [README.md](./README.md) - 完整配置说明
- [QUICKSTART.md](./QUICKSTART.md) - 快速开始指南
- [AUTO_START.md](./AUTO_START.md) - 自动启动详细说明

## 🎓 最佳实践

1. **首次使用**：确保 Docker 服务运行，镜像已拉取
2. **日常开发**：直接打开 Cursor，自动设置会处理一切
3. **GUI 调试**：使用 `verify-gui.sh` 验证环境
4. **问题排查**：查看容器日志和验证脚本输出

## ⚡ 快速命令参考

```bash
# 设置 X11 权限
bash .devcontainer/setup-x11.sh

# 验证配置
bash .devcontainer/verify.sh

# 验证 GUI
bash .devcontainer/verify-gui.sh

# 预启动检查
bash .devcontainer/pre-start.sh

# 构建项目
cd build && cmake .. -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64 && make -j$(nproc)
```
