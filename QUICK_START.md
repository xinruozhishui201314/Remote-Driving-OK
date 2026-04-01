# 快速开始指南

## 一键编译和运行

### Client（客户端台式机）

```bash
cd client
./build.sh    # 编译
./run.sh      # 运行
./debug.sh    # 调试
```

### Media（流媒体服务器）

```bash
cd media
./build.sh    # 编译
./run.sh      # 运行
./debug.sh    # 调试
```

### Vehicle-side（车辆端）

```bash
cd Vehicle-side
./build.sh                    # 编译
./run.sh mqtt://broker:1883   # 运行（指定 MQTT Broker）
./debug.sh mqtt://broker:1883 # 调试
```

## 使用 Makefile

```bash
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

## 使用 Taskfile

```bash
# 安装 Task
go install github.com/go-task/task/v3/cmd/task@latest

# 编译客户端
task client:build

# 运行客户端
task client:run

# 编译所有工程
task build
```

## 部署到不同机器

### 1. 客户端台式机

```bash
# 复制到台式机
scp -r client/ user@desktop:/path/to/

# 在台式机上
ssh user@desktop
cd /path/to/client
./build.sh && ./run.sh
```

### 2. 流媒体服务器

```bash
# 复制到服务器
scp -r media/ user@server:/path/to/

# 在服务器上
ssh user@server
cd /path/to/media
./build.sh && ./run.sh
```

### 3. 车辆端（Jetson Orin）

```bash
# 复制到车辆
scp -r Vehicle-side/ user@vehicle:/path/to/

# 在车辆上
ssh user@vehicle
cd /path/to/Vehicle-side
./build.sh
./run.sh mqtt://192.168.1.100:1883
```

## 环境要求

### Client
- Qt 6.8.0+
- libdatachannel
- Paho MQTT C++

### Media
- CMake 3.1.3+
- C++11 编译器
- FFmpeg（可选）

### Vehicle-side
- CMake 3.16+
- C++17 编译器
- Paho MQTT C++
- ROS2（可选）

## 故障排查

### 编译失败
- 检查依赖是否安装
- 查看错误信息
- 参考 BUILD_GUIDE.md

### 运行失败
- 检查可执行文件是否存在
- 检查环境变量设置
- 查看日志输出

### 调试问题
- 使用 `./debug.sh` 启动 GDB
- 设置断点
- 查看变量值

详细说明请参考 [BUILD_GUIDE.md](./BUILD_GUIDE.md)。
