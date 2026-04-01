# Client-Dev 完备镜像构建指南

## 概述

为了确保容器启动时是完备的运行环境，避免启动时进行安装和编译操作，需要预先构建完备的 `client-dev` 镜像。

## 完备镜像包含的内容

完备镜像 `remote-driving-client-dev:full` 应包含：

1. ✅ **Qt6 6.8.0**（基础镜像提供）
2. ✅ **libdatachannel**（WebRTC 库）
3. ✅ **FFmpeg 开发库**（视频解码）
4. ✅ **中文字体**（fonts-wqy-zenhei）
5. ✅ **编译工具**（cmake, make, g++）

## 构建方法

### 方法1：使用构建脚本（推荐）

```bash
# 构建完备镜像（包含中文字体和 FFmpeg）
bash scripts/build-client-dev-full-image.sh
```

该脚本会：
1. 在宿主机编译 libdatachannel
2. 启动临时容器
3. 复制 libdatachannel 到容器
4. **安装中文字体和 FFmpeg 开发库**
5. 提交为镜像并打 tag：`remote-driving-client-dev:full`

### 方法2：使用 Dockerfile

```bash
# 使用 Dockerfile 构建（需要网络连接拉取 libdatachannel）
docker compose -f docker-compose.yml -f docker-compose.client-dev.yml build client-dev
```

**注意**：Dockerfile 已更新，包含中文字体安装。

## 验证镜像是否完备

### 检查中文字体

```bash
docker run --rm remote-driving-client-dev:full dpkg -l fonts-wqy-zenhei | grep -q "^ii" && echo "✓ 中文字体已安装" || echo "✗ 中文字体未安装"
```

### 检查 libdatachannel

```bash
docker run --rm remote-driving-client-dev:full ls -la /opt/libdatachannel/lib/libdatachannel.so && echo "✓ libdatachannel 已安装" || echo "✗ libdatachannel 未安装"
```

### 检查 FFmpeg

```bash
docker run --rm remote-driving-client-dev:full pkg-config --exists libavcodec && echo "✓ FFmpeg 已安装" || echo "✗ FFmpeg 未安装"
```

## 启动时的行为

### 默认行为（推荐）

使用完备镜像启动时：

```bash
bash scripts/start-full-chain.sh manual
```

脚本会：
- ✅ **检查**中文字体（不安装，镜像应已预装）
- ✅ **检查**客户端是否已编译（如果未编译，则编译）
- ✅ 启动客户端

### 跳过编译步骤

如果希望客户端在启动时自动编译（不预先编译）：

```bash
bash scripts/start-full-chain.sh manual no-build
```

脚本会：
- ✅ **检查**中文字体（不安装）
- ⏭️ **跳过**编译步骤
- ✅ 启动客户端（客户端启动时会自动编译）

## 优化建议

### 1. 预编译客户端（可选）

如果希望镜像包含预编译的客户端，可以在构建镜像时编译：

```bash
# 在构建脚本中添加编译步骤
docker run --rm -v $(pwd)/client:/workspace/client:ro \
    remote-driving-client-dev:full \
    bash -c 'cd /workspace/client && mkdir -p build && cd build && \
    cmake .. -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug && \
    make -j4'
```

然后将编译产物复制到镜像中。

### 2. 使用多阶段构建

可以创建多阶段 Dockerfile，在构建阶段编译客户端，在运行阶段只包含运行时依赖。

## 当前状态

### Dockerfile.client-dev

已更新，包含：
- ✅ libdatachannel（从构建阶段复制）
- ✅ FFmpeg 开发库
- ✅ **中文字体（fonts-wqy-zenhei）**

### build-client-dev-full-image.sh

已包含：
- ✅ libdatachannel 安装
- ✅ **中文字体安装**
- ✅ FFmpeg 开发库安装

### start-full-chain.sh

已优化：
- ✅ `ensure_client_chinese_font()`：仅检查，不安装
- ✅ `ensure_client_built()`：检查是否已编译，如果未编译且未设置 `no-build`，则编译
- ✅ `start_client()`：优先使用已编译的客户端，如果未编译则自动编译

## 使用完备镜像

### 1. 构建完备镜像

```bash
# 首次构建（需要网络连接）
bash scripts/build-client-dev-full-image.sh
```

### 2. 启动全链路

```bash
# 使用完备镜像启动（不会安装字体，不会预先编译）
bash scripts/start-full-chain.sh manual no-build
```

### 3. 验证

启动后应该看到：
- ✅ `✓ 中文字体已安装（fonts-wqy-zenhei）`（而不是安装过程）
- ✅ 客户端启动时自动编译（如果使用 `no-build`）或使用已编译版本

## 故障排查

### 问题1：启动时仍显示安装中文字体

**原因**：镜像未包含中文字体

**解决**：
1. 重新构建镜像：`bash scripts/build-client-dev-full-image.sh`
2. 或使用 Dockerfile：`docker compose -f docker-compose.yml -f docker-compose.client-dev.yml build client-dev`

### 问题2：启动时仍显示编译客户端

**原因**：客户端未预编译

**解决**：
1. 使用 `no-build` 参数跳过编译步骤，让客户端启动时自动编译
2. 或预先编译客户端（见"优化建议"部分）

### 问题3：镜像构建失败

**排查步骤**：
1. 检查网络连接（需要拉取 libdatachannel）
2. 检查 Docker 权限
3. 查看构建日志：`docker compose -f docker-compose.yml -f docker-compose.client-dev.yml build client-dev --progress=plain`

## 相关文档

- `client/Dockerfile.client-dev` - Dockerfile 源码
- `scripts/build-client-dev-full-image.sh` - 构建脚本
- `docs/START_FULL_CHAIN_RESTART.md` - 启动脚本说明
