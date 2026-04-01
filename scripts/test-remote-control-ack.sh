#!/bin/bash
# 自动化测试远驾接管确认功能并分析日志

set -e

echo "=========================================="
echo "远驾接管确认功能自动化测试"
echo "=========================================="

CLIENT_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'client-dev|client' | head -1)
VEHICLE_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'vehicle|vehicle-side' | head -1)

if [[ -z "$CLIENT_CONTAINER" ]]; then
    echo "✗ 未找到客户端容器"
    exit 1
fi
echo "✓ 找到客户端容器: $CLIENT_CONTAINER"

if [[ -z "$VEHICLE_CONTAINER" ]]; then
    echo "✗ 未找到车端容器"
    exit 1
fi
echo "✓ 找到车端容器: $VEHICLE_CONTAINER"

# 清空之前的日志（保留最后100行）
echo ""
echo "1. 准备测试环境"
echo "----------------------------------------"
echo "  清空日志缓冲区（保留最后100行）..."
docker logs "$CLIENT_CONTAINER" --tail 100 > /tmp/client_logs_before.txt 2>&1 || true
docker logs "$VEHICLE_CONTAINER" --tail 100 > /tmp/vehicle_logs_before.txt 2>&1 || true

echo ""
echo "2. 等待用户操作"
echo "----------------------------------------"
echo "  请在客户端界面："
echo "  1. 确保视频流已连接（显示「已连接」）"
echo "  2. 点击「远驾接管」按钮"
echo "  3. 等待3秒后按 Enter 继续..."
read -t 300 || echo "  超时，继续分析日志..."

echo ""
echo "3. 分析车端日志"
echo "----------------------------------------"
VEHICLE_LOGS=$(docker logs "$VEHICLE_CONTAINER" --tail 200 2>&1 | grep -E "remote_control|远驾接管|publishRemoteControlAck|remote_control_ack" | tail -30)
if [[ -n "$VEHICLE_LOGS" ]]; then
    echo "✓ 发现车端相关日志:"
    echo "$VEHICLE_LOGS" | sed 's/^/  /'
    
    # 检查关键日志
    HAS_DETECTED=$(echo "$VEHICLE_LOGS" | grep -q "检测到 remote_control 指令" && echo "yes" || echo "no")
    HAS_PROCESSED=$(echo "$VEHICLE_LOGS" | grep -q "remote_control 指令处理成功" && echo "yes" || echo "no")
    HAS_SENT_ACK=$(echo "$VEHICLE_LOGS" | grep -q "已成功发送远驾接管确认消息" && echo "yes" || echo "no")
    
    echo ""
    echo "  关键步骤检查:"
    echo "    - 检测到指令: $HAS_DETECTED"
    echo "    - 处理成功: $HAS_PROCESSED"
    echo "    - 发送确认: $HAS_SENT_ACK"
    
    if [[ "$HAS_DETECTED" == "yes" && "$HAS_PROCESSED" == "yes" && "$HAS_SENT_ACK" == "yes" ]]; then
        echo "  ✓✓✓ 车端流程完整"
    else
        echo "  ✗ 车端流程不完整，请检查上述日志"
    fi
else
    echo "✗ 未发现车端相关日志"
fi

echo ""
echo "4. 分析客户端日志"
echo "----------------------------------------"
CLIENT_LOGS=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -E "远驾接管确认|remote_control_ack|VEHICLE_STATUS.*远驾|QML.*车端远驾接管状态确认|QML.*驾驶模式已更新" | tail -30)
if [[ -n "$CLIENT_LOGS" ]]; then
    echo "✓ 发现客户端相关日志:"
    echo "$CLIENT_LOGS" | sed 's/^/  /'
    
    # 检查关键日志
    HAS_RECEIVED_ACK=$(echo "$CLIENT_LOGS" | grep -q "收到远驾接管确认消息" && echo "yes" || echo "no")
    HAS_STATUS_UPDATED=$(echo "$CLIENT_LOGS" | grep -q "远驾接管状态更新\|车端远驾接管状态确认" && echo "yes" || echo "no")
    HAS_MODE_UPDATED=$(echo "$CLIENT_LOGS" | grep -q "驾驶模式已更新\|驾驶模式更新" && echo "yes" || echo "no")
    
    echo ""
    echo "  关键步骤检查:"
    echo "    - 收到确认消息: $HAS_RECEIVED_ACK"
    echo "    - 状态已更新: $HAS_STATUS_UPDATED"
    echo "    - 模式已更新: $HAS_MODE_UPDATED"
    
    if [[ "$HAS_RECEIVED_ACK" == "yes" && "$HAS_STATUS_UPDATED" == "yes" && "$HAS_MODE_UPDATED" == "yes" ]]; then
        echo "  ✓✓✓ 客户端流程完整"
    else
        echo "  ✗ 客户端流程不完整，请检查上述日志"
    fi
else
    echo "✗ 未发现客户端相关日志"
fi

echo ""
echo "5. 完整日志时间线"
echo "----------------------------------------"
echo "车端日志（最近50行，包含 remote_control）:"
docker logs "$VEHICLE_CONTAINER" --tail 50 2>&1 | grep -E "remote_control|远驾接管|publishRemoteControlAck|remote_control_ack|MQTT.*确认" | tail -20 | sed 's/^/  [车端] /' || echo "  无相关日志"

echo ""
echo "客户端日志（最近50行，包含确认）:"
docker logs "$CLIENT_CONTAINER" --tail 50 2>&1 | grep -E "远驾接管确认|remote_control_ack|VEHICLE_STATUS|QML.*车端远驾接管|QML.*驾驶模式" | tail -20 | sed 's/^/  [客户端] /' || echo "  无相关日志"

echo ""
echo "6. 测试结论"
echo "----------------------------------------"
if [[ "$HAS_DETECTED" == "yes" && "$HAS_PROCESSED" == "yes" && "$HAS_SENT_ACK" == "yes" && \
      "$HAS_RECEIVED_ACK" == "yes" && "$HAS_STATUS_UPDATED" == "yes" && "$HAS_MODE_UPDATED" == "yes" ]]; then
    echo "✓✓✓ 功能测试通过："
    echo "  - 车端收到指令并处理"
    echo "  - 车端发送确认消息"
    echo "  - 客户端收到确认消息"
    echo "  - 客户端更新状态和模式"
    echo ""
    echo "建议检查UI："
    echo "  - 按钮文本应变为「远驾已接管」"
    echo "  - 驾驶模式应显示为「远驾」"
    exit 0
else
    echo "✗ 功能测试未完全通过"
    echo ""
    echo "问题诊断："
    if [[ "$HAS_DETECTED" != "yes" ]]; then
        echo "  - 车端未检测到 remote_control 指令（检查MQTT连接）"
    fi
    if [[ "$HAS_PROCESSED" != "yes" ]]; then
        echo "  - 车端处理指令失败（检查 control_protocol.cpp）"
    fi
    if [[ "$HAS_SENT_ACK" != "yes" ]]; then
        echo "  - 车端未发送确认消息（检查 publishRemoteControlAck 方法）"
    fi
    if [[ "$HAS_RECEIVED_ACK" != "yes" ]]; then
        echo "  - 客户端未收到确认消息（检查MQTT订阅和消息解析）"
    fi
    if [[ "$HAS_STATUS_UPDATED" != "yes" ]]; then
        echo "  - 客户端状态未更新（检查 VehicleStatus.updateStatus）"
    fi
    if [[ "$HAS_MODE_UPDATED" != "yes" ]]; then
        echo "  - 驾驶模式未更新（检查 driving_mode 字段解析）"
    fi
    exit 1
fi
