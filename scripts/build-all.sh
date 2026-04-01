#!/bin/bash
# 全局构建脚本：编译所有模块

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================"
echo "Building All Modules"
echo "========================================"
echo "Project directory: $PROJECT_DIR"
echo ""

# 检查共享依赖目录
if [ ! -d "$PROJECT_DIR/deps" ]; then
    echo "错误: 找不到共享依赖目录 deps/"
    exit 1
fi

# 编译backend
echo ""
echo "========================================"
echo "[1/3] Building Backend"
echo "========================================"
cd "$PROJECT_DIR/backend"
if [ -f "scripts/build.sh" ]; then
    ./scripts/build.sh
else
    echo "错误: 未找到 backend/scripts/build.sh"
    exit 1
fi

# 编译client
echo ""
echo "========================================"
echo "[2/3] Building Client"
echo "========================================"
cd "$PROJECT_DIR/client"
if [ -f "scripts/build.sh" ]; then
    ./scripts/build.sh
else
    echo "错误: 未找到 client/scripts/build.sh"
    exit 1
fi

# 编译Vehicle-side
echo ""
echo "========================================"
echo "[3/3] Building Vehicle-side"
echo "========================================"
cd "$PROJECT_DIR/Vehicle-side"
if [ -f "build.sh" ]; then
    ./build.sh
else
    echo "错误: 未找到 Vehicle-side/build.sh"
    exit 1
fi

echo ""
echo "========================================"
echo "All modules built successfully!"
echo "========================================"
echo ""
echo "可执行文件位置:"
echo "  Backend:   $PROJECT_DIR/backend/build/teleop_backend"
echo "  Client:    $PROJECT_DIR/client/build/client"
echo "  Vehicle:   $PROJECT_DIR/Vehicle-side/build/VehicleSide"
echo ""
echo "运行各模块:"
echo "  Backend:   cd backend && ./scripts/run.sh"
echo "  Client:    cd client && ./scripts/run.sh"
echo "  Vehicle:   cd Vehicle-side && ./run.sh"
echo ""
