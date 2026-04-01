#!/bin/bash
# 工作区打开时自动执行的脚本
# 此脚本会被 Cursor/VSCode 在打开工作区时调用（如果配置了）

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=========================================="
echo "Workspace opened - Setting up environment"
echo "=========================================="

# 运行预启动脚本
bash "$SCRIPT_DIR/pre-start.sh"

echo ""
echo "Ready to open in container!"
echo "Press F1 → 'Dev Containers: Reopen in Container'"
