#!/bin/bash
# Vehicle-side 工程调试脚本
# 使用 GDB 进行调试

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build"
EXECUTABLE="$BUILD_DIR/VehicleSide"

# 检查是否已编译
if [ ! -f "$EXECUTABLE" ]; then
    echo "错误: 未找到可执行文件 $EXECUTABLE"
    echo "请先运行: ./build.sh"
    exit 1
fi

# MQTT Broker URL（默认值）
MQTT_BROKER="${1:-mqtt://192.168.1.100:1883}"

echo "=========================================="
echo "启动 GDB 调试 Vehicle-side Controller"
echo "=========================================="
echo ""

cd "$BUILD_DIR"

# 检查 GDB 是否可用
if ! command -v gdb > /dev/null 2>&1; then
    echo "错误: 未找到 gdb，请安装: sudo apt-get install gdb"
    exit 1
fi

# 启动 GDB
gdb --args ./VehicleSide "$MQTT_BROKER"
