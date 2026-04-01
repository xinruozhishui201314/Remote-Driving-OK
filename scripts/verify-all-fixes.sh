#!/bin/bash
# 完整验证所有修复的脚本

set -e

echo "=========================================="
echo "完整验证所有修复"
echo "=========================================="

# 1. 验证断开连接崩溃修复
echo ""
echo "1. 验证断开连接崩溃修复"
echo "=========================================="
bash scripts/verify-disconnect-fix.sh
DISCONNECT_FIX_RESULT=$?

# 2. 验证停止推流功能
echo ""
echo "2. 验证停止推流功能"
echo "=========================================="
if [[ -f scripts/verify-stop-stream.sh ]]; then
    bash scripts/verify-stop-stream.sh
    STOP_STREAM_RESULT=$?
else
    echo "⊘ 停止推流验证脚本不存在，跳过"
    STOP_STREAM_RESULT=0
fi

# 3. 检查最近的日志（综合检查）
echo ""
echo "3. 综合日志检查"
echo "=========================================="

CLIENT_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'client-dev|client' | head -1)
VEHICLE_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'vehicle' | head -1)

if [[ -n "$CLIENT_CONTAINER" ]]; then
    echo "客户端日志检查："
    echo "----------------------------------------"
    
    # 检查崩溃
    CRASH_COUNT=$(docker logs "$CLIENT_CONTAINER" --tail 500 2>&1 | grep -cE "Segmentation fault|Segmentation|fault.*backtrace" || echo "0")
    if [[ "$CRASH_COUNT" -eq 0 ]]; then
        echo "✓ 未发现崩溃日志（最近500行）"
    else
        echo "⚠ 发现 $CRASH_COUNT 条崩溃日志"
        docker logs "$CLIENT_CONTAINER" --tail 500 2>&1 | grep -E "Segmentation fault|Segmentation|fault.*backtrace" | tail -3 | sed 's/^/  /'
    fi
    
    # 检查断开连接
    DISCONNECT_COUNT=$(docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -cE "requestStreamStop|已发送停止推流指令|手动断开" || echo "0")
    if [[ "$DISCONNECT_COUNT" -gt 0 ]]; then
        echo "✓ 发现 $DISCONNECT_COUNT 条断开连接日志"
        docker logs "$CLIENT_CONTAINER" --tail 200 2>&1 | grep -E "requestStreamStop|已发送停止推流指令|手动断开" | tail -3 | sed 's/^/  /'
    else
        echo "⊘ 未发现断开连接日志（可能还未测试）"
    fi
    
    # 检查自动重连（手动断开后不应有）
    RECONNECT_AFTER_MANUAL=$(docker logs "$CLIENT_CONTAINER" --tail 500 2>&1 | grep -A 5 "手动断开" | grep -cE "自动重连|第.*次.*重连" || echo "0")
    if [[ "$RECONNECT_AFTER_MANUAL" -eq 0 ]]; then
        echo "✓ 手动断开后未发现自动重连（符合预期）"
    else
        echo "⚠ 手动断开后仍发现自动重连（可能存在问题）"
    fi
fi

if [[ -n "$VEHICLE_CONTAINER" ]]; then
    echo ""
    echo "车端日志检查："
    echo "----------------------------------------"
    
    # 检查停止推流指令
    STOP_STREAM_COUNT=$(docker logs "$VEHICLE_CONTAINER" --tail 200 2>&1 | grep -cE "stop_stream|停止推流" || echo "0")
    if [[ "$STOP_STREAM_COUNT" -gt 0 ]]; then
        echo "✓ 发现 $STOP_STREAM_COUNT 条停止推流日志"
        docker logs "$VEHICLE_CONTAINER" --tail 200 2>&1 | grep -E "stop_stream|停止推流" | tail -3 | sed 's/^/  /'
    else
        echo "⊘ 未发现停止推流日志（可能还未测试）"
    fi
fi

# 4. 检查进程状态
echo ""
echo "4. 检查进程状态"
echo "=========================================="

if [[ -n "$CLIENT_CONTAINER" ]]; then
    CLIENT_RUNNING=$(docker exec "$CLIENT_CONTAINER" bash -c "pgrep -f RemoteDrivingClient >/dev/null 2>&1 && echo 'yes' || echo 'no'" 2>/dev/null || echo "unknown")
    if [[ "$CLIENT_RUNNING" == "yes" ]]; then
        echo "✓ 客户端进程正常运行"
    else
        echo "✗ 客户端进程未运行"
    fi
fi

if [[ -n "$VEHICLE_CONTAINER" ]]; then
    VEHICLE_RUNNING=$(docker exec "$VEHICLE_CONTAINER" bash -c "pgrep -f vehicle_controller >/dev/null 2>&1 && echo 'yes' || echo 'no'" 2>/dev/null || echo "unknown")
    if [[ "$VEHICLE_RUNNING" == "yes" ]]; then
        echo "✓ 车端进程正常运行"
    else
        echo "⊘ 车端进程状态未知"
    fi
fi

# 总结
echo ""
echo "=========================================="
echo "验证总结"
echo "=========================================="

TOTAL_ISSUES=0

if [[ "$DISCONNECT_FIX_RESULT" -ne 0 ]]; then
    echo "✗ 断开连接崩溃修复验证失败"
    TOTAL_ISSUES=$((TOTAL_ISSUES + 1))
else
    echo "✓ 断开连接崩溃修复验证通过"
fi

if [[ "$STOP_STREAM_RESULT" -ne 0 ]]; then
    echo "✗ 停止推流功能验证失败"
    TOTAL_ISSUES=$((TOTAL_ISSUES + 1))
else
    echo "✓ 停止推流功能验证通过（或未测试）"
fi

if [[ "$CRASH_COUNT" -gt 0 ]]; then
    echo "⚠ 发现 $CRASH_COUNT 条崩溃日志（需要确认是否在修复之后）"
    TOTAL_ISSUES=$((TOTAL_ISSUES + 1))
else
    echo "✓ 未发现崩溃日志"
fi

if [[ "$TOTAL_ISSUES" -eq 0 ]]; then
    echo ""
    echo "✓✓✓ 所有验证通过 ✓✓✓"
    echo ""
    echo "建议进行手动测试："
    echo "  1. 在客户端UI中点击「连接车端」"
    echo "  2. 等待显示「已连接」"
    echo "  3. 点击「已连接」按钮"
    echo "  4. 观察日志确认："
    echo "     - 没有崩溃"
    echo "     - 发送了停止推流指令"
    echo "     - 没有自动重连"
    echo "     - 车端停止了推流"
    exit 0
else
    echo ""
    echo "⚠ 发现 $TOTAL_ISSUES 个问题，请检查上述输出"
    exit 1
fi
