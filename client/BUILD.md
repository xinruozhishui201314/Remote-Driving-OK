# 编译和部署说明

## 环境要求

### 必需软件

- **Qt 6.8.0+** (gcc_64)
- **CMake 3.16+**
- **GCC 7+** 或 **Clang 5+**
- **pkg-config**

### Qt6 模块要求

- Qt6::Core
- Qt6::Quick
- Qt6::Network
- Qt6::Multimedia
- Qt6::Qml
- Qt6::Gui
- Qt6::WebSockets

## 编译步骤

### 1. 准备环境

```bash
# 设置 Qt6 环境变量
export CMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64
export PATH=/opt/Qt/6.8.0/gcc_64/bin:$PATH
export LD_LIBRARY_PATH=/opt/Qt/6.8.0/gcc_64/lib:$LD_LIBRARY_PATH
```

### 2. 创建构建目录

```bash
cd /home/wqs/bigdata/Remote-Driving/client
mkdir -p build
cd build
```

### 3. 配置 CMake

```bash
cmake .. \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=g++
```

### 4. 编译

```bash
make -j$(nproc)
```

### 5. 运行

```bash
./RemoteDrivingClient
```

## 在 Dev Container 中编译

如果使用 Dev Container：

```bash
# 容器内已配置好 Qt6 环境
cd /workspace/client
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH=$QT_GCC -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

## 带视频解码的 client-dev 镜像（libdatachannel + FFmpeg）

要启用四路 RTP/H.264 解码与 VideoRenderer 显示，需在含 **libdatachannel** 和 **FFmpeg 开发库** 的环境中构建：

### 使用 Docker 构建镜像并编译

```bash
# 项目根目录
cd /path/to/Remote-Driving

# 构建 client-dev 镜像（内装 libdatachannel + FFmpeg）
docker compose -f docker-compose.yml -f docker-compose.client-dev.yml build client-dev

# 进入容器并编译（若 build 目录权限异常，可在容器内 rm -rf build 后重建）
docker compose -f docker-compose.yml -f docker-compose.client-dev.yml run --rm client-dev
# 容器内：
cd /workspace/client
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
./build/RemoteDrivingClient
```

若镜像构建时出现 `Permission denied`（如 apt/var/lib/apt/lists），可尝试：

- 使用 root 运行 Docker：`sudo docker compose ...`
- 或本机安装依赖后直接编译（见下）

### 本机安装依赖后编译

```bash
# Ubuntu/Debian 示例
sudo apt-get install -y libavcodec-dev libavutil-dev libswscale-dev libavformat-dev pkg-config

# libdatachannel 需从源码安装
# 见 https://github.com/paullouisageneau/libdatachannel#building

cd client
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
```

CMake 会自动检测 FFmpeg 与 libdatachannel；未找到时仍可编译，但无视频解码与 WebRTC 媒体连接。

## client-dev 镜像内已装 WebRTC 库（推荐）

默认 **client-dev 使用 Dockerfile.client-dev 构建**，镜像内已安装 **libdatachannel**（多阶段构建，无需在 Qt 基础镜像中 apt 大量工具），启动容器即完备。首次或 Dockerfile 变更后执行：

```bash
docker compose build client-dev
```

之后 `make e2e-full` 或 `make run` 时，容器内编译客户端会自动找到 libdatachannel，连接车端后四路显示「已连接」并接收 RTP。若镜像内 FFmpeg 安装成功，还会启用 H.264 解码与画面显示。

## 可选：宿主机安装 libdatachannel 并挂载（无需改镜像时）

若暂时不想构建 client-dev 镜像，可在宿主机安装 libdatachannel 并挂载到容器（需在 docker-compose 中恢复对 `client/deps/libdatachannel-install` 的挂载并改用仅 Qt 的 image）：

```bash
bash scripts/install-libdatachannel-for-client.sh
```

当前默认为主 compose 构建完备镜像，不再依赖该挂载。

## 常见问题

### Q1: 找不到 Qt6

**错误**: `Could not find a package configuration file provided by "Qt6"`

**解决**:
```bash
export CMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64
# 或使用完整路径
cmake .. -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64
```

### Q2: 找不到 Qt6::Multimedia

**错误**: `Could not find Qt6 component "Multimedia"`

**解决**: 确保安装了 Qt6 Multimedia 模块
```bash
# 检查模块是否存在
ls /opt/Qt/6.8.0/gcc_64/lib/cmake/Qt6Multimedia/
```

### Q3: QML 文件找不到

**错误**: `qrc:/qml/main.qml: No such file or directory`

**解决**: 确保资源文件已正确添加到 CMakeLists.txt，并重新运行 CMake

### Q4: 链接错误

**错误**: `undefined reference to Qt6::...`

**解决**: 检查 CMakeLists.txt 中的 `target_link_libraries` 是否包含所有必需的模块

## 调试

### 启用调试输出

```bash
QT_LOGGING_RULES="*.debug=true" ./RemoteDrivingClient
```

### 使用 GDB 调试

```bash
gdb ./RemoteDrivingClient
(gdb) run
```

### 检查 Qt 版本

```bash
qmake --version
```

## 部署

### 打包依赖

使用 `linuxdeployqt` 或手动复制 Qt 库：

```bash
# 使用 linuxdeployqt
linuxdeployqt RemoteDrivingClient -qmldir=../qml

# 或手动复制
mkdir -p deploy/lib
cp /opt/Qt/6.8.0/gcc_64/lib/libQt6Core.so.6 deploy/lib/
# ... 复制其他依赖库
```

### 创建 AppImage

```bash
# 使用 linuxdeployqt 创建 AppImage
linuxdeployqt RemoteDrivingClient -appimage -qmldir=../qml
```

## 性能优化

### Release 构建

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 启用编译器优化

```bash
cmake .. -DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=native"
```
