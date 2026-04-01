# Client-Dev 镜像完整构建指南

## 概述

本文档说明如何构建一个**完整的** client-dev Docker 镜像，确保镜像包含所有运行时依赖，避免容器重启时出现问题。

## 问题背景

在之前的实现中，存在以下问题：

1. **中文字体未安装**：虽然 Dockerfile 中有 `fonts-wqy-zenhei`，但使用了 `|| true`，导致安装失败时不会报错。
2. **客户端在运行时编译**：每次启动都在编译，而不是使用预编译的二进制。
3. **镜像构建不完整**：缺少验证步骤，无法确保镜像包含所有必需依赖。

## 解决方案

### 1. 修复 Dockerfile

**文件**：`client/Dockerfile.client-dev`

**改进内容**：
- 移除 `|| true`，确保依赖安装失败时构建失败。
- 添加 `fc-cache -fv`，更新字体缓存。
- 添加构建工具（cmake、make、g++），确保可以编译客户端。
- 预编译客户端（构建镜像时编译，避免运行时编译）。

**关键改进**：
```dockerfile
# 之前（可能静默失败）
RUN ... || true

# 现在（必须成功）
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        fonts-wqy-zenhei \
        libavcodec-dev \
        ... \
    && rm -rf /var/lib/apt/lists/* \
    && fc-cache -fv \
    && echo "✓ 依赖已安装"
```

### 2. 更新构建脚本

**文件**：`scripts/build-client-dev-full-image.sh`

**改进内容**：
- 移除 `|| true`，确保依赖安装失败时脚本失败。
- 添加错误处理，安装失败时退出。

### 3. 创建验证脚本

**文件**：`scripts/verify-client-dev-image.sh`

**功能**：
- 检查镜像是否存在。
- 验证所有必需依赖是否已安装。
- 验证环境变量是否正确设置。
- 验证预编译客户端是否存在。

## 构建步骤

### 方法 1：使用 Dockerfile（推荐）

```bash
# 构建镜像
docker compose -f docker-compose.yml -f docker-compose.client-dev.yml build client-dev

# 验证镜像
bash scripts/verify-client-dev-image.sh remote-driving-client-dev:full
```

### 方法 2：使用构建脚本

```bash
# 构建完整镜像（包含 libdatachannel）
bash scripts/build-client-dev-full-image.sh remote-driving-client-dev:full

# 验证镜像
bash scripts/verify-client-dev-image.sh remote-driving-client-dev:full
```

## 验证镜像完整性

### 快速验证

```bash
# 验证镜像
bash scripts/verify-client-dev-image.sh remote-driving-client-dev:full
```

### 手动验证

```bash
# 启动临时容器
docker run -it --rm remote-driving-client-dev:full bash

# 在容器内检查
# 1. 检查中文字体
dpkg -l fonts-wqy-zenhei
fc-list | grep -i "wqy\|wenquanyi"

# 2. 检查 FFmpeg
dpkg -l | grep -E "libavcodec|libavutil|libswscale|libavformat"

# 3. 检查构建工具
which cmake make g++ pkg-config

# 4. 检查 libdatachannel
ls -la /opt/libdatachannel/lib/

# 5. 检查 Qt6
ls -la /opt/Qt/6.8.0/gcc_64/

# 6. 检查预编译客户端
ls -la /tmp/client-build/RemoteDrivingClient
```

## 镜像内容清单

### 必需依赖

| 依赖 | 检查方法 | 说明 |
|------|---------|------|
| **中文字体** | `dpkg -l fonts-wqy-zenhei` | 必需，用于 Qt 界面显示中文 |
| **FFmpeg 开发库** | `dpkg -l \| grep libav` | 必需，用于视频解码 |
| **构建工具** | `which cmake make g++` | 必需，用于编译客户端 |
| **Qt6** | `ls /opt/Qt/6.8.0/gcc_64` | 必需，Qt 框架 |
| **libdatachannel** | `ls /opt/libdatachannel/lib/` | 推荐，WebRTC 库 |

### 预编译文件

| 文件 | 路径 | 说明 |
|------|------|------|
| **客户端可执行文件** | `/tmp/client-build/RemoteDrivingClient` | 预编译的客户端（可选，运行时可重新编译） |

### 环境变量

| 变量 | 值 | 说明 |
|------|-----|------|
| `CMAKE_PREFIX_PATH` | `/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel` | CMake 查找路径 |
| `LD_LIBRARY_PATH` | `/opt/Qt/6.8.0/gcc_64/lib` | 动态库路径 |
| `PATH` | `/opt/Qt/6.8.0/gcc_64/bin` | Qt 工具路径 |

## 使用完整镜像

### 启动容器

```bash
# 使用完整镜像启动
docker compose -f docker-compose.yml -f docker-compose.client-dev.yml up -d client-dev

# 验证容器环境
docker compose exec client-dev bash -c '
    echo "检查中文字体:"
    dpkg -l fonts-wqy-zenhei | grep ^ii
    
    echo "检查 FFmpeg:"
    pkg-config --modversion libavcodec
    
    echo "检查预编译客户端:"
    ls -lh /tmp/client-build/RemoteDrivingClient 2>/dev/null || echo "未预编译"
'
```

### 运行客户端

```bash
# 使用预编译的客户端（如果存在）
docker compose exec client-dev /tmp/client-build/RemoteDrivingClient

# 或使用 start-full-chain.sh（会自动检测并使用预编译版本）
bash scripts/start-full-chain.sh manual
```

## 故障排查

### 问题 1：中文字体未安装

**症状**：`No Chinese font found, Chinese text may not display correctly`

**排查步骤**：
1. 检查镜像：`bash scripts/verify-client-dev-image.sh`
2. 重新构建镜像：`docker compose -f docker-compose.yml -f docker-compose.client-dev.yml build client-dev`
3. 验证字体：`docker run --rm remote-driving-client-dev:full fc-list | grep -i wqy`

### 问题 2：客户端在运行时编译

**症状**：每次启动都在编译，启动时间很长

**排查步骤**：
1. 检查预编译客户端：`docker run --rm remote-driving-client-dev:full ls -lh /tmp/client-build/RemoteDrivingClient`
2. 如果不存在，重新构建镜像（Dockerfile 会预编译）
3. 或使用 `start-full-chain.sh`，它会优先使用预编译版本

### 问题 3：镜像构建失败

**症状**：`apt-get install` 失败

**排查步骤**：
1. 检查网络连接：`docker run --rm ubuntu:22.04 apt-get update`
2. 检查 Dockerfile：确保没有 `|| true` 掩盖错误
3. 查看构建日志：`docker compose build client-dev 2>&1 | tee build.log`

### 问题 4：容器重启后环境丢失

**症状**：容器重启后，依赖或预编译文件丢失

**原因**：依赖或文件存储在容器层，而不是镜像层

**解决方案**：
1. 确保依赖在 Dockerfile 中安装（写入镜像层）
2. 使用数据卷存储预编译文件（如果需要持久化）
3. 使用完整镜像，避免运行时安装

## 最佳实践

1. **构建时安装依赖**：所有运行时依赖应在 Dockerfile 中安装，而不是运行时安装。
2. **验证镜像完整性**：构建后使用 `verify-client-dev-image.sh` 验证镜像。
3. **预编译客户端**：在 Dockerfile 中预编译客户端，避免运行时编译。
4. **使用完整镜像**：使用 `build-client-dev-full-image.sh` 构建包含所有依赖的完整镜像。
5. **避免运行时操作**：不要在启动脚本中安装依赖或编译代码。

## 总结

通过修复 Dockerfile、更新构建脚本和创建验证脚本，现在可以：

1. ✅ **确保依赖安装成功**：移除 `|| true`，构建失败时会报错。
2. ✅ **预编译客户端**：在镜像构建时编译，避免运行时编译。
3. ✅ **验证镜像完整性**：使用验证脚本确保镜像包含所有必需依赖。
4. ✅ **避免容器重启问题**：所有依赖都在镜像层，容器重启不会丢失。

这确保了无论 Docker 容器如何重启，运行环境都能保持一致，满足需求："要解决docker镜像运行环境的问题，并保存镜像避免重启容器镜像时出问题"。
