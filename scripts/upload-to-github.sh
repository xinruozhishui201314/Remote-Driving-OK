#!/bin/bash
# 上传工程到 GitHub 的脚本
# 使用方法: ./scripts/upload-to-github.sh [commit_message]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

echo "=========================================="
echo "准备上传工程到 GitHub"
echo "=========================================="

# 1. 修复 .git 目录权限（如果需要）
if [ -d .git ] && [ "$(stat -c '%U' .git)" != "$(whoami)" ]; then
    echo "⚠️  检测到 .git 目录权限问题，需要修复..."
    echo "请运行以下命令修复权限："
    echo "  sudo chown -R $(whoami):$(whoami) .git"
    echo ""
    read -p "是否已修复权限？(y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "请先修复权限后重新运行此脚本"
        exit 1
    fi
fi

# 2. 检查远程仓库配置
if ! git remote get-url origin >/dev/null 2>&1; then
    echo "❌ 未配置远程仓库"
    echo "请先配置远程仓库："
    echo "  git remote add origin git@github.com:your-username/Remote-Driving.git"
    exit 1
fi

REMOTE_URL=$(git remote get-url origin)
echo "✓ 远程仓库: $REMOTE_URL"

# 3. 检查是否有未提交的更改
if [ -z "$(git status --porcelain)" ]; then
    echo "✓ 工作区干净，没有需要提交的更改"
else
    echo "📝 发现未提交的更改："
    git status --short
    
    # 4. 添加所有文件
    echo ""
    echo "添加所有文件到暂存区..."
    git add -A
    
    # 5. 显示将要提交的文件
    echo ""
    echo "将要提交的文件："
    git status --short
    
    # 6. 提交更改
    COMMIT_MSG="${1:-Update: Add latest changes and documentation}"
    echo ""
    echo "提交更改 (消息: $COMMIT_MSG)..."
    git commit -m "$COMMIT_MSG"
fi

# 7. 检查当前分支
CURRENT_BRANCH=$(git branch --show-current)
echo "当前分支: $CURRENT_BRANCH"

# 8. 推送到远程仓库
echo ""
echo "推送到 GitHub..."
if git push -u origin "$CURRENT_BRANCH"; then
    echo ""
    echo "=========================================="
    echo "✅ 成功上传到 GitHub!"
    echo "=========================================="
    echo "仓库地址: $REMOTE_URL"
    echo "分支: $CURRENT_BRANCH"
else
    echo ""
    echo "=========================================="
    echo "❌ 推送失败"
    echo "=========================================="
    echo "可能的原因："
    echo "1. 网络连接问题"
    echo "2. SSH 密钥未配置"
    echo "3. 权限不足"
    echo ""
    echo "请检查："
    echo "  - SSH 密钥: ssh -T git@github.com"
    echo "  - 远程仓库权限"
    exit 1
fi
