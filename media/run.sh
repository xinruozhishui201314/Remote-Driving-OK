#!/bin/bash
# Media 工程一键运行脚本（禁止在宿主机使用）
# 宿主机请使用: make run-media（启动 zlmediakit 容器）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ ! -f "/.dockerenv" ] && [ -z "$CONTAINER_ID" ]; then
    echo "错误: 禁止在宿主机上运行媒体服务。请使用: make run-media" >&2
    echo "详见: docs/BUILD_AND_RUN_POLICY.md" >&2
    exit 1
fi

BUILD_DIR="ZLMediaKit/build"
EXECUTABLE="$BUILD_DIR/media_server"
CONFIG_FILE="ZLMediaKit/conf/config.ini"

# 检查是否已编译
if [ ! -f "$EXECUTABLE" ]; then
    echo "错误: 未找到可执行文件 $EXECUTABLE"
    echo "请先运行: ./build.sh"
    exit 1
fi

# 检查配置文件
if [ ! -f "$CONFIG_FILE" ]; then
    echo "警告: 未找到配置文件 $CONFIG_FILE"
    echo "将使用默认配置"
    CONFIG_FILE=""
fi

echo "=========================================="
echo "启动 ZLMediaKit Media Server"
echo "=========================================="
echo ""

cd "$BUILD_DIR"

# 运行程序
if [ -n "$CONFIG_FILE" ]; then
    exec ./media_server -c "$CONFIG_FILE" "$@"
else
    exec ./media_server "$@"
fi
