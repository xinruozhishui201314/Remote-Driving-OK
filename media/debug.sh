#!/bin/bash
# Media 工程调试脚本
# 使用 GDB 进行调试

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="ZLMediaKit/build"
EXECUTABLE="$BUILD_DIR/media_server"
CONFIG_FILE="ZLMediaKit/conf/config.ini"

# 检查是否已编译
if [ ! -f "$EXECUTABLE" ]; then
    echo "错误: 未找到可执行文件 $EXECUTABLE"
    echo "请先运行: ./build.sh"
    exit 1
fi

echo "=========================================="
echo "启动 GDB 调试 Media Server"
echo "=========================================="
echo ""

cd "$BUILD_DIR"

# 检查 GDB 是否可用
if ! command -v gdb > /dev/null 2>&1; then
    echo "错误: 未找到 gdb，请安装: sudo apt-get install gdb"
    exit 1
fi

# 准备 GDB 命令
GDB_CMDS=$(mktemp)
cat > "$GDB_CMDS" <<EOF
set args -c "$CONFIG_FILE"
run
EOF

# 启动 GDB
gdb -x "$GDB_CMDS" ./media_server

# 清理临时文件
rm -f "$GDB_CMDS"
