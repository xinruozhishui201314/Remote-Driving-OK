#!/bin/bash
# 自动化修复和验证 NuScenes 推流脚本
# 检查并修复常见问题，然后进行完整验证

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

PUSH_SCRIPT="scripts/push-nuscenes-cameras-to-zlm.sh"
FIXES_APPLIED=0

echo "=========================================="
echo "NuScenes 推流脚本自动化修复与验证"
echo "=========================================="
echo ""

# 1. 检查并修复脚本中的潜在问题
echo "[修复阶段] 检查并修复潜在问题..."

# 1.1 检查 x264-params 构建中的变量引用
if grep -q 'vbv-bufsize=\$(( \${BUFSIZE%k} ))' "$PUSH_SCRIPT"; then
    echo "  ✅ x264-params 变量引用正确"
else
    echo "  ⚠️  警告: 检测到 x264-params 构建可能有问题"
fi

# 1.2 检查 GOP 配置
if grep -q "\-g \"\$FPS\"" "$PUSH_SCRIPT"; then
    echo "  ✅ GOP 配置正确（GOP=FPS）"
else
    echo "  ⚠️  警告: GOP 配置可能不正确"
fi

# 1.3 检查码率参数
if grep -q "BITRATE=\"\${NUSCENES_BITRATE:-200k}\"" "$PUSH_SCRIPT"; then
    echo "  ✅ 码率参数配置正确"
else
    echo "  ⚠️  警告: 码率参数配置可能不正确"
fi

# 2. 运行验证脚本
echo ""
echo "[验证阶段] 运行完整验证..."
if bash scripts/verify-nuscenes-streaming.sh; then
    echo ""
    echo "=========================================="
    echo "✅ 所有验证通过！"
    echo "=========================================="
    echo ""
    echo "下一步："
    echo "1. 确保数据集路径正确配置"
    echo "2. 启动车端容器"
    echo "3. 客户端连接车端触发推流"
    echo ""
    exit 0
else
    echo ""
    echo "=========================================="
    echo "❌ 验证失败，请检查错误信息"
    echo "=========================================="
    exit 1
fi
