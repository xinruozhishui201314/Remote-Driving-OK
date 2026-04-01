#!/bin/bash
# 快速验证 NuScenes 推流脚本（最小检查）
# 用于快速检查关键配置项

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

PUSH_SCRIPT="scripts/push-nuscenes-cameras-to-zlm.sh"

echo "快速验证 NuScenes 推流脚本..."
echo ""

# 1. 语法检查
if bash -n "$PUSH_SCRIPT" 2>/dev/null; then
    echo "✅ 语法检查通过"
else
    echo "❌ 语法错误"
    exit 1
fi

# 2. 检查关键配置
if grep -q 'BITRATE="\${NUSCENES_BITRATE:-200k}"' "$PUSH_SCRIPT"; then
    echo "✅ 码率配置正确"
else
    echo "❌ 码率配置错误"
    exit 1
fi

if grep -q '\-g "\$FPS"' "$PUSH_SCRIPT"; then
    echo "✅ GOP 配置正确"
else
    echo "❌ GOP 配置错误"
    exit 1
fi

# 3. 检查 FFmpeg
if command -v ffmpeg &>/dev/null; then
    echo "✅ FFmpeg 已安装"
else
    echo "❌ FFmpeg 未安装"
    exit 1
fi

echo ""
echo "✅ 快速验证通过！"
