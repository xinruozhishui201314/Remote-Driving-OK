#!/bin/bash
# 完整验证脚本：验证远驾接管功能是否正常工作
# 用法: bash scripts/verify-remote-control-complete.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

CLIENT_CONTAINER="teleop-client-dev"
VEHICLE_CONTAINER="remote-driving-vehicle-1"

echo "=========================================="
echo "远驾接管功能完整验证"
echo "=========================================="
echo ""

# 检查容器是否存在
if ! docker ps --format '{{.Names}}' | grep -q "^${CLIENT_CONTAINER}$"; then
    echo "✗ 错误: 客户端容器 ${CLIENT_CONTAINER} 不存在"
    exit 1
fi

if ! docker ps --format '{{.Names}}' | grep -q "^${VEHICLE_CONTAINER}$"; then
    echo "✗ 错误: 车端容器 ${VEHICLE_CONTAINER} 不存在"
    exit 1
fi

echo "✓ 容器检查通过"
echo ""

# 清空日志缓冲区
echo "清空日志缓冲区..."
docker logs ${CLIENT_CONTAINER} --tail 10 > /dev/null 2>&1
docker logs ${VEHICLE_CONTAINER} --tail 10 > /dev/null 2>&1
echo "✓ 日志缓冲区已清空"
echo ""

# 等待服务稳定
echo "等待服务稳定（5秒）..."
sleep 5
echo ""

# 分析最近的车端日志（查找 remote_control 相关）
echo "=========================================="
echo "步骤 1: 分析车端日志（最近500行）"
echo "=========================================="
echo ""

VEHICLE_LOGS=$(docker logs ${VEHICLE_CONTAINER} --tail 500 2>&1)

echo "【车端关键日志（REMOTE_CONTROL 标记）】"
echo "----------------------------------------"
echo "$VEHICLE_LOGS" | grep -E "REMOTE_CONTROL|remote_control" | tail -30 || echo "未找到相关日志"
echo ""

# 检查关键步骤
echo "【车端流程检查】"
echo "----------------------------------------"
HAS_DETECTED=$(echo "$VEHICLE_LOGS" | grep -q "确认是 remote_control 指令" && echo "yes" || echo "no")
HAS_HANDLED=$(echo "$VEHICLE_LOGS" | grep -q "handle_control_json 返回: true" && echo "yes" || echo "no")
HAS_SENT_ACK=$(echo "$VEHICLE_LOGS" | grep -q "已成功发送远驾接管确认消息" && echo "yes" || echo "no")
HAS_ACK_CONTENT=$(echo "$VEHICLE_LOGS" | grep -q "remote_control_enabled.*driving_mode" && echo "yes" || echo "no")

echo "  检测到指令: ${HAS_DETECTED}"
echo "  处理成功: ${HAS_HANDLED}"
echo "  发送确认: ${HAS_SENT_ACK}"
echo "  确认内容: ${HAS_ACK_CONTENT}"
echo ""

# 分析客户端日志
echo "=========================================="
echo "步骤 2: 分析客户端日志（最近500行）"
echo "=========================================="
echo ""

CLIENT_LOGS=$(docker logs ${CLIENT_CONTAINER} --tail 500 2>&1)

echo "【客户端关键日志（REMOTE_CONTROL 标记）】"
echo "----------------------------------------"
echo "$CLIENT_LOGS" | grep -E "REMOTE_CONTROL|remote_control" | tail -30 || echo "未找到相关日志"
echo ""

# 检查客户端接收
echo "【客户端流程检查】"
echo "----------------------------------------"
HAS_SENT_CMD=$(echo "$CLIENT_LOGS" | grep -q "请求远驾接管" && echo "yes" || echo "no")
HAS_RECEIVED_ACK=$(echo "$CLIENT_LOGS" | grep -q "收到远驾接管确认消息" && echo "yes" || echo "no")
HAS_UPDATED_STATUS=$(echo "$CLIENT_LOGS" | grep -q "远驾接管状态变化" && echo "yes" || echo "no")
HAS_UPDATED_MODE=$(echo "$CLIENT_LOGS" | grep -q "驾驶模式变化" && echo "yes" || echo "no")
HAS_QML_UPDATED=$(echo "$CLIENT_LOGS" | grep -q "车端远驾接管状态变化" && echo "yes" || echo "no")

echo "  发送指令: ${HAS_SENT_CMD}"
echo "  收到确认: ${HAS_RECEIVED_ACK}"
echo "  状态更新: ${HAS_UPDATED_STATUS}"
echo "  模式更新: ${HAS_UPDATED_MODE}"
echo "  QML更新: ${HAS_QML_UPDATED}"
echo ""

# 完整流程时间线
echo "=========================================="
echo "步骤 3: 完整流程时间线"
echo "=========================================="
echo ""

echo "【车端流程】"
echo "$VEHICLE_LOGS" | grep -E "REMOTE_CONTROL" | tail -20 | while read line; do
    echo "  [车端] $line"
done
echo ""

echo "【客户端流程】"
echo "$CLIENT_LOGS" | grep -E "REMOTE_CONTROL" | tail -20 | while read line; do
    echo "  [客户端] $line"
done
echo ""

# 诊断结果
echo "=========================================="
echo "步骤 4: 诊断结果"
echo "=========================================="
echo ""

ALL_STEPS_OK=true

if [ "$HAS_DETECTED" != "yes" ]; then
    echo "✗ 车端未检测到 remote_control 指令"
    ALL_STEPS_OK=false
fi

if [ "$HAS_HANDLED" != "yes" ]; then
    echo "✗ 车端处理失败或未返回 true"
    ALL_STEPS_OK=false
fi

if [ "$HAS_SENT_ACK" != "yes" ]; then
    echo "✗ 车端未发送确认消息"
    ALL_STEPS_OK=false
fi

if [ "$HAS_RECEIVED_ACK" != "yes" ]; then
    echo "✗ 客户端未收到确认消息"
    ALL_STEPS_OK=false
fi

if [ "$HAS_UPDATED_STATUS" != "yes" ]; then
    echo "✗ 客户端状态未更新"
    ALL_STEPS_OK=false
fi

if [ "$HAS_UPDATED_MODE" != "yes" ]; then
    echo "✗ 客户端驾驶模式未更新"
    ALL_STEPS_OK=false
fi

if [ "$HAS_QML_UPDATED" != "yes" ]; then
    echo "✗ QML 界面未更新"
    ALL_STEPS_OK=false
fi

echo ""

if [ "$ALL_STEPS_OK" = "true" ]; then
    echo "✓✓✓ 功能正常！所有步骤都成功完成"
    echo ""
    echo "成功步骤："
    echo "  ✓ 车端检测到指令"
    echo "  ✓ 车端处理成功"
    echo "  ✓ 车端发送确认"
    echo "  ✓ 客户端收到确认"
    echo "  ✓ 客户端状态更新"
    echo "  ✓ 客户端模式更新"
    echo "  ✓ QML 界面更新"
    exit 0
else
    echo "✗ 功能存在问题，请检查上述步骤"
    echo ""
    echo "建议："
    echo "  1. 检查车端日志中的错误信息（查找 REMOTE_CONTROL 标记）"
    echo "  2. 检查客户端日志中的错误信息（查找 REMOTE_CONTROL 标记）"
    echo "  3. 确认 MQTT 连接正常"
    echo "  4. 确认车端代码已重新编译"
    echo ""
    echo "查看实时日志："
    echo "  docker logs ${VEHICLE_CONTAINER} -f | grep REMOTE_CONTROL"
    echo "  docker logs ${CLIENT_CONTAINER} -f | grep REMOTE_CONTROL"
    exit 1
fi
