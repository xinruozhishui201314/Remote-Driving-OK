#!/bin/bash
# 验证测试流程：Debug 构建 + 运行（可选 GDB）
# 用法: ./verify-and-run.sh        # Debug 构建并运行
#       ./verify-and-run.sh --gdb  # Debug 构建并用 GDB 运行（崩溃时 bt）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "验证流程：Debug 构建 + 运行"
echo "=========================================="

export BUILD_TYPE=Debug
bash build.sh

if [ "$1" = "--gdb" ] || [ "$1" = "-g" ]; then
    echo ""
    echo "使用 GDB 启动（崩溃时可 bt 精确定位）..."
    bash debug.sh --bt 2>&1 || true
else
    echo ""
    echo "启动客户端（测试：账户 123 / 密码 123，选 VIN 123456789 进入主页面）..."
    bash run.sh
fi
