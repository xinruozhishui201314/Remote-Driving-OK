#!/bin/bash
# 验证远驾接管功能

set -e

echo "=========================================="
echo "验证远驾接管功能"
echo "=========================================="

CLIENT_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'client-dev|client' | head -1)
VEHICLE_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'vehicle' | head -1)
MQTT_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'mosquitto|mqtt' | head -1)

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

VEHICLE_BINARY=$(docker exec "$VEHICLE_CONTAINER" bash -c "test -f /app/vehicle_controller && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$VEHICLE_BINARY" == "yes" ]]; then
    echo "✓ 车端已编译"
else
    echo "⚠ 车端可执行文件检查失败（可能路径不同）"
fi

# 2. 检查代码功能
echo ""
echo "2. 检查代码功能"
echo "----------------------------------------"
# 客户端：检查 requestRemoteControl 方法
CLIENT_HAS_METHOD=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q 'requestRemoteControl' /workspace/client/src/mqttcontroller.h 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$CLIENT_HAS_METHOD" == "yes" ]]; then
    echo "✓ 客户端包含 requestRemoteControl() 方法"
else
    echo "✗ 客户端缺少 requestRemoteControl() 方法"
    exit 1
fi

# 客户端：检查 QML 中的远驾接管按钮
CLIENT_HAS_BUTTON=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q '远驾接管\|remoteControlActive' /workspace/client/qml/DrivingInterface.qml 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$CLIENT_HAS_BUTTON" == "yes" ]]; then
    echo "✓ 客户端 QML 包含远驾接管按钮"
else
    echo "✗ 客户端 QML 缺少远驾接管按钮"
    exit 1
fi

# 客户端：检查按钮文本状态逻辑
CLIENT_HAS_STATE=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q 'streamStopped\|连接车辆' /workspace/client/qml/DrivingInterface.qml 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$CLIENT_HAS_STATE" == "yes" ]]; then
    echo "✓ 客户端 QML 包含按钮状态逻辑（streamStopped/连接车辆）"
else
    echo "✗ 客户端 QML 缺少按钮状态逻辑"
    exit 1
fi

# 车端：检查 remote_control 处理
VEHICLE_HAS_HANDLER=$(docker exec "$VEHICLE_CONTAINER" bash -c "grep -q 'remote_control\|setRemoteControlEnabled' /app/src/control_protocol.cpp 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$VEHICLE_HAS_HANDLER" == "yes" ]]; then
    echo "✓ 车端包含 remote_control 处理逻辑"
else
    echo "✗ 车端缺少 remote_control 处理逻辑"
    exit 1
fi

# 车端：检查 VehicleController 方法
VEHICLE_HAS_METHOD=$(docker exec "$VEHICLE_CONTAINER" bash -c "grep -q 'setRemoteControlEnabled\|isRemoteControlEnabled' /app/src/vehicle_controller.h 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$VEHICLE_HAS_METHOD" == "yes" ]]; then
    echo "✓ 车端 VehicleController 包含远驾接管方法"
else
    echo "✗ 车端 VehicleController 缺少远驾接管方法"
    exit 1
fi

# 3. 检查日志
echo ""
echo "3. 检查相关日志（最近100行）"
echo "----------------------------------------"
# 客户端日志
CLIENT_LOGS=$(docker logs "$CLIENT_CONTAINER" --tail 100 2>&1 | grep -E "远驾接管|requestRemoteControl|remote_control|streamStopped" | tail -5)
if [[ -n "$CLIENT_LOGS" ]]; then
    echo "✓ 发现客户端相关日志:"
    echo "$CLIENT_LOGS" | sed 's/^/  /'
else
    echo "⊘ 未发现客户端相关日志（可能还未测试）"
fi

# 车端日志
VEHICLE_LOGS=$(docker logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "remote_control|远驾接管|setRemoteControlEnabled" | tail -5)
if [[ -n "$VEHICLE_LOGS" ]]; then
    echo "✓ 发现车端相关日志:"
    echo "$VEHICLE_LOGS" | sed 's/^/  /'
else
    echo "⊘ 未发现车端相关日志（可能还未测试）"
fi

# 4. 检查 MQTT 消息
echo ""
echo "4. 检查 MQTT 消息（如果 MQTT 容器支持）"
echo "----------------------------------------"
if [[ -n "$MQTT_CONTAINER" ]]; then
    echo "✓ MQTT 容器: $MQTT_CONTAINER"
    echo "  提示: 可通过 mosquitto_sub 监听 vehicle/control 主题查看消息"
else
    echo "⊘ 未找到 MQTT 容器"
fi

# 总结
echo ""
echo "=========================================="
echo "验证结果"
echo "=========================================="

if [[ "$CLIENT_HAS_METHOD" == "yes" && "$CLIENT_HAS_BUTTON" == "yes" && "$CLIENT_HAS_STATE" == "yes" && "$VEHICLE_HAS_HANDLER" == "yes" && "$VEHICLE_HAS_METHOD" == "yes" ]]; then
    echo "✓✓✓ 代码功能验证通过："
    echo "  - 客户端包含 requestRemoteControl() 方法"
    echo "  - 客户端 QML 包含远驾接管按钮"
    echo "  - 客户端 QML 包含按钮状态逻辑（停止推流后显示'连接车辆'）"
    echo "  - 车端包含 remote_control 处理逻辑"
    echo "  - 车端 VehicleController 包含远驾接管方法"
    echo ""
    echo "建议进行手动测试："
    echo "  1. 在客户端UI中点击「连接车端」"
    echo "  2. 等待显示「已连接」"
    echo "  3. 点击「已连接」按钮，确认："
    echo "     - 按钮文本变为「连接车辆」"
    echo "     - 右侧出现「远驾接管」按钮"
    echo "  4. 点击「远驾接管」按钮，观察日志确认："
    echo "     - 客户端日志显示「远驾接管状态变更: 启用」"
    echo "     - 车端日志显示「收到 remote_control，远驾接管状态: 启用」"
    echo "     - 车端日志显示「远驾接管状态已设置: 启用」"
    echo "  5. 再次点击「远驾接管」按钮（应变为「取消接管」），确认："
    echo "     - 客户端日志显示「远驾接管状态变更: 禁用」"
    echo "     - 车端日志显示「收到 remote_control，远驾接管状态: 禁用」"
    exit 0
else
    echo "✗ 代码功能验证失败"
    echo "  请检查上述输出中的缺失项"
    exit 1
fi
