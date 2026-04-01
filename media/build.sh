#!/bin/bash
# Media 工程一键编译脚本（禁止在宿主机使用；流媒体请用现成镜像）
# 宿主机请使用: make run-media（启动 zlmediakit 容器，无需编译）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ ! -f "/.dockerenv" ] && [ -z "$CONTAINER_ID" ]; then
    echo "错误: 禁止在宿主机上编译媒体服务。请使用: make run-media 启动 zlmediakit 容器。" >&2
    echo "详见: docs/BUILD_AND_RUN_POLICY.md" >&2
    exit 1
fi

echo "=========================================="
echo "Building ZLMediaKit Media Server"
echo "=========================================="

# 进入 ZLMediaKit 目录
if [ ! -d "ZLMediaKit" ]; then
    echo "错误: 未找到 ZLMediaKit 目录"
    exit 1
fi

cd ZLMediaKit

# 创建构建目录
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    echo "清理旧的构建目录..."
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 配置 CMake（启用 WebRTC 和服务器功能）
echo ""
echo "配置 CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" \
    -DENABLE_WEBRTC=ON \
    -DENABLE_SERVER=ON \
    -DENABLE_API=ON \
    -DENABLE_HLS=ON \
    -DENABLE_MP4=ON \
    -DENABLE_RTPPROXY=ON \
    -DENABLE_SRT=ON \
    -DENABLE_FFMPEG=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 编译
echo ""
echo "开始编译..."
CPU_COUNT=$(nproc)
make -j${CPU_COUNT}

echo ""
echo "=========================================="
echo "编译完成！"
echo "=========================================="
echo ""
echo "可执行文件位置: $BUILD_DIR/media_server"
echo "配置文件位置: ../conf/config.ini"
echo ""
echo "运行命令:"
echo "  cd $BUILD_DIR && ./media_server -c ../conf/config.ini"
echo "  或使用: ./run.sh"
echo ""
