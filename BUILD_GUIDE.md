# 编译和运行指南

## Executive Summary

三个工程（client、media、Vehicle-side）已配置独立的编译和运行脚本，支持一键编译、一键运行和高效调试。

---

## 1. 工程部署位置

| 工程 | 部署位置 | 说明 |
|------|---------|------|
| **client** | 客户端台式机 | Qt6/QML 客户端应用 |
| **media** | 流媒体服务器 | ZLMediaKit 媒体服务器 |
| **Vehicle-side** | 车辆端（Jetson Orin） | ROS2/MQTT 车辆控制器 |

---

## 2. 快速开始

### 2.1 使用 Makefile（推荐）

```bash
# 查看所有可用命令
make help

# 编译客户端
make build-client

# 运行客户端
make run-client

# 调试客户端
make debug-client

# 编译所有工程
make build-all

# 清理所有构建文件
make clean-all
```

### 2.2 使用 Taskfile（可选）

```bash
# 安装 Task: go install github.com/go-task/task/v3/cmd/task@latest

# 查看所有任务
task --list

# 编译客户端
task client:build

# 运行客户端
task client:run

# 编译所有工程
task build
```

### 2.3 直接使用脚本

> **注意**：仓库策略要求 **默认在 Docker 容器内** 编译与运行；子目录 `build.sh` / `run.sh` 设计为在对应容器内调用。宿主机直接 `cd client && ./build.sh` 可能报错或环境不一致，请先阅读 [`docs/BUILD_AND_RUN_POLICY.md`](docs/BUILD_AND_RUN_POLICY.md)。

```bash
# 客户端（仅当已在 client-dev 容器内；根目录 build.sh 会拒绝宿主机执行）
cd client
./build.sh
./run.sh

# 媒体服务器
cd media
./build.sh    # 编译
./run.sh      # 运行
./debug.sh    # 调试

# 车辆端
cd Vehicle-side
./build.sh    # 编译
./run.sh      # 运行
./debug.sh    # 调试
```

---

## 3. 各工程详细说明

### 3.1 Client 工程（客户端台式机）

**位置**: `client/`

**栈与分层**:
- **CMake 3.21+**，**C++20**，**Qt6**（Core, Network, Gui, Quick, Qml；可选 Multimedia、WebSockets、QuickControls2、ShaderTools、OpenGL、Test）
- 可选：FFmpeg（软解）、VA-API、EGL DMA-BUF（Linux）、NVDEC（`ENABLE_NVDEC=ON`）、libdatachannel（WebRTC）、Paho MQTT C++
- 源码分层：`src/core`、`src/infrastructure`、`src/services`、`src/presentation`，视频着色器在 `client/shaders/`，详见 [`client/README.md`](client/README.md)

**推荐编译/运行（Docker client-dev 容器）**:
```bash
# 在仓库根目录（宿主机）
make build-client
make run-client
```

**在容器内使用脚本**:
```bash
cd client
./build.sh
./run.sh
```

**环境变量（节选）**:
- `CMAKE_PREFIX_PATH`: Qt6 与 libdatachannel 等前缀路径
- `BACKEND_URL`、`KEYCLOAK_URL`、`MQTT_BROKER_URL`、`ZLM_WHEP_URL`
- 死手 / 排障：`CLIENT_DEADMAN_*`、`CLIENT_LEGACY_CONTROL_ONLY` 等（见 `client/README.md`）

**输出**:
- 可执行文件: `client/build/RemoteDrivingClient`（构建目录以实际 `-B` 为准）

**架构与调用链文档**: [`client/docs/CALLCHAIN_AND_ARCHITECTURE.md`](client/docs/CALLCHAIN_AND_ARCHITECTURE.md)

---

### 3.2 Media 工程（流媒体服务器）

**位置**: `media/`

**依赖**:
- CMake 3.1.3+
- C++11 编译器
- FFmpeg（可选）
- OpenSSL

**编译**:
```bash
cd media
./build.sh
```

**运行**:
```bash
cd media
./run.sh
```

**调试**:
```bash
cd media
./debug.sh
```

**环境变量**:
- `BUILD_TYPE`: 构建类型（Debug/Release，默认 Release）

**输出**:
- 可执行文件: `ZLMediaKit/build/media_server`
- 配置文件: `ZLMediaKit/conf/config.ini`

**配置**:
编辑 `ZLMediaKit/conf/config.ini` 设置端口、WebRTC 等参数。

---

### 3.3 Vehicle-side 工程（车辆端）

**位置**: `Vehicle-side/`

**依赖**:
- CMake 3.16+
- C++17 编译器
- Paho MQTT C++（MQTT）
- ROS2（可选，Humble/Foxy）

**编译**:
```bash
cd Vehicle-side
./build.sh
```

**运行**:
```bash
cd Vehicle-side
./run.sh [mqtt_broker_url]
# 例如: ./run.sh mqtt://192.168.1.100:1883
```

**调试**:
```bash
cd Vehicle-side
./debug.sh [mqtt_broker_url]
```

**环境变量**:
- `BUILD_TYPE`: 构建类型（Debug/Release，默认 Release）
- ROS2 环境（如果启用）

**输出**:
- 可执行文件: `build/VehicleSide`

**功能**:
- 接收 MQTT 控制指令
- 控制车辆（方向盘、油门、刹车、档位）
- ROS2 桥接（可选）

---

## 4. 编译脚本说明

### 4.1 脚本功能

每个工程的 `build.sh` 脚本：
1. ✅ 自动检测依赖
2. ✅ 创建构建目录
3. ✅ 配置 CMake
4. ✅ 并行编译（使用所有 CPU 核心）
5. ✅ 生成编译数据库（compile_commands.json）

### 4.2 脚本特性

- **自动清理**: 每次编译前清理旧的构建目录
- **并行编译**: 使用 `make -j$(nproc)` 加速编译
- **错误处理**: 使用 `set -e` 遇到错误立即退出
- **环境检测**: 自动检测 Qt6、ROS2 等环境

---

## 5. 运行脚本说明

### 5.1 脚本功能

每个工程的 `run.sh` 脚本：
1. ✅ 检查可执行文件是否存在
2. ✅ 设置必要的环境变量
3. ✅ 运行程序

### 5.2 环境变量设置

**Client**:
- `LD_LIBRARY_PATH`: Qt6 库路径
- `DISPLAY`: X11 显示（GUI 应用）

**Media**:
- 使用默认配置或指定配置文件

**Vehicle-side**:
- MQTT Broker URL（命令行参数）

---

## 6. 调试脚本说明

### 6.1 GDB 调试

每个工程的 `debug.sh` 脚本：
1. ✅ 检查 GDB 是否安装
2. ✅ 设置环境变量
3. ✅ 启动 GDB

### 6.2 调试技巧

**设置断点**:
```bash
(gdb) break main
(gdb) break VehicleController::processCommand
```

**运行**:
```bash
(gdb) run
```

**查看变量**:
```bash
(gdb) print variable_name
```

**继续执行**:
```bash
(gdb) continue
```

**退出**:
```bash
(gdb) quit
```

---

## 7. 部署到不同机器

### 7.1 客户端台式机

```bash
# 1. 复制 client 目录到台式机
scp -r client/ user@desktop:/path/to/

# 2. 在台式机上编译
ssh user@desktop
cd /path/to/client
./build.sh

# 3. 运行
./run.sh
```

### 7.2 流媒体服务器

```bash
# 1. 复制 media 目录到服务器
scp -r media/ user@server:/path/to/

# 2. 在服务器上编译
ssh user@server
cd /path/to/media
./build.sh

# 3. 运行
./run.sh
```

### 7.3 车辆端（Jetson Orin）

```bash
# 1. 复制 Vehicle-side 目录到车辆
scp -r Vehicle-side/ user@vehicle:/path/to/

# 2. 在车辆上编译
ssh user@vehicle
cd /path/to/Vehicle-side
./build.sh

# 3. 运行（指定 MQTT Broker）
./run.sh mqtt://192.168.1.100:1883
```

---

## 8. 常见问题

### Q1: 编译失败 - 找不到 Qt6

**解决**:
```bash
export CMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64
# 或
export QT_GCC=/opt/Qt/6.8.0/gcc_64
./build.sh
```

### Q2: 编译失败 - 找不到 libdatachannel

**解决**:
```bash
# 安装 libdatachannel
git clone https://github.com/paullouisageneau/libdatachannel.git
cd libdatachannel
cmake -B build && cmake --build build && sudo cmake --install build
```

### Q3: 运行失败 - 找不到共享库

**解决**:
```bash
# 设置 LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/opt/Qt/6.8.0/gcc_64/lib:$LD_LIBRARY_PATH
./run.sh
```

### Q4: GUI 应用无法显示

**解决**:
```bash
# 设置 DISPLAY
export DISPLAY=:0
xhost +local:docker  # 如果使用 Docker
./run.sh
```

### Q5: MQTT 连接失败

**检查**:
1. MQTT Broker 是否运行
2. 网络连接是否正常
3. 防火墙设置
4. Broker URL 是否正确

---

## 9. 性能优化

### 9.1 编译优化

**Release 构建**:
```bash
BUILD_TYPE=Release ./build.sh
```

**并行编译**:
脚本已自动使用所有 CPU 核心（`make -j$(nproc)`）

### 9.2 运行优化

**客户端**:
- 使用 Release 构建减少内存占用
- 关闭不必要的日志输出

**媒体服务器**:
- 使用 Release 构建提高性能
- 调整 ZLMediaKit 配置参数

**车辆端**:
- 使用 Release 构建减少延迟
- 优化 MQTT 消息频率

---

## 10. 开发工作流

### 10.1 本地开发

```bash
# 1. 修改代码
vim client/src/main.cpp

# 2. 编译
make build-client

# 3. 运行测试
make run-client

# 4. 调试问题
make debug-client
```

### 10.2 远程部署

```bash
# 1. 本地编译测试
make build-client

# 2. 部署到目标机器
scp -r client/build/ user@target:/path/to/client/

# 3. 在目标机器运行
ssh user@target
cd /path/to/client
./run.sh
```

---

## 11. 脚本自定义

### 11.1 修改编译选项

编辑各工程的 `build.sh`，修改 CMake 参数：

```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -D自定义选项=ON
```

### 11.2 添加环境变量

在各工程的 `run.sh` 中添加：

```bash
export CUSTOM_VAR=value
```

---

## 12. 参考

- [CMake 文档](https://cmake.org/documentation/)
- [GDB 文档](https://www.gnu.org/software/gdb/documentation/)
- [Taskfile 文档](https://taskfile.dev/)
- [Makefile 文档](https://www.gnu.org/software/make/manual/)

---

**所有脚本已创建完成，可直接使用！** 🚀
