#!/bin/bash
# 全局验证脚本：验证所有模块的编译和运行状态

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================"
echo "Verifying All Modules"
echo "========================================"
echo "Project directory: $PROJECT_DIR"
echo ""

FAILED=0

# 验证 backend
echo ""
echo "========================================"
echo "[1/3] Verifying Backend"
echo "========================================"
BACKEND_EXEC="$PROJECT_DIR/backend/build/teleop_backend"
if [ -f "$BACKEND_EXEC" ]; then
    echo "✓ Backend 可执行文件存在: $BACKEND_EXEC"
    ls -lh "$BACKEND_EXEC"
else
    echo "✗ Backend 可执行文件不存在: $BACKEND_EXEC"
    FAILED=$((FAILED + 1))
fi

# 验证 client
echo ""
echo "========================================"
echo "[2/3] Verifying Client"
echo "========================================"
CLIENT_EXEC="$PROJECT_DIR/client/build/client"
if [ -f "$CLIENT_EXEC" ]; then
    echo "✓ Client 可执行文件存在: $CLIENT_EXEC"
    ls -lh "$CLIENT_EXEC"
else
    echo "✗ Client 可执行文件不存在: $CLIENT_EXEC"
    FAILED=$((FAILED + 1))
fi

# 验证 Vehicle-side
echo ""
echo "========================================"
echo "[3/3] Verifying Vehicle-side"
echo "========================================"
VEHICLE_EXEC="$PROJECT_DIR/Vehicle-side/build/VehicleSide"
if [ -f "$VEHICLE_EXEC" ]; then
    echo "✓ Vehicle-side 可执行文件存在: $VEHICLE_EXEC"
    ls -lh "$VEHICLE_EXEC"
else
    echo "✗ Vehicle-side 可执行文件不存在: $VEHICLE_EXEC"
    FAILED=$((FAILED + 1))
fi

# 验证共享依赖
echo ""
echo "========================================"
echo "[4/4] Verifying Shared Dependencies"
echo "========================================"
DEPS_DIR="$PROJECT_DIR/deps"
if [ -d "$DEPS_DIR" ]; then
    echo "✓ 共享依赖目录存在: $DEPS_DIR"
    echo "  依赖项:"
    ls -1 "$DEPS_DIR"
else
    echo "✗ 共享依赖目录不存在: $DEPS_DIR"
    FAILED=$((FAILED + 1))
fi

# 总结
echo ""
echo "========================================"
echo "Verification Summary"
echo "========================================"
if [ $FAILED -eq 0 ]; then
    echo "✓ All modules verified successfully!"
    echo ""
    echo "可执行文件:"
    echo "  Backend:   $BACKEND_EXEC"
    echo "  Client:    $CLIENT_EXEC"
    echo "  Vehicle:   $VEHICLE_EXEC"
    exit 0
else
    echo "✗ Verification failed with $FAILED error(s)"
    exit 1
fi
