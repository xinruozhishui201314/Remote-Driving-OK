#!/bin/bash
# Vehicle-side 工程一键编译脚本（仅允许在 Docker 容器内执行）
# 宿主机请使用: make build-vehicle（将构建 vehicle 镜像，容器内编译）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ ! -f "/.dockerenv" ] && [ -z "$CONTAINER_ID" ]; then
    echo "错误: 禁止在宿主机上编译车辆端。请使用: make build-vehicle" >&2
    echo "详见: docs/BUILD_AND_RUN_POLICY.md" >&2
    exit 1
fi

echo "=========================================="
echo "Building Vehicle-side Controller"
echo "=========================================="

# 检测 ROS2（可选）
if [ -f "/opt/ros/humble/setup.bash" ] || [ -f "/opt/ros/foxy/setup.bash" ]; then
    echo "检测到 ROS2，将启用 ROS2 支持"
    export ENABLE_ROS2=ON
else
    echo "未检测到 ROS2，将编译独立版本"
    export ENABLE_ROS2=OFF
fi

# 创建构建目录
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    echo "清理旧的构建目录..."
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 配置 CMake
echo ""
echo "配置 CMake..."

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

# 如果启用 ROS2，source ROS2 环境
if [ "$ENABLE_ROS2" = "ON" ]; then
    if [ -f "/opt/ros/humble/setup.bash" ]; then
        source /opt/ros/humble/setup.bash
    elif [ -f "/opt/ros/foxy/setup.bash" ]; then
        source /opt/ros/foxy/setup.bash
    fi
    CMAKE_ARGS+=(-DENABLE_ROS2=ON)
fi

cmake .. "${CMAKE_ARGS[@]}"

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
echo "可执行文件位置: $BUILD_DIR/VehicleSide"
echo ""
echo "运行命令:"
echo "  cd $BUILD_DIR && ./VehicleSide [mqtt_broker_url]"
echo "  或使用: ./run.sh"
echo ""
