#!/bin/bash
# Vehicle-side 工程一键运行脚本（仅允许在 Docker 容器内执行）
# 宿主机请使用: make run-vehicle（将启动 vehicle 容器）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ ! -f "/.dockerenv" ] && [ -z "$CONTAINER_ID" ]; then
    echo "错误: 禁止在宿主机上运行车辆端。请使用: make run-vehicle" >&2
    echo "详见: docs/BUILD_AND_RUN_POLICY.md" >&2
    exit 1
fi

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
echo "启动 Vehicle-side Controller"
echo "=========================================="
echo "MQTT Broker: $MQTT_BROKER"
echo ""

cd "$BUILD_DIR"

# 运行程序
exec ./VehicleSide "$MQTT_BROKER"
