#!/bin/bash
# Client 模块独立编译脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================"
echo "Building Client Module"
echo "========================================"
echo "Project directory: $PROJECT_DIR"
echo ""

cd "$PROJECT_DIR"

# 检查依赖目录
if [ ! -d "../deps" ]; then
    echo "错误: 找不到共享依赖目录 ../deps"
    echo "请确保项目根目录存在 deps/ 目录"
    exit 1
fi

# 检查源码目录
if [ ! -d "src" ]; then
    echo "错误: 找不到源码目录 src/"
    exit 1
fi

# 检查QML目录
if [ ! -d "qml" ]; then
    echo "警告: 找不到 QML 目录 qml/，可能会影响UI功能"
fi

# 创建构建目录
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    echo "清理旧的构建目录..."
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"

# 配置CMake
echo ""
echo "配置 CMake..."
cd "$BUILD_DIR"

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

# 如果有额外的CMake参数，传递给cmake
if [ -n "$CMAKE_ARGS_EXTRA" ]; then
    CMAKE_ARGS+=($CMAKE_ARGS_EXTRA)
fi

cmake .. "${CMAKE_ARGS[@]}"

# 编译
echo ""
echo "开始编译..."
CPU_COUNT=$(nproc)
echo "使用 $CPU_COUNT 个CPU核心编译"
make -j${CPU_COUNT}

echo ""
echo "========================================"
echo "Client 编译完成"
echo "========================================"
echo ""
echo "可执行文件位置: $BUILD_DIR/client"
echo ""
echo "运行命令:"
echo "  cd $BUILD_DIR && ./client"
echo "  或使用: ./scripts/run.sh"
echo ""
