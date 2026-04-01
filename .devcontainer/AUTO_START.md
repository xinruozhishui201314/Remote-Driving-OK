# 自动启动配置说明

## 概述

此配置实现了**每次 Cursor 打开工程时自动设置 X11 权限并启动 Docker 容器**的功能。

## 自动化流程

```
打开 Cursor 工程
    ↓
自动运行预启动脚本 (pre-start.sh)
    ↓
设置 X11 权限 (setup-x11.sh)
    ↓
检查 Docker 服务
    ↓
提示：按 F1 → "Dev Containers: Reopen in Container"
    ↓
容器启动
    ↓
自动运行 setup.sh (初始化环境)
    ↓
自动运行 verify-gui.sh (验证 GUI)
    ↓
可以运行 GUI 应用 ✓
```

## 配置说明

### 1. 自动任务配置

`.vscode/tasks.json` 中配置了：
- **Setup Dev Container (Pre-start)**: 在文件夹打开时自动运行
- **Setup X11 Permissions**: 手动任务，设置 X11 权限
- **Verify GUI Environment**: 验证 GUI 环境

### 2. Dev Container 配置

`.devcontainer/devcontainer.json` 中配置了：
- X11 socket 挂载
- DISPLAY 环境变量传递
- 容器启动后自动验证 GUI

### 3. 脚本说明

| 脚本 | 运行位置 | 功能 |
|------|---------|------|
| `pre-start.sh` | 主机 | 预启动检查（Docker、X11） |
| `setup-x11.sh` | 主机 | 设置 X11 权限 |
| `setup.sh` | 容器内 | 初始化开发环境 |
| `verify-gui.sh` | 容器内 | 验证 GUI 环境 |

## 使用方法

### 方法一：自动启动（推荐）

1. **打开 Cursor**：
   ```bash
   cd /home/wqs/bigdata/Remote-Driving
   cursor .
   ```

2. **自动执行**：
   - 打开文件夹时，会自动运行 `pre-start.sh`
   - 设置 X11 权限
   - 检查 Docker 服务

3. **启动容器**：
   - 按 `F1` 或 `Ctrl+Shift+P`
   - 输入：`Dev Containers: Reopen in Container`
   - 等待容器启动

4. **验证 GUI**：
   容器启动后会自动运行 `verify-gui.sh`，确认 GUI 环境就绪

### 方法二：手动设置（如果自动任务被禁用）

1. **手动设置 X11 权限**：
   ```bash
   bash .devcontainer/setup-x11.sh
   ```

2. **启动容器**：
   在 Cursor 中按 `F1` → `Dev Containers: Reopen in Container`

## 运行 GUI 应用

容器启动后，可以直接运行 Qt GUI 应用：

```bash
# 构建项目
cd build
cmake .. -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64
make -j$(nproc)

# 运行应用
./bin/your_app
```

GUI 窗口会显示在主机的 X11 显示器上。

## 故障排查

### Q1: 自动任务没有运行

**检查：**
1. 打开 Cursor 设置
2. 搜索 `task.allowAutomaticTasks`
3. 确保设置为 `on`

**或手动运行：**
```bash
bash .devcontainer/pre-start.sh
```

### Q2: X11 权限设置失败

**手动设置：**
```bash
# 方法 1
xhost +local:docker

# 方法 2
xhost +SI:localuser:$(whoami)

# 方法 3（不推荐，安全性较低）
xhost +
```

### Q3: GUI 应用无法显示

**检查清单：**
1. DISPLAY 环境变量：`echo $DISPLAY`
2. X11 socket：`ls -la /tmp/.X11-unix/`
3. X11 连接：`xdpyinfo -display $DISPLAY`
4. Qt 平台插件：`ls $QT_GCC/plugins/platforms/`

**运行验证脚本：**
```bash
bash .devcontainer/verify-gui.sh
```

### Q4: 容器启动后 DISPLAY 未设置

**在容器内手动设置：**
```bash
export DISPLAY=:0
export QT_QPA_PLATFORM=xcb
```

## 高级配置

### 禁用自动任务

如果不想自动运行预启动脚本，编辑 `.vscode/settings.json`：
```json
{
  "task.allowAutomaticTasks": "off"
}
```

### 自定义 DISPLAY

如果使用远程 X11 或自定义显示，编辑 `.devcontainer/devcontainer.json`：
```json
"containerEnv": {
  "DISPLAY": "your-custom-display:0"
}
```

### 添加更多 X11 工具

在 `setup.sh` 中添加：
```bash
sudo apt-get install -y x11-apps x11-utils
```

## 验证清单

- [ ] 打开 Cursor 时自动运行预启动脚本
- [ ] X11 权限自动设置成功
- [ ] Docker 服务正常运行
- [ ] 容器启动成功
- [ ] GUI 环境验证通过
- [ ] 可以运行 Qt GUI 应用

## 参考

- [Dev Containers 文档](https://containers.dev/)
- [X11 转发文档](https://wiki.archlinux.org/title/X11_forwarding)
- [Qt 平台插件文档](https://doc.qt.io/qt-6/qtplatform.html)
