#!/bin/bash
# Client 工程调试脚本
# 用法:
#   ./debug.sh          - 交互式 GDB
#   ./debug.sh --bt     - 自动运行直到崩溃并打印堆栈 (batch)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build"
EXECUTABLE="$BUILD_DIR/RemoteDrivingClient"

if [ ! -f "$EXECUTABLE" ]; then
    echo "错误: 未找到可执行文件 $EXECUTABLE"
    echo "请先运行: ./build.sh"
    exit 1
fi

if [ -d "/opt/Qt/6.8.0/gcc_64" ]; then
    export LD_LIBRARY_PATH="/opt/Qt/6.8.0/gcc_64/lib:${LD_LIBRARY_PATH:-}"
    export PATH="/opt/Qt/6.8.0/gcc_64/bin:${PATH:-}"
elif [ -n "$QT_GCC" ] && [ -d "$QT_GCC" ]; then
    export LD_LIBRARY_PATH="$QT_GCC/lib:${LD_LIBRARY_PATH:-}"
fi
[ -z "$DISPLAY" ] && export DISPLAY=:0
export QT_QPA_PLATFORM=xcb

cd "$BUILD_DIR"

if ! command -v gdb > /dev/null 2>&1; then
    echo "错误: 未找到 gdb"
    echo "安装: sudo apt-get update && sudo apt-get install -y gdb"
    exit 1
fi

if [ "$1" = "--bt" ] || [ "$1" = "--backtrace" ]; then
    echo "=========================================="
    echo "GDB 自动运行直至崩溃并打印堆栈"
    echo "=========================================="
    gdb -batch \
        -ex "set pagination off" \
        -ex "run" \
        -ex "bt full" \
        -ex "info registers" \
        -ex "quit" \
        --args ./RemoteDrivingClient 2>&1 || true
else
    echo "=========================================="
    echo "启动 GDB 调试"
    echo "=========================================="
    gdb --args ./RemoteDrivingClient
fi
