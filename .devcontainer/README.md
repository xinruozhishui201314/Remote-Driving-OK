# Dev Container 配置说明

## 概述

此配置允许在 Cursor/VSCode 中直接使用 Docker 镜像 `docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt` 进行开发。

## 功能特性

- ✅ 自动启动 Qt6 开发容器
- ✅ 项目代码自动挂载到容器
- ✅ Qt6.8.0 + CMake + GCC 开发环境
- ✅ C++ 智能提示和调试支持
- ✅ 端口自动转发（HTTP/RTMP/RTSP/MQTT）
- ✅ 支持主机网络模式（用于远程驾驶）

## 快速开始

### 1. 前置条件

确保已安装：
- Docker Engine 20.10+
- Cursor/VSCode（已安装 Remote - Containers 扩展）
- X11 服务器（Linux 桌面环境自带）

### 2. 自动启动（推荐）

**一键启动流程：**

1. **打开 Cursor**：
   ```bash
   cd /home/wqs/bigdata/Remote-Driving
   cursor .
   ```

2. **自动执行**：
   - ✅ 自动设置 X11 权限（允许容器访问 GUI）
   - ✅ 检查 Docker 服务状态
   - ✅ 验证 Docker 镜像

3. **启动容器**：
   - 按 `F1` 或 `Ctrl+Shift+P`
   - 输入：`Dev Containers: Reopen in Container`
   - 等待容器启动（首次可能需要几分钟）

4. **自动验证**：
   - 容器启动后自动运行初始化脚本
   - 自动验证 GUI 环境
   - 可以直接运行 Qt GUI 应用

### 3. 手动启动（备选）

**方法一：通过命令面板**
1. 在 Cursor 中按 `F1` 或 `Ctrl+Shift+P`
2. 输入 `Dev Containers: Reopen in Container`
3. 选择该命令，等待容器启动

**方法二：通过通知**
1. 打开项目文件夹
2. 如果检测到 `.devcontainer` 配置，会弹出通知
3. 点击 "Reopen in Container"

**方法三：手动设置 X11 权限**
如果自动设置失败，手动运行：
```bash
bash .devcontainer/setup-x11.sh
```

**方法三：通过终端**
```bash
# 手动启动容器（用于调试）
docker run -it --rm \
  --privileged \
  --network=host \
  -v $(pwd):/workspace \
  -v /var/run/docker.sock:/var/run/docker.sock \
  -w /workspace \
  docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt \
  /bin/bash
```

### 3. 验证环境

容器启动后，在终端执行：
```bash
# 检查 Qt 版本
qmake --version

# 检查 CMake 版本
cmake --version

# 检查 GCC 版本
g++ --version

# 检查 Qt 路径
echo $QT_GCC
ls -la $QT_GCC/bin
```

## 开发工作流

### 构建项目

```bash
# 创建构建目录
mkdir -p build && cd build

# 配置 CMake（根据项目结构调整）
cmake .. \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64 \
  -DCMAKE_BUILD_TYPE=Debug

# 编译
make -j$(nproc)
```

### 调试配置

在 `.vscode/launch.json` 中添加调试配置：

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "C++ Debug (Container)",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/bin/your_app",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [
        {
          "name": "QT_QPA_PLATFORM",
          "value": "offscreen"
        }
      ],
      "externalConsole": false,
      "MIMode": "gdb",
      "setupCommands": [
        {
          "description": "Enable pretty-printing for gdb",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        }
      ],
      "preLaunchTask": "build"
    }
  ]
}
```

### 运行 GUI 应用

**配置已自动完成！** 容器启动后可以直接运行 GUI 应用：

```bash
# 构建项目
cd build
cmake .. -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64
make -j$(nproc)

# 运行 GUI 应用（窗口会自动显示在主机）
./bin/your_app
```

**验证 GUI 环境：**
```bash
# 在容器内运行
bash .devcontainer/verify-gui.sh

# 或测试 X11 连接
xdpyinfo -display $DISPLAY
```

**GUI 配置说明：**
- ✅ X11 socket 已自动挂载
- ✅ DISPLAY 环境变量已自动设置
- ✅ Qt 平台插件已配置（xcb）
- ✅ X11 权限已自动设置

## 端口转发说明

| 端口 | 服务 | 说明 |
|------|------|------|
| 8080 | ZLMediaKit HTTP | WebRTC/HTTP API |
| 1935 | RTMP | RTMP 推流/拉流 |
| 554 | RTSP | RTSP 流媒体 |
| 9000 | WebRTC | WebRTC 信令（示例） |
| 1883 | MQTT | 遥测数据（示例） |

端口会自动转发到主机，可通过 `localhost:端口` 访问。

## 常见问题

### Q1: 容器启动失败

**检查项：**
- Docker 服务是否运行：`sudo systemctl status docker`
- 镜像是否存在：`docker images | grep qt6`
- 权限问题：确保用户在 `docker` 组中

### Q2: 无法找到 Qt 库

**解决方案：**
```bash
# 检查环境变量
echo $CMAKE_PREFIX_PATH
echo $LD_LIBRARY_PATH

# 手动设置（如果需要）
export CMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64
export LD_LIBRARY_PATH=/opt/Qt/6.8.0/gcc_64/lib:$LD_LIBRARY_PATH
```

### Q3: GUI 应用无法显示

**检查清单：**
1. 验证 X11 权限：`xhost`（应显示 local:docker）
2. 检查 DISPLAY：`echo $DISPLAY`（容器内）
3. 检查 X11 socket：`ls -la /tmp/.X11-unix/`
4. 运行验证脚本：`bash .devcontainer/verify-gui.sh`

**解决方案：**
```bash
# 在主机上重新设置 X11 权限
bash .devcontainer/setup-x11.sh

# 或在容器内手动设置
export DISPLAY=:0
export QT_QPA_PLATFORM=xcb
```

### Q4: 智能提示不工作

**解决方案：**
1. 重新加载窗口：`F1` → `Developer: Reload Window`
2. 检查 C++ 扩展是否安装
3. 检查 `c_cpp_properties.json` 配置

### Q5: 网络连接问题

**说明：**
- 配置使用了 `--network=host`，容器直接使用主机网络
- 如果不需要主机网络，可以移除该参数

### Q6: 文件权限问题

**解决方案：**
```bash
# 在容器内修复权限
sudo chown -R user:user /workspace
```

### Q7: 自动任务未运行

**检查：**
1. Cursor 设置中 `task.allowAutomaticTasks` 应为 `on`
2. 或手动运行：`bash .devcontainer/pre-start.sh`

## 高级配置

### 自定义启动命令

修改 `devcontainer.json` 中的 `postCreateCommand`：
```json
"postCreateCommand": "bash .devcontainer/setup.sh"
```

### 安装额外工具

在 `postCreateCommand` 中添加：
```json
"postCreateCommand": "apt-get update && apt-get install -y vim git curl && echo 'Tools installed'"
```

### GPU 支持（如果需要）

添加 GPU 运行时：
```json
"runArgs": [
  "--privileged",
  "--network=host",
  "--gpus=all"
]
```

## 故障排查

### 查看容器日志
```bash
docker logs <container_id>
```

### 进入运行中的容器
```bash
docker exec -it <container_id> /bin/bash
```

### 清理容器
```bash
# 停止并删除容器
docker stop <container_id>
docker rm <container_id>

# 清理未使用的资源
docker system prune -a
```

## 参考资源

- [Dev Containers 官方文档](https://containers.dev/)
- [Qt6 文档](https://doc.qt.io/qt-6/)
- [CMake 文档](https://cmake.org/documentation/)
