#!/bin/bash
# 分析远驾接管确认功能的日志，验证功能是否正常

set -e

echo "=========================================="
echo "远驾接管确认功能日志分析"
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

echo ""
echo "1. 分析车端日志（最近500行）"
echo "----------------------------------------"
VEHICLE_LOGS=$(docker logs "$VEHICLE_CONTAINER" --tail 500 2>&1)

# 检查关键步骤
HAS_DETECTED=$(echo "$VEHICLE_LOGS" | grep -q "检测到 remote_control 指令" && echo "yes" || echo "no")
HAS_PROCESSED=$(echo "$VEHICLE_LOGS" | grep -q "remote_control 指令处理成功" && echo "yes" || echo "no")
HAS_ACK_START=$(echo "$VEHICLE_LOGS" | grep -q "publishRemoteControlAck.*开始发送" && echo "yes" || echo "no")
HAS_ACK_SENT=$(echo "$VEHICLE_LOGS" | grep -q "已成功发送远驾接管确认消息" && echo "yes" || echo "no")
HAS_ACK_FAILED=$(echo "$VEHICLE_LOGS" | grep -q "无法发送远驾接管确认\|发送远驾接管确认失败" && echo "yes" || echo "no")

echo "  关键步骤检查:"
echo "    - 检测到指令: $HAS_DETECTED"
echo "    - 处理成功: $HAS_PROCESSED"
echo "    - 开始发送确认: $HAS_ACK_START"
echo "    - 确认发送成功: $HAS_ACK_SENT"
echo "    - 确认发送失败: $HAS_ACK_FAILED"

if [[ "$HAS_DETECTED" == "yes" ]]; then
    echo ""
    echo "  检测到指令的日志:"
    echo "$VEHICLE_LOGS" | grep "检测到 remote_control 指令" | tail -5 | sed 's/^/    /'
fi

if [[ "$HAS_PROCESSED" == "yes" ]]; then
    echo ""
    echo "  处理成功的日志:"
    echo "$VEHICLE_LOGS" | grep "remote_control 指令处理成功" | tail -3 | sed 's/^/    /'
elif [[ "$HAS_DETECTED" == "yes" ]]; then
    echo ""
    echo "  ⚠ 检测到指令但未看到处理成功日志"
    echo "  检查 handle_control_json 返回值:"
    echo "$VEHICLE_LOGS" | grep "handle_control_json 返回" | tail -3 | sed 's/^/    /' || echo "    未找到返回值日志"
fi

if [[ "$HAS_ACK_START" == "yes" ]]; then
    echo ""
    echo "  开始发送确认的日志:"
    echo "$VEHICLE_LOGS" | grep "publishRemoteControlAck.*开始发送" | tail -3 | sed 's/^/    /'
fi

if [[ "$HAS_ACK_SENT" == "yes" ]]; then
    echo ""
    echo "  确认发送成功的日志:"
    echo "$VEHICLE_LOGS" | grep "已成功发送远驾接管确认消息" | tail -3 | sed 's/^/    /'
    echo "$VEHICLE_LOGS" | grep -A 1 "已成功发送远驾接管确认消息" | grep "内容:" | tail -3 | sed 's/^/    /'
fi

if [[ "$HAS_ACK_FAILED" == "yes" ]]; then
    echo ""
    echo "  ✗ 确认发送失败的日志:"
    echo "$VEHICLE_LOGS" | grep -E "无法发送远驾接管确认|发送远驾接管确认失败" | tail -5 | sed 's/^/    /'
fi

echo ""
echo "2. 分析客户端日志（最近500行）"
echo "----------------------------------------"
CLIENT_LOGS=$(docker logs "$CLIENT_CONTAINER" --tail 500 2>&1)

# 检查关键步骤
HAS_RECEIVED_ACK_MQTT=$(echo "$CLIENT_LOGS" | grep -q "收到远驾接管确认消息.*remote_control_ack" && echo "yes" || echo "no")
HAS_RECEIVED_ACK_STATUS=$(echo "$CLIENT_LOGS" | grep -q "VEHICLE_STATUS.*收到远驾接管确认消息" && echo "yes" || echo "no")
HAS_STATUS_UPDATED=$(echo "$CLIENT_LOGS" | grep -q "远驾接管状态变化\|远驾接管状态更新" && echo "yes" || echo "no")
HAS_MODE_UPDATED=$(echo "$CLIENT_LOGS" | grep -q "驾驶模式变化\|驾驶模式更新\|驾驶模式已更新" && echo "yes" || echo "no")
HAS_QML_CONFIRMED=$(echo "$CLIENT_LOGS" | grep -q "QML.*车端远驾接管状态确认" && echo "yes" || echo "no")
HAS_QML_MODE=$(echo "$CLIENT_LOGS" | grep -q "QML.*驾驶模式已更新" && echo "yes" || echo "no")

echo "  关键步骤检查:"
echo "    - MQTT收到确认: $HAS_RECEIVED_ACK_MQTT"
echo "    - Status收到确认: $HAS_RECEIVED_ACK_STATUS"
echo "    - 状态已更新: $HAS_STATUS_UPDATED"
echo "    - 模式已更新: $HAS_MODE_UPDATED"
echo "    - QML状态确认: $HAS_QML_CONFIRMED"
echo "    - QML模式更新: $HAS_QML_MODE"

if [[ "$HAS_RECEIVED_ACK_MQTT" == "yes" ]]; then
    echo ""
    echo "  MQTT收到确认的日志:"
    echo "$CLIENT_LOGS" | grep "收到远驾接管确认消息.*remote_control_ack" | tail -3 | sed 's/^/    /'
    echo "$CLIENT_LOGS" | grep -A 1 "收到远驾接管确认消息.*remote_control_ack" | grep "确认消息内容:" | tail -3 | sed 's/^/    /'
fi

if [[ "$HAS_RECEIVED_ACK_STATUS" == "yes" ]]; then
    echo ""
    echo "  Status收到确认的日志:"
    echo "$CLIENT_LOGS" | grep "VEHICLE_STATUS.*收到远驾接管确认消息" | tail -3 | sed 's/^/    /'
fi

if [[ "$HAS_STATUS_UPDATED" == "yes" ]]; then
    echo ""
    echo "  状态更新的日志:"
    echo "$CLIENT_LOGS" | grep -E "远驾接管状态变化|远驾接管状态更新" | tail -5 | sed 's/^/    /'
fi

if [[ "$HAS_MODE_UPDATED" == "yes" ]]; then
    echo ""
    echo "  模式更新的日志:"
    echo "$CLIENT_LOGS" | grep -E "驾驶模式变化|驾驶模式更新|驾驶模式已更新" | tail -5 | sed 's/^/    /'
fi

if [[ "$HAS_QML_CONFIRMED" == "yes" ]]; then
    echo ""
    echo "  QML状态确认的日志:"
    echo "$CLIENT_LOGS" | grep "QML.*车端远驾接管状态确认" | tail -5 | sed 's/^/    /'
fi

if [[ "$HAS_QML_MODE" == "yes" ]]; then
    echo ""
    echo "  QML模式更新的日志:"
    echo "$CLIENT_LOGS" | grep "QML.*驾驶模式已更新" | tail -3 | sed 's/^/    /'
fi

echo ""
echo "3. 完整流程时间线"
echo "----------------------------------------"
echo "车端流程:"
VEHICLE_TIMELINE=$(docker logs "$VEHICLE_CONTAINER" --tail 200 2>&1 | grep -E "remote_control|远驾接管|publishRemoteControlAck|确认消息|handle_control_json" | tail -15)
if [[ -n "$VEHICLE_TIMELINE" ]]; then
    echo "$VEHICLE_TIMELINE" | sed 's/^/  [车端] /'
else
    echo "  无相关日志"
fi

echo ""
echo "客户端流程:"
CLIENT_TIMELINE=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -E "远驾接管确认|remote_control_ack|VEHICLE_STATUS|QML.*车端远驾接管|QML.*驾驶模式|发送远驾接管指令" | tail -15)
if [[ -n "$CLIENT_TIMELINE" ]]; then
    echo "$CLIENT_TIMELINE" | sed 's/^/  [客户端] /'
else
    echo "  无相关日志"
fi

echo ""
echo "4. 问题诊断"
echo "----------------------------------------"
ALL_STEPS_OK=true

if [[ "$HAS_DETECTED" != "yes" ]]; then
    echo "✗ 车端未检测到 remote_control 指令"
    echo "  可能原因：MQTT消息格式不匹配或车端未运行"
    ALL_STEPS_OK=false
fi

if [[ "$HAS_DETECTED" == "yes" && "$HAS_PROCESSED" != "yes" ]]; then
    echo "✗ 车端检测到指令但处理失败"
    echo "  可能原因：handle_control_json 返回 false 或抛出异常"
    echo "  检查：查看车端日志中的错误信息"
    ALL_STEPS_OK=false
fi

if [[ "$HAS_PROCESSED" == "yes" && "$HAS_ACK_START" != "yes" ]]; then
    echo "✗ 车端处理成功但未开始发送确认"
    echo "  可能原因：publishRemoteControlAck 未被调用"
    ALL_STEPS_OK=false
fi

if [[ "$HAS_ACK_START" == "yes" && "$HAS_ACK_SENT" != "yes" ]]; then
    echo "✗ 车端开始发送确认但未成功"
    echo "  可能原因：MQTT连接问题或发布失败"
    if [[ "$HAS_ACK_FAILED" == "yes" ]]; then
        echo "  错误信息："
        echo "$VEHICLE_LOGS" | grep -E "无法发送远驾接管确认|发送远驾接管确认失败" | tail -3 | sed 's/^/    /'
    fi
    ALL_STEPS_OK=false
fi

if [[ "$HAS_ACK_SENT" == "yes" && "$HAS_RECEIVED_ACK_MQTT" != "yes" ]]; then
    echo "✗ 车端发送确认成功但客户端未收到"
    echo "  可能原因：MQTT主题订阅问题或消息丢失"
    ALL_STEPS_OK=false
fi

if [[ "$HAS_RECEIVED_ACK_MQTT" == "yes" && "$HAS_RECEIVED_ACK_STATUS" != "yes" ]]; then
    echo "✗ 客户端MQTT收到确认但Status未收到"
    echo "  可能原因：statusReceived 信号未连接或 updateStatus 未调用"
    ALL_STEPS_OK=false
fi

if [[ "$HAS_RECEIVED_ACK_STATUS" == "yes" && "$HAS_STATUS_UPDATED" != "yes" ]]; then
    echo "✗ Status收到确认但状态未更新"
    echo "  可能原因：状态值相同或 setRemoteControlEnabled 未触发"
    ALL_STEPS_OK=false
fi

if [[ "$HAS_STATUS_UPDATED" == "yes" && "$HAS_QML_CONFIRMED" != "yes" ]]; then
    echo "✗ 状态已更新但QML未响应"
    echo "  可能原因：Connections 未连接或信号未触发"
    ALL_STEPS_OK=false
fi

echo ""
echo "5. 测试结论"
echo "----------------------------------------"
if [[ "$ALL_STEPS_OK" == "true" && \
      "$HAS_DETECTED" == "yes" && "$HAS_PROCESSED" == "yes" && \
      "$HAS_ACK_SENT" == "yes" && "$HAS_RECEIVED_ACK_MQTT" == "yes" && \
      "$HAS_RECEIVED_ACK_STATUS" == "yes" && "$HAS_STATUS_UPDATED" == "yes" && \
      "$HAS_MODE_UPDATED" == "yes" && "$HAS_QML_CONFIRMED" == "yes" && "$HAS_QML_MODE" == "yes" ]]; then
    echo "✓✓✓ 功能完全正常："
    echo "  - 车端收到指令并处理"
    echo "  - 车端发送确认消息"
    echo "  - 客户端收到确认消息"
    echo "  - 客户端更新状态和模式"
    echo "  - QML响应状态变化"
    echo ""
    echo "建议检查UI："
    echo "  - 按钮文本应显示「远驾已接管」"
    echo "  - 驾驶模式应显示「远驾」"
    exit 0
else
    echo "✗ 功能存在问题，请根据上述诊断信息修复"
    exit 1
fi
