# Client-Dev 镜像运行环境修复

## 问题描述

容器启动时需要安装额外的库（如 `mosquitto-clients`），导致：
1. 每次启动都需要执行 `apt-get install`
2. 启动时间延长
3. 运行环境不稳定（依赖网络和包管理器）

## 根本原因

镜像 `remote-driving-client-dev:full` 构建时未包含所有运行时依赖，特别是 `mosquitto-clients`。

## 修复方案

### 方法 1：更新现有镜像（已执行）

1. 在运行中的容器内安装缺失的依赖：
   ```bash
   docker exec -u root teleop-client-dev bash -c \
     "apt-get update && apt-get install -y --no-install-recommends mosquitto-clients"
   ```

2. 提交容器为新镜像：
   ```bash
   docker commit teleop-client-dev remote-driving-client-dev:full
   ```

3. 验证镜像完备性：
   ```bash
   bash scripts/verify-client-dev-image-complete.sh
   ```

### 方法 2：修复 Dockerfile（推荐用于未来构建）

**文件**：`client/Dockerfile.client-dev`

**修改**：确保以 root 用户运行安装命令

```dockerfile
# 在 Qt 镜像内安装 FFmpeg 开发库、mosquitto-clients、中文字体（必须成功，否则构建失败）
# mosquitto-clients：无 Paho 时客户端用 mosquitto_pub 发送 start_stream，车端才能收到并启动推流
# 确保以 root 用户运行（基础镜像可能不是 root）
USER root
RUN mkdir -p /var/lib/apt/lists/partial \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        libavcodec-dev \
        libavutil-dev \
        libswscale-dev \
        libavformat-dev \
        pkg-config \
        fonts-wqy-zenhei \
        cmake \
        make \
        g++ \
        mosquitto-clients \
    && rm -rf /var/lib/apt/lists/* \
    && fc-cache -fv \
    && echo "✓ FFmpeg、mosquitto-clients、中文字体已安装"
```

**注意**：基础镜像 `docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt` 默认用户是 `user`（非 root），需要显式指定 `USER root`。

## 验证

### 自动化验证脚本

**文件**：`scripts/verify-client-dev-image-complete.sh`

**验证项**：
1. ✓ mosquitto-clients（`mosquitto_pub` 命令）
2. ✓ 中文字体（`fonts-wqy-zenhei`）
3. ✓ FFmpeg 开发库（`libavcodec-dev` 等）
4. ✓ Qt6（`/opt/Qt/6.8.0/gcc_64`）
5. ✓ libdatachannel（`/opt/libdatachannel`）
6. ✓ 编译工具（`cmake`, `make`, `g++`）

**运行**：
```bash
bash scripts/verify-client-dev-image-complete.sh
```

### 手动验证

1. 启动容器：
   ```bash
   docker run --rm remote-driving-client-dev:full bash -c "command -v mosquitto_pub"
   ```

2. 检查包列表：
   ```bash
   docker run --rm remote-driving-client-dev:full bash -c "dpkg -l | grep mosquitto"
   ```

3. 运行启动脚本：
   ```bash
   bash scripts/start-full-chain.sh manual
   ```
   应该看到：`✓ mosquitto_pub 已可用（镜像中已预装）`

## 修改文件清单

1. `client/Dockerfile.client-dev`
   - 添加 `USER root` 确保安装命令以 root 执行
   - 添加 `mkdir -p /var/lib/apt/lists/partial` 创建必要目录

2. `scripts/verify-client-dev-image-complete.sh`（新增）
   - 自动化验证脚本

3. `scripts/start-full-chain.sh`
   - 已更新：移除运行时安装逻辑，仅检查是否已安装

## 技术要点

### Docker 镜像用户

- **基础镜像用户**：`docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt` 默认用户是 `user`（非 root）
- **安装包需要 root**：`apt-get install` 需要 root 权限
- **解决方案**：在 Dockerfile 中显式指定 `USER root`，或在运行时使用 `docker exec -u root`

### 镜像完备性

- **所有运行时依赖应在构建时安装**
- **启动脚本不应执行 `apt-get install`**
- **镜像应包含所有必要的工具和库**

### 验证策略

- **自动化验证**：使用脚本检查所有依赖
- **手动验证**：启动容器并测试功能
- **日志验证**：检查启动日志确认无安装操作

## 验证结论

✓✓✓ **修复已完成并通过验证**

- ✓ `mosquitto-clients` 已安装到镜像中
- ✓ 镜像完备性验证通过
- ✓ 启动脚本不再执行运行时安装
- ✓ 容器启动后可直接使用，无需安装额外库

**镜像现在包含所有运行时依赖，启动容器后可直接使用。**
