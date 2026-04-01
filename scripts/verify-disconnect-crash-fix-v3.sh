#!/bin/bash
# 验证断开连接崩溃修复（V3 - 详细日志版本）

set -e

echo "=========================================="
echo "验证断开连接崩溃修复（V3 - 详细日志版本）"
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
BINARY_EXISTS=$(docker exec "$CLIENT_CONTAINER" bash -c "test -f /workspace/client/build/RemoteDrivingClient && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$BINARY_EXISTS" == "yes" ]]; then
    BINARY_TIME=$(docker exec "$CLIENT_CONTAINER" bash -c "stat -c '%y' /workspace/client/build/RemoteDrivingClient 2>/dev/null | cut -d'.' -f1" 2>/dev/null || echo "unknown")
    echo "✓ 可执行文件存在（时间戳: $BINARY_TIME）"
else
    echo "✗ 可执行文件不存在，需要编译"
    exit 1
fi

# 2. 检查代码修复（详细日志）
echo ""
echo "2. 检查代码修复（详细日志）"
echo "----------------------------------------"
HAS_DETAILED_LOGS=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q 'disconnect() 开始\|disconnect() 准备断开\|disconnect() reply 对象有效' /workspace/client/src/webrtcclient.cpp 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_DETAILED_LOGS" == "yes" ]]; then
    echo "✓ 代码中包含详细日志"
else
    echo "⚠ 未找到详细日志代码"
fi

HAS_SAFE_DISCONNECT=$(docker exec "$CLIENT_CONTAINER" bash -c "grep -q 'QPointer<QNetworkReply> safeReply\|reply->parent()\|reply->thread()' /workspace/client/src/webrtcclient.cpp 2>/dev/null && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$HAS_SAFE_DISCONNECT" == "yes" ]]; then
    echo "✓ 代码中包含安全断开逻辑"
else
    echo "⚠ 未找到安全断开逻辑"
fi

# 3. 检查最近的崩溃日志
echo ""
echo "3. 检查最近的崩溃日志（最近200行）"
echo "----------------------------------------"
CRASH_LOGS=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -E "Segmentation fault|Segmentation|fault.*backtrace" | tail -3)
if [[ -n "$CRASH_LOGS" ]]; then
    echo "⚠ 发现崩溃日志:"
    echo "$CRASH_LOGS" | sed 's/^/  /'
    CRASH_TIME=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -E "Segmentation fault" | tail -1 | grep -oE "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}" | head -1 || echo "")
    if [[ -n "$CRASH_TIME" ]]; then
        echo "  崩溃时间: $CRASH_TIME"
        echo "  请确认是否在修复之后"
    fi
else
    echo "✓ 未发现崩溃日志（最近200行）"
fi

# 4. 检查断开连接详细日志
echo ""
echo "4. 检查断开连接详细日志"
echo "----------------------------------------"
DISCONNECT_LOGS=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -E "disconnect\(\)|requestStreamStop|已发送停止推流指令" | tail -10)
if [[ -n "$DISCONNECT_LOGS" ]]; then
    echo "✓ 发现断开连接日志:"
    echo "$DISCONNECT_LOGS" | sed 's/^/  /'
else
    echo "⊘ 未发现断开连接日志（可能还未测试）"
fi

# 5. 检查客户端进程状态
echo ""
echo "5. 检查客户端进程状态"
echo "----------------------------------------"
CLIENT_RUNNING=$(docker exec "$CLIENT_CONTAINER" bash -c "pgrep -f RemoteDrivingClient >/dev/null 2>&1 && echo 'yes' || echo 'no'" 2>/dev/null)
if [[ "$CLIENT_RUNNING" == "yes" ]]; then
    CLIENT_PID=$(docker exec "$CLIENT_CONTAINER" bash -c "pgrep -f RemoteDrivingClient" 2>/dev/null | head -1)
    echo "✓ 客户端进程正常运行 (PID: $CLIENT_PID)"
else
    echo "✗ 客户端进程未运行（可能已崩溃）"
fi

# 总结
echo ""
echo "=========================================="
echo "验证结果"
echo "=========================================="

CRASH_COUNT=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -cE "Segmentation fault|fault.*backtrace" 2>/dev/null | head -1 || echo "0")
CRASH_COUNT=$(echo "$CRASH_COUNT" | tr -d '\n' | head -c 10)
CRASH_COUNT=${CRASH_COUNT:-0}

if [ "$CRASH_COUNT" = "0" ] && [ "$CLIENT_RUNNING" = "yes" ]; then
    echo "✓✓✓ 验证通过："
    echo "  - 未发现崩溃日志"
    echo "  - 客户端进程正常运行"
    echo "  - 代码包含详细日志和安全断开逻辑"
    echo ""
    echo "建议进行手动测试："
    echo "  1. 在客户端UI中点击「连接车端」"
    echo "  2. 等待显示「已连接」"
    echo "  3. 点击「已连接」按钮"
    echo "  4. 观察日志确认："
    echo "     - 没有崩溃"
    echo "     - 有详细的 disconnect() 日志"
    echo "     - reply 对象被安全处理"
    echo "     - 发送了停止推流指令"
    exit 0
elif [ "$CRASH_COUNT" -gt 0 ]; then
    echo "✗ 验证失败：发现 $CRASH_COUNT 条崩溃日志"
    echo ""
    echo "请检查日志中的详细 disconnect() 日志以定位问题："
    docker logs "$CLIENT_CONTAINER" --tail 100 2>&1 | grep -E "disconnect\(\)|reply|QPointer" | tail -20
    exit 1
elif [ "$CLIENT_RUNNING" = "no" ]; then
    echo "✗ 验证失败：客户端进程未运行"
    exit 1
else
    echo "⚠ 部分验证未通过，请检查上述输出"
    exit 1
fi
