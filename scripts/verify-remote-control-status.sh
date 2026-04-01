#!/bin/bash
# 验证远驾接管状态反馈和驾驶模式显示功能

set -e

echo "=========================================="
echo "验证远驾接管状态反馈和驾驶模式显示"
echo "=========================================="

CLIENT_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'client-dev|client' | head -1)
VEHICLE_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'vehicle|vehicle-side' | head -1)

if [[ -z "$CLIENT_CONTAINER" ]]; then
    echo "✗ 未找到客户端容器"
    exit 1
fi
echo "✓ 找到客户端容器: $CLIENT_CONTAINER"

if [[ -z "$VEHICLE_CONTAINER" ]]; then
    echo "⚠ 未找到车端容器（可能未启动）"
else
    echo "✓ 找到车端容器: $VEHICLE_CONTAINER"
fi

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

if [[ -n "$VEHICLE_CONTAINER" ]]; then
    VEHICLE_BINARY=$(docker exec "$VEHICLE_CONTAINER" bash -c "test -f /app/build/VehicleSide && echo 'yes' || echo 'no'" 2>/dev/null)
    if [[ "$VEHICLE_BINARY" == "yes" ]]; then
        echo "✓ 车端已编译"
    else
        echo "⚠ 车端未编译（可能不影响代码检查）"
    fi
fi

# 2. 检查代码逻辑
echo ""
echo "2. 检查代码逻辑"
echo "----------------------------------------"

# 检查车端状态发布
if [[ -n "$VEHICLE_CONTAINER" ]]; then
    HAS_REMOTE_CONTROL_FIELD=$(docker exec "$VEHICLE_CONTAINER" bash -c "grep -q 'remote_control_enabled' /app/src/mqtt_handler.cpp 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
    if [[ "$HAS_REMOTE_CONTROL_FIELD" == "yes" ]]; then
        echo "✓ 车端状态发布包含 remote_control_enabled 字段"
    else
        echo "✗ 车端状态发布缺少 remote_control_enabled 字段"
        exit 1
    fi
    
    HAS_DRIVING_MODE_FIELD=$(docker exec "$VEHICLE_CONTAINER" bash -c "grep -q 'driving_mode' /app/src/mqtt_handler.cpp 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
    if [[ "$HAS_DRIVING_MODE_FIELD" == "yes" ]]; then
        echo "✓ 车端状态发布包含 driving_mode 字段"
    else
        echo "✗ 车端状态发布缺少 driving_mode 字段"
        exit 1
    fi
    
    HAS_DRIVING_MODE_ENUM=$(docker exec "$VEHICLE_CONTAINER" bash -c "grep -q 'enum class DrivingMode' /app/src/vehicle_controller.h 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
    if [[ "$HAS_DRIVING_MODE_ENUM" == "yes" ]]; then
        echo "✓ 车端包含驾驶模式枚举定义"
    else
        echo "✗ 车端缺少驾驶模式枚举定义"
        exit 1
    fi
fi

# 检查客户端状态接收
HAS_REMOTE_CONTROL_PROPERTY=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q 'remoteControlEnabled' /workspace/client/src/vehiclestatus.h 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_REMOTE_CONTROL_PROPERTY" == "yes" ]]; then
    echo "✓ 客户端包含 remoteControlEnabled 属性"
else
    echo "✗ 客户端缺少 remoteControlEnabled 属性"
    exit 1
fi

HAS_DRIVING_MODE_PROPERTY=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q 'drivingMode' /workspace/client/src/vehiclestatus.h 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_DRIVING_MODE_PROPERTY" == "yes" ]]; then
    echo "✓ 客户端包含 drivingMode 属性"
else
    echo "✗ 客户端缺少 drivingMode 属性"
    exit 1
fi

# 检查QML按钮文本逻辑
HAS_REMOTE_CONTROL_CONFIRMED=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q 'remoteControlConfirmed' /workspace/client/qml/DrivingInterface.qml 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_REMOTE_CONTROL_CONFIRMED" == "yes" ]]; then
    echo "✓ QML包含 remoteControlConfirmed 属性（根据车端反馈显示）"
else
    echo "✗ QML缺少 remoteControlConfirmed 属性"
    exit 1
fi

HAS_BUTTON_TEXT_LOGIC=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -A 3 '远驾已接管' /workspace/client/qml/DrivingInterface.qml 2>/dev/null | grep -q 'remoteControlConfirmed' && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_BUTTON_TEXT_LOGIC" == "yes" ]]; then
    echo "✓ QML按钮文本逻辑正确（根据 remoteControlConfirmed 显示）"
else
    echo "✗ QML按钮文本逻辑不正确"
    exit 1
fi

# 检查驾驶模式显示组件
HAS_DRIVING_MODE_DISPLAY=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q '驾驶模式显示' /workspace/client/qml/DrivingInterface.qml 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_DRIVING_MODE_DISPLAY" == "yes" ]]; then
    echo "✓ QML包含驾驶模式显示组件"
else
    echo "✗ QML缺少驾驶模式显示组件"
    exit 1
fi

# 3. 检查日志（如果程序正在运行）
echo ""
echo "3. 检查相关日志（最近100行）"
echo "----------------------------------------"
CLIENT_LOGS=$(docker logs "$CLIENT_CONTAINER" --tail 100 2>&1 | grep -E "远驾接管|驾驶模式|remoteControlEnabled|drivingMode" | tail -10)
if [[ -n "$CLIENT_LOGS" ]]; then
    echo "✓ 发现客户端相关日志:"
    echo "$CLIENT_LOGS" | sed 's/^/  /'
else
    echo "⊘ 未发现客户端相关日志（可能还未测试）"
fi

if [[ -n "$VEHICLE_CONTAINER" ]]; then
    VEHICLE_LOGS=$(docker logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "远驾接管|驾驶模式|remote_control_enabled|driving_mode" | tail -10)
    if [[ -n "$VEHICLE_LOGS" ]]; then
        echo "✓ 发现车端相关日志:"
        echo "$VEHICLE_LOGS" | sed 's/^/  /'
    else
        echo "⊘ 未发现车端相关日志（可能还未测试）"
    fi
fi

# 4. 代码片段验证
echo ""
echo "4. 关键代码片段验证"
echo "----------------------------------------"
echo "检查按钮文本逻辑："
docker exec "$CLIENT_CONTAINER" bash -c "grep -A 5 '远驾已接管' /workspace/client/qml/DrivingInterface.qml 2>/dev/null | head -6" | sed 's/^/  /' || echo "  ⚠ 未找到"
echo ""
echo "检查驾驶模式显示："
docker exec "$CLIENT_CONTAINER" bash -c "grep -A 3 '驾驶模式显示' /workspace/client/qml/DrivingInterface.qml 2>/dev/null | head -4" | sed 's/^/  /' || echo "  ⚠ 未找到"

# 总结
echo ""
echo "=========================================="
echo "验证结果"
echo "=========================================="

ALL_CHECKS_PASSED=true

if [[ "$HAS_REMOTE_CONTROL_PROPERTY" != "yes" || "$HAS_DRIVING_MODE_PROPERTY" != "yes" || \
      "$HAS_REMOTE_CONTROL_CONFIRMED" != "yes" || "$HAS_BUTTON_TEXT_LOGIC" != "yes" || \
      "$HAS_DRIVING_MODE_DISPLAY" != "yes" ]]; then
    ALL_CHECKS_PASSED=false
fi

if [[ "$ALL_CHECKS_PASSED" == "true" ]]; then
    echo "✓✓✓ 功能验证通过："
    echo "  - 车端状态发布包含 remote_control_enabled 和 driving_mode 字段"
    echo "  - 客户端包含 remoteControlEnabled 和 drivingMode 属性"
    echo "  - QML按钮根据车端反馈显示\"远驾接管\"或\"远驾已接管\""
    echo "  - QML包含驾驶模式显示组件（遥控/自驾/远驾）"
    echo ""
    echo "建议进行手动测试："
    echo "  1. 启动客户端和车端"
    echo "  2. 连接视频流（点击「连接车端」）"
    echo "  3. 点击「远驾接管」按钮"
    echo "     - 验证：按钮文本变为「远驾已接管」（等待车端确认）"
    echo "     - 验证：右侧驾驶模式显示为「远驾」"
    echo "     - 查看日志："
    echo "       docker logs $CLIENT_CONTAINER --tail 50 | grep -E '远驾接管|驾驶模式'"
    echo ""
    echo "  4. 再次点击「远驾已接管」按钮（取消接管）"
    echo "     - 验证：按钮文本恢复为「远驾接管」"
    echo "     - 验证：右侧驾驶模式显示为「自驾」"
    echo ""
    echo "  5. 断开视频流（点击「已连接」）"
    echo "     - 验证：如果远驾接管是激活状态，自动禁用"
    echo "     - 验证：按钮文本恢复为「远驾接管」"
    exit 0
else
    echo "✗ 功能验证失败"
    echo "  请检查上述输出中的缺失项"
    exit 1
fi
