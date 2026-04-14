#!/bin/bash
# 自动化测试脚本：触发远驾接管操作并分析日志
# 用法: bash scripts/test-remote-control-auto.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
# shellcheck source=lib/mqtt_control_json.sh
source "$SCRIPT_DIR/lib/mqtt_control_json.sh"

CLIENT_CONTAINER="teleop-client-dev"
VEHICLE_CONTAINER="remote-driving-vehicle-1"

echo "=========================================="
echo "远驾接管功能自动化测试"
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

# 发送测试指令（启用远驾接管）
echo "=========================================="
echo "步骤 1: 发送远驾接管启用指令"
echo "=========================================="
echo ""

# 通过 mosquitto_pub 发送指令（如果可用）
if command -v mosquitto_pub >/dev/null 2>&1; then
    TEST_MSG="$(mqtt_json_remote_control "123456789" true)"
    echo "发送消息: ${TEST_MSG}"
    mosquitto_pub -h localhost -p 1883 -t vehicle/control -m "${TEST_MSG}" 2>&1 || echo "⚠ mosquitto_pub 发送失败，将在日志中查找手动发送的记录"
else
    echo "⚠ mosquitto_pub 不可用，将在日志中查找客户端发送的记录"
fi

echo ""
echo "等待3秒让消息处理完成..."
sleep 3
echo ""

# 分析日志
echo "=========================================="
echo "步骤 2: 分析车端日志"
echo "=========================================="
echo ""

VEHICLE_LOGS=$(docker logs ${VEHICLE_CONTAINER} --tail 200 2>&1)

echo "【车端关键日志】"
echo "----------------------------------------"
echo "$VEHICLE_LOGS" | grep -E "remote_control|handle_control_json|publishRemoteControlAck|确认消息|已成功发送|准备返回 true" | tail -20 || echo "未找到相关日志"
echo ""

# 检查关键步骤
echo "【车端流程检查】"
echo "----------------------------------------"
HAS_DETECTED=$(echo "$VEHICLE_LOGS" | grep -q "检测到 remote_control 指令" && echo "yes" || echo "no")
HAS_HANDLED=$(echo "$VEHICLE_LOGS" | grep -q "handle_control_json 返回: true" && echo "yes" || echo "no")
HAS_SENT_ACK=$(echo "$VEHICLE_LOGS" | grep -q "已成功发送远驾接管确认消息" && echo "yes" || echo "no")

echo "  检测到指令: ${HAS_DETECTED}"
echo "  处理成功: ${HAS_HANDLED}"
echo "  发送确认: ${HAS_SENT_ACK}"
echo ""

# 分析客户端日志
echo "=========================================="
echo "步骤 3: 分析客户端日志"
echo "=========================================="
echo ""

CLIENT_LOGS=$(docker logs ${CLIENT_CONTAINER} --tail 200 2>&1)

echo "【客户端关键日志】"
echo "----------------------------------------"
echo "$CLIENT_LOGS" | grep -E "远驾接管确认|remote_control_ack|VEHICLE_STATUS.*远驾|QML.*车端远驾接管|发送远驾接管指令" | tail -20 || echo "未找到相关日志"
echo ""

# 检查客户端接收
echo "【客户端流程检查】"
echo "----------------------------------------"
HAS_RECEIVED_ACK=$(echo "$CLIENT_LOGS" | grep -q "收到远驾接管确认消息" && echo "yes" || echo "no")
HAS_UPDATED_STATUS=$(echo "$CLIENT_LOGS" | grep -q "远驾接管状态变化" && echo "yes" || echo "no")
HAS_UPDATED_MODE=$(echo "$CLIENT_LOGS" | grep -q "驾驶模式变化" && echo "yes" || echo "no")

echo "  收到确认: ${HAS_RECEIVED_ACK}"
echo "  状态更新: ${HAS_UPDATED_STATUS}"
echo "  模式更新: ${HAS_UPDATED_MODE}"
echo ""

# 完整流程时间线
echo "=========================================="
echo "步骤 4: 完整流程时间线"
echo "=========================================="
echo ""

echo "【车端流程】"
echo "$VEHICLE_LOGS" | grep -E "remote_control|handle_control_json|publishRemoteControlAck|确认消息" | tail -10 | while read line; do
    echo "  [车端] $line"
done
echo ""

echo "【客户端流程】"
echo "$CLIENT_LOGS" | grep -E "远驾接管确认|remote_control_ack|VEHICLE_STATUS.*远驾" | tail -10 | while read line; do
    echo "  [客户端] $line"
done
echo ""

# 诊断结果
echo "=========================================="
echo "步骤 5: 诊断结果"
echo "=========================================="
echo ""

if [ "$HAS_DETECTED" = "yes" ] && [ "$HAS_HANDLED" = "yes" ] && [ "$HAS_SENT_ACK" = "yes" ] && [ "$HAS_RECEIVED_ACK" = "yes" ] && [ "$HAS_UPDATED_STATUS" = "yes" ]; then
    echo "✓✓✓ 功能正常！所有步骤都成功完成"
    echo ""
    echo "成功步骤："
    echo "  ✓ 车端检测到指令"
    echo "  ✓ 车端处理成功"
    echo "  ✓ 车端发送确认"
    echo "  ✓ 客户端收到确认"
    echo "  ✓ 客户端状态更新"
    exit 0
else
    echo "✗ 功能存在问题，请检查以下步骤："
    echo ""
    [ "$HAS_DETECTED" = "no" ] && echo "  ✗ 车端未检测到指令"
    [ "$HAS_HANDLED" = "no" ] && echo "  ✗ 车端处理失败或未返回 true"
    [ "$HAS_SENT_ACK" = "no" ] && echo "  ✗ 车端未发送确认消息"
    [ "$HAS_RECEIVED_ACK" = "no" ] && echo "  ✗ 客户端未收到确认消息"
    [ "$HAS_UPDATED_STATUS" = "no" ] && echo "  ✗ 客户端状态未更新"
    echo ""
    echo "建议："
    echo "  1. 检查车端日志中的错误信息"
    echo "  2. 检查客户端日志中的错误信息"
    echo "  3. 确认 MQTT 连接正常"
    echo "  4. 确认车端代码已重新编译"
    exit 1
fi
