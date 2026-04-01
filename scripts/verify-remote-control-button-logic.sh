#!/bin/bash
# 验证远驾接管按钮逻辑：只有视频流连接时才能点击

set -e

echo "=========================================="
echo "验证远驾接管按钮逻辑"
echo "=========================================="

CLIENT_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'client-dev|client' | head -1)

if [[ -z "$CLIENT_CONTAINER" ]]; then
    echo "✗ 未找到客户端容器"
    exit 1
fi
echo "✓ 找到客户端容器: $CLIENT_CONTAINER"

# 1. 检查编译状态
echo ""
echo "1. 检查编译状态"
echo "----------------------------------------"
CLIENT_BINARY=$(docker exec "$CLIENT_CONTAINER" bash -c "test -f /workspace/client/build/RemoteDrivingClient && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$CLIENT_BINARY" == "yes" ]]; then
    CLIENT_TIME=$(docker exec "$CLIENT_CONTAINER" bash -c "stat -c '%y' /workspace/client/build/RemoteDrivingClient 2>/dev/null | cut -d'.' -f1" 2>/dev/null || echo "unknown")
    echo "✓ 客户端已编译（时间戳: $CLIENT_TIME）"
else
    echo "✗ 客户端未编译"
    exit 1
fi

# 2. 检查代码逻辑
echo ""
echo "2. 检查代码逻辑"
echo "----------------------------------------"

# 检查按钮启用逻辑
HAS_ENABLED_LOGIC=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q 'buttonEnabled.*isVideoConnected\|enabled.*buttonEnabled' /workspace/client/qml/DrivingInterface.qml 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_ENABLED_LOGIC" == "yes" ]]; then
    echo "✓ 包含按钮启用逻辑（buttonEnabled/isVideoConnected）"
else
    echo "✗ 缺少按钮启用逻辑"
    exit 1
fi

# 检查视频流状态检查
HAS_VIDEO_CHECK=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q 'isVideoConnected.*webrtcStreamManager.*anyConnected' /workspace/client/qml/DrivingInterface.qml 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_VIDEO_CHECK" == "yes" ]]; then
    echo "✓ 包含视频流状态检查（isVideoConnected）"
else
    echo "✗ 缺少视频流状态检查"
    exit 1
fi

# 检查禁用时的样式
HAS_DISABLED_STYLE=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q 'buttonEnabled.*0\\.5\|opacity.*buttonEnabled' /workspace/client/qml/DrivingInterface.qml 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_DISABLED_STYLE" == "yes" ]]; then
    echo "✓ 包含禁用时的样式（opacity/颜色）"
else
    echo "✗ 缺少禁用时的样式"
    exit 1
fi

# 检查自动禁用逻辑（视频流断开时）
HAS_AUTO_DISABLE=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -A 5 'onAnyConnectedChanged' /workspace/client/qml/DrivingInterface.qml 2>/dev/null | grep -q '视频流已断开\|自动禁用' && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_AUTO_DISABLE" == "yes" ]]; then
    echo "✓ 包含自动禁用逻辑（视频流断开时）"
else
    echo "✗ 缺少自动禁用逻辑"
    exit 1
fi

# 检查日志记录
HAS_LOG=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q '视频流未连接.*无法启用\|视频流状态变化\|远驾接管按钮被禁用' /workspace/client/qml/DrivingInterface.qml 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_LOG" == "yes" ]]; then
    echo "✓ 包含详细日志记录"
else
    echo "✗ 缺少详细日志记录"
    exit 1
fi

# 检查 ToolTip 更新
HAS_TOOLTIP=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -A 3 'ToolTip.text' /workspace/client/qml/DrivingInterface.qml 2>/dev/null | grep -q '视频流未连接\|请先连接车辆' && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_TOOLTIP" == "yes" ]]; then
    echo "✓ 包含 ToolTip 提示（禁用时说明原因）"
else
    echo "✗ 缺少 ToolTip 提示"
    exit 1
fi

# 3. 检查日志（如果程序正在运行）
echo ""
echo "3. 检查相关日志（最近100行）"
echo "----------------------------------------"
CLIENT_LOGS=$(docker logs "$CLIENT_CONTAINER" --tail 100 2>&1 | grep -E "远驾接管|视频流|buttonEnabled|isVideoConnected" | tail -10)
if [[ -n "$CLIENT_LOGS" ]]; then
    echo "✓ 发现相关日志:"
    echo "$CLIENT_LOGS" | sed 's/^/  /'
else
    echo "⊘ 未发现相关日志（可能还未测试）"
fi

# 4. 代码片段验证
echo ""
echo "4. 关键代码片段验证"
echo "----------------------------------------"
echo "检查按钮启用条件："
docker exec "$CLIENT_CONTAINER" bash -c "grep -A 2 'property bool buttonEnabled' /workspace/client/qml/DrivingInterface.qml 2>/dev/null | head -3" | sed 's/^/  /' || echo "  ⚠ 未找到"
echo ""
echo "检查禁用时的点击处理："
docker exec "$CLIENT_CONTAINER" bash -c "grep -A 3 'if (!parent.buttonEnabled)' /workspace/client/qml/DrivingInterface.qml 2>/dev/null | head -4" | sed 's/^/  /' || echo "  ⚠ 未找到"
echo ""
echo "检查自动禁用逻辑："
docker exec "$CLIENT_CONTAINER" bash -c "grep -A 5 '视频流已断开.*自动禁用' /workspace/client/qml/DrivingInterface.qml 2>/dev/null | head -6" | sed 's/^/  /' || echo "  ⚠ 未找到"

# 总结
echo ""
echo "=========================================="
echo "验证结果"
echo "=========================================="

if [[ "$HAS_ENABLED_LOGIC" == "yes" && "$HAS_VIDEO_CHECK" == "yes" && "$HAS_DISABLED_STYLE" == "yes" && "$HAS_AUTO_DISABLE" == "yes" && "$HAS_LOG" == "yes" && "$HAS_TOOLTIP" == "yes" ]]; then
    echo "✓✓✓ 按钮逻辑验证通过："
    echo "  - 包含按钮启用逻辑（只有视频流连接时才能点击）"
    echo "  - 包含视频流状态检查"
    echo "  - 包含禁用时的样式（半透明、灰色）"
    echo "  - 包含自动禁用逻辑（视频流断开时）"
    echo "  - 包含详细日志记录"
    echo "  - 包含 ToolTip 提示"
    echo ""
    echo "建议进行手动测试："
    echo "  1. 启动客户端，不连接视频流"
    echo "     - 验证：「远驾接管」按钮应为禁用状态（半透明、灰色）"
    echo "     - 验证：鼠标悬停显示「视频流未连接，请先连接车辆后再启用远驾接管」"
    echo "     - 验证：点击按钮无效果，日志显示「远驾接管按钮被禁用：视频流未连接」"
    echo ""
    echo "  2. 连接视频流（点击「连接车端」）"
    echo "     - 验证：视频流连接后，「远驾接管」按钮变为可用状态"
    echo "     - 验证：日志显示「视频流状态变化: 已连接，远驾接管按钮已启用」"
    echo ""
    echo "  3. 点击「远驾接管」按钮"
    echo "     - 验证：按钮文本变为「取消接管」"
    echo "     - 验证：日志显示「远驾接管状态变更: 禁用 -> 启用（视频流已连接）」"
    echo ""
    echo "  4. 断开视频流（点击「已连接」）"
    echo "     - 验证：如果远驾接管是激活状态，自动禁用"
    echo "     - 验证：日志显示「视频流已断开，自动禁用远驾接管状态」"
    echo "     - 验证：日志显示「已发送远驾接管禁用指令到车端（视频流断开）」"
    echo "     - 验证：日志显示「视频流状态变化: 已断开，远驾接管按钮已禁用」"
    exit 0
else
    echo "✗ 按钮逻辑验证失败"
    echo "  请检查上述输出中的缺失项"
    exit 1
fi
