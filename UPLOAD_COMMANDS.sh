#!/bin/bash
# 一键上传到 GitHub 的命令序列
# 请先执行: sudo chown -R wqs:wqs /home/wqs/bigdata/Remote-Driving/.git

set -e

cd /home/wqs/bigdata/Remote-Driving

echo "=========================================="
echo "开始上传到 GitHub"
echo "=========================================="

# 检查权限
if [ ! -w .git ]; then
    echo "❌ 错误: .git 目录权限不足"
    echo ""
    echo "请先执行以下命令修复权限："
    echo "  sudo chown -R wqs:wqs /home/wqs/bigdata/Remote-Driving/.git"
    echo ""
    exit 1
fi

# 添加所有文件
echo "📝 添加文件到暂存区..."
git add -A

# 显示状态
echo ""
echo "将要提交的文件："
git status --short

# 提交
COMMIT_MSG="Update: Complete M0 milestone and add documentation

- Add M0 verification reports and summaries
- Add backend service implementation
- Add deployment configuration (Docker Compose)
- Add project documentation and structure
- Update .gitignore to exclude sensitive files
- Add scripts for verification and deployment"

echo ""
echo "📦 提交更改..."
git commit -m "$COMMIT_MSG"

# 推送
echo ""
echo "🚀 推送到 GitHub..."
git push -u origin master

echo ""
echo "=========================================="
echo "✅ 上传完成！"
echo "=========================================="
echo "仓库地址: https://github.com/xinruozhishui201314/Remote-Driving"
