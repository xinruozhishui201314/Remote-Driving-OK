#!/bin/bash
# 修复脚本执行权限脚本
# 用于处理 noexec 挂载的文件系统

echo "=========================================="
echo "Fixing Script Permissions"
echo "=========================================="

WORKSPACE_DIR="${1:-/workspace}"

if [ ! -d "$WORKSPACE_DIR" ]; then
    echo "⚠ Workspace directory not found: $WORKSPACE_DIR"
    exit 1
fi

echo "Scanning for shell scripts in: $WORKSPACE_DIR"
SCRIPT_COUNT=0

# 查找并修复所有 .sh 文件的权限
while IFS= read -r -d '' script; do
    if [ -f "$script" ]; then
        chmod +x "$script" 2>/dev/null && {
            SCRIPT_COUNT=$((SCRIPT_COUNT + 1))
            echo "✓ Fixed: $script"
        } || echo "⚠ Failed: $script"
    fi
done < <(find "$WORKSPACE_DIR" -name "*.sh" -type f -print0 2>/dev/null)

echo ""
echo "=========================================="
echo "Fixed $SCRIPT_COUNT script(s)"
echo "=========================================="
echo ""
echo "Note: If filesystem is mounted with 'noexec',"
echo "      use 'bash script.sh' instead of './script.sh'"
echo ""
