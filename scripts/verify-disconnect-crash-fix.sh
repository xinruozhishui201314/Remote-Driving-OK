#!/bin/bash
# 验证断开连接崩溃修复（完整测试）

set -e

echo "=========================================="
echo "验证断开连接崩溃修复（完整测试）"
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
BINARY_EXISTS=$(docker exec "$CLIENT_CONTAINER" bash -c "test -f /workspace/client/build/RemoteDrivingClient && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$BINARY_EXISTS" == "yes" ]]; then
    BINARY_TIME=$(docker exec "$CLIENT_CONTAINER" bash -c "stat -c '%y' /workspace/client/build/RemoteDrivingClient 2>/dev/null | cut -d'.' -f1" 2>/dev/null || echo "unknown")
    echo "✓ 可执行文件存在（时间戳: $BINARY_TIME）"
else
    echo "✗ 可执行文件不存在，需要编译"
    exit 1
fi

# 2. 检查代码修复
echo ""
echo "2. 检查代码修复"
echo "----------------------------------------"
HAS_TIMER_FIX=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q 'QTimer::singleShot.*onStateChange\|QTimer::singleShot(0.*this.*state' /workspace/client/src/webrtcclient.cpp 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_TIMER_FIX" == "yes" ]]; then
    echo "✓ 代码中包含线程安全修复（QTimer::singleShot 或 QMetaObject::invokeMethod）"
else
    echo "⚠ 未找到线程安全修复代码"
fi

# 3. 检查最近的崩溃日志
echo ""
echo "3. 检查最近的崩溃日志（最近100行）"
echo "----------------------------------------"
CRASH_LOGS=$(docker logs "$CLIENT_CONTAINER" --tail 100 2>&1 | grep -E "Segmentation fault|Segmentation|fault.*backtrace" | tail -3)
if [[ -n "$CRASH_LOGS" ]]; then
    echo "⚠ 发现崩溃日志:"
    echo "$CRASH_LOGS" | sed 's/^/  /'
    CRASH_TIME=$(docker logs "$CLIENT_CONTAINER" --tail 100 2>&1 | grep -E "Segmentation fault" | tail -1 | grep -oE "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}" | head -1 || echo "")
    if [[ -n "$CRASH_TIME" ]]; then
        echo "  崩溃时间: $CRASH_TIME"
        echo "  请确认是否在修复之后"
    fi
else
    echo "✓ 未发现崩溃日志（最近100行）"
fi

# 4. 检查线程错误
echo ""
echo "4. 检查线程相关错误"
echo "----------------------------------------"
THREAD_ERRORS=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -E "Timers cannot be started from another thread|startTimer.*thread" | tail -3)
if [[ -n "$THREAD_ERRORS" ]]; then
    echo "⚠ 发现线程错误:"
    echo "$THREAD_ERRORS" | sed 's/^/  /'
else
    echo "✓ 未发现线程错误"
fi

# 5. 检查断开连接日志
echo ""
echo "5. 检查断开连接日志"
echo "----------------------------------------"
DISCONNECT_LOGS=$(docker logs "$CLIENT_CONTAINER" --tail 100 2>&1 | grep -E "requestStreamStop|已发送停止推流指令|手动断开|disconnectAll" | tail -5)
if [[ -n "$DISCONNECT_LOGS" ]]; then
    echo "✓ 发现断开连接日志:"
    echo "$DISCONNECT_LOGS" | sed 's/^/  /'
else
    echo "⊘ 未发现断开连接日志（可能还未测试）"
fi

# 6. 检查停止推流日志
echo ""
echo "6. 检查车端停止推流日志"
echo "----------------------------------------"
if [[ -n "$VEHICLE_CONTAINER" ]]; then
    STOP_STREAM_LOGS=$(docker logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "stop_stream|停止推流" | tail -5)
    if [[ -n "$STOP_STREAM_LOGS" ]]; then
        echo "✓ 发现停止推流日志:"
        echo "$STOP_STREAM_LOGS" | sed 's/^/  /'
    else
        echo "⊘ 未发现停止推流日志（可能还未测试）"
    fi
fi

# 7. 检查客户端进程状态
echo ""
echo "7. 检查客户端进程状态"
echo "----------------------------------------"
CLIENT_RUNNING=$(docker exec "$CLIENT_CONTAINER" bash -c "pgrep -f RemoteDrivingClient >/dev/null 2>&1 && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$CLIENT_RUNNING" == "yes" ]]; then
    CLIENT_PID=$(docker exec "$CLIENT_CONTAINER" bash -c "pgrep -f RemoteDrivingClient" 2>/dev/null | head -1)
    echo "✓ 客户端进程正常运行 (PID: $CLIENT_PID)"
else
    echo "✗ 客户端进程未运行（可能已崩溃）"
fi

# 8. 检查运行时安装
echo ""
echo "8. 检查运行时安装（不应有）"
echo "----------------------------------------"
RUNTIME_INSTALL=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -E "apt-get install|安装.*mosquitto|Installing.*mosquitto" | tail -3)
if [[ -n "$RUNTIME_INSTALL" ]]; then
    echo "⚠ 发现运行时安装日志:"
    echo "$RUNTIME_INSTALL" | sed 's/^/  /'
    echo "  提示: 应该在镜像构建时安装，而不是运行时"
else
    echo "✓ 未发现运行时安装日志（符合预期）"
fi

# 总结
echo ""
echo "=========================================="
echo "验证结果"
echo "=========================================="

CRASH_COUNT=$(docker logs "$CLIENT_CONTAINER" --tail 100 2>&1 | grep -cE "Segmentation fault|fault.*backtrace" 2>/dev/null | head -1 || echo "0")
THREAD_ERROR_COUNT=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -cE "Timers cannot be started from another thread" 2>/dev/null | head -1 || echo "0")

# 确保变量是数字（去除换行符）
CRASH_COUNT=$(echo "$CRASH_COUNT" | tr -d '\n' | head -c 10)
THREAD_ERROR_COUNT=$(echo "$THREAD_ERROR_COUNT" | tr -d '\n' | head -c 10)
CRASH_COUNT=${CRASH_COUNT:-0}
THREAD_ERROR_COUNT=${THREAD_ERROR_COUNT:-0}

if [ "$CRASH_COUNT" = "0" ] && [ "$THREAD_ERROR_COUNT" = "0" ] && [ "$CLIENT_RUNNING" = "yes" ]; then
    echo "✓✓✓ 验证通过："
    echo "  - 未发现崩溃日志"
    echo "  - 未发现线程错误"
    echo "  - 客户端进程正常运行"
    echo ""
    echo "建议进行手动测试："
    echo "  1. 在客户端UI中点击「连接车端」"
    echo "  2. 等待显示「已连接」"
    echo "  3. 点击「已连接」按钮"
    echo "  4. 观察日志确认："
    echo "     - 没有崩溃"
    echo "     - 没有线程错误"
    echo "     - 发送了停止推流指令"
    echo "     - 车端停止了推流"
    exit 0
elif [ "$CRASH_COUNT" -gt 0 ]; then
    echo "✗ 验证失败：发现 $CRASH_COUNT 条崩溃日志"
    exit 1
elif [ "$THREAD_ERROR_COUNT" -gt 0 ]; then
    echo "✗ 验证失败：发现 $THREAD_ERROR_COUNT 条线程错误"
    exit 1
elif [ "$CLIENT_RUNNING" = "no" ]; then
    echo "✗ 验证失败：客户端进程未运行"
    exit 1
else
    echo "⚠ 部分验证未通过，请检查上述输出"
    exit 1
fi
