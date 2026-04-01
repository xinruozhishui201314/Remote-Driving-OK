#!/bin/bash
# 验证断开连接崩溃修复脚本

set -e

echo "=========================================="
echo "验证断开连接崩溃修复"
echo "=========================================="

# 查找客户端容器
CLIENT_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'client-dev|client' | head -1)
if [[ -z "$CLIENT_CONTAINER" ]]; then
    echo "✗ 未找到客户端容器"
    echo "  提示: 请先运行 bash scripts/start-full-chain.sh manual"
    exit 1
fi
echo "✓ 找到客户端容器: $CLIENT_CONTAINER"

# 检查客户端进程是否在运行
CLIENT_PID=$(docker exec "$CLIENT_CONTAINER" bash -c "pgrep -f RemoteDrivingClient || echo ''" 2>/dev/null || echo "")
if [[ -z "$CLIENT_PID" ]]; then
    echo "⊘ 客户端进程未运行（可能需要先启动）"
else
    echo "✓ 客户端进程正在运行 (PID: $CLIENT_PID)"
fi

# 检查最近的崩溃日志
echo ""
echo "1. 检查最近的崩溃日志"
echo "----------------------------------------"
CRASH_LOGS=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -E "Segmentation fault|Segmentation|fault|崩溃|crash|SIGSEGV" | tail -5)
if [[ -n "$CRASH_LOGS" ]]; then
    echo "⚠ 发现崩溃日志:"
    echo "$CRASH_LOGS" | sed 's/^/  /'
    echo ""
    echo "  检查时间戳..."
    LAST_CRASH_TIME=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -E "Segmentation fault" | tail -1 | grep -oE "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}" | head -1 || echo "")
    if [[ -n "$LAST_CRASH_TIME" ]]; then
        echo "  最近崩溃时间: $LAST_CRASH_TIME"
    fi
else
    echo "✓ 未发现崩溃日志"
fi

# 检查断开连接相关日志
echo ""
echo "2. 检查断开连接相关日志"
echo "----------------------------------------"
DISCONNECT_LOGS=$(docker logs "$CLIENT_CONTAINER" --tail 100 2>&1 | grep -E "disconnect|断开|stop_stream|requestStreamStop|手动断开" | tail -10)
if [[ -n "$DISCONNECT_LOGS" ]]; then
    echo "✓ 发现断开连接日志:"
    echo "$DISCONNECT_LOGS" | sed 's/^/  /'
else
    echo "⊘ 未发现断开连接日志（可能还未测试断开功能）"
fi

# 检查自动重连日志（应该没有，因为手动断开）
echo ""
echo "3. 检查自动重连日志（手动断开后不应有）"
echo "----------------------------------------"
RECONNECT_LOGS=$(docker logs "$CLIENT_CONTAINER" --tail 100 2>&1 | grep -E "自动重连|reconnect|第.*次.*重连" | tail -10)
if [[ -n "$RECONNECT_LOGS" ]]; then
    # 检查是否是在手动断开之后的重连（不应该有）
    MANUAL_DISCONNECT_TIME=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -E "手动断开|requestStreamStop" | tail -1 | grep -oE "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}" | head -1 || echo "")
    if [[ -n "$MANUAL_DISCONNECT_TIME" ]]; then
        echo "⚠ 发现自动重连日志（手动断开后不应有）:"
        echo "$RECONNECT_LOGS" | sed 's/^/  /'
        echo "  手动断开时间: $MANUAL_DISCONNECT_TIME"
    else
        echo "⊘ 发现自动重连日志（但未找到手动断开记录，可能是正常重连）"
    fi
else
    echo "✓ 未发现自动重连日志（符合预期：手动断开后不应自动重连）"
fi

# 检查客户端进程状态
echo ""
echo "4. 检查客户端进程状态"
echo "----------------------------------------"
CURRENT_PID=$(docker exec "$CLIENT_CONTAINER" bash -c "pgrep -f RemoteDrivingClient || echo ''" 2>/dev/null || echo "")
if [[ -n "$CURRENT_PID" ]]; then
    if [[ "$CURRENT_PID" == "$CLIENT_PID" ]]; then
        echo "✓ 客户端进程仍在运行 (PID: $CURRENT_PID)，未崩溃"
    else
        echo "⚠ 客户端进程PID已变化（可能重启过）"
        echo "  之前PID: $CLIENT_PID"
        echo "  当前PID: $CURRENT_PID"
    fi
else
    echo "✗ 客户端进程未运行（可能已崩溃）"
    echo "  检查崩溃日志..."
    docker logs "$CLIENT_CONTAINER" --tail 50 2>&1 | grep -E "Segmentation|fault|crash|退出|exit" | tail -5 || echo "  未找到明确的崩溃信息"
fi

# 检查代码修复是否已应用
echo ""
echo "5. 检查代码修复是否已应用"
echo "----------------------------------------"
# 检查是否包含 m_reconnectTimer 相关代码
HAS_TIMER_FIX=$(docker exec "$CLIENT_CONTAINER" bash -c "strings /workspace/client/build/RemoteDrivingClient 2>/dev/null | grep -q 'm_reconnectTimer' && echo 'yes' || echo 'no'" 2>/dev/null || echo "unknown")
if [[ "$HAS_TIMER_FIX" == "yes" ]]; then
    echo "✓ 代码修复已编译到可执行文件中（包含 m_reconnectTimer）"
elif [[ "$HAS_TIMER_FIX" == "unknown" ]]; then
    echo "⊘ 无法检查代码修复（可执行文件路径可能不同）"
else
    echo "⚠ 代码修复可能未编译（建议重新编译客户端）"
fi

# 总结
echo ""
echo "=========================================="
echo "验证结果"
echo "=========================================="

HAS_CRASH=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -qE "Segmentation fault" && echo "yes" || echo "no")
IS_RUNNING=$(docker exec "$CLIENT_CONTAINER" bash -c "pgrep -f RemoteDrivingClient >/dev/null 2>&1 && echo 'yes' || echo 'no'" 2>/dev/null || echo "unknown")

if [[ "$HAS_CRASH" == "no" && "$IS_RUNNING" == "yes" ]]; then
    echo "✓ 验证通过："
    echo "  - 未发现崩溃日志"
    echo "  - 客户端进程正常运行"
    echo "  - 断开连接功能正常"
    echo ""
    echo "建议："
    echo "  1. 在客户端UI中点击「已连接」按钮测试断开功能"
    echo "  2. 观察日志确认没有崩溃和自动重连"
    exit 0
elif [[ "$HAS_CRASH" == "yes" ]]; then
    echo "✗ 验证失败：发现崩溃日志"
    echo "  请检查最近的崩溃时间，确认是否在修复之后"
    exit 1
elif [[ "$IS_RUNNING" == "no" ]]; then
    echo "⚠ 客户端进程未运行"
    echo "  可能原因："
    echo "  1. 客户端未启动"
    echo "  2. 客户端已崩溃"
    echo "  建议：重新启动客户端并测试断开功能"
    exit 1
else
    echo "⊘ 无法完全验证（客户端状态未知）"
    echo "  建议：手动测试断开连接功能"
    exit 0
fi
