#!/bin/bash
# 验证停止推流功能脚本

set -e

echo "=========================================="
echo "验证停止推流功能"
echo "=========================================="

# 查找车端容器
VEHICLE_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'remote-driving-vehicle|vehicle' | head -1)
if [[ -z "$VEHICLE_CONTAINER" ]]; then
    echo "✗ 未找到车端容器"
    exit 1
fi
echo "✓ 找到车端容器: $VEHICLE_CONTAINER"

# 查找MQTT容器
MQTT_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'mosquitto|mqtt' | head -1)
if [[ -z "$MQTT_CONTAINER" ]]; then
    echo "✗ 未找到MQTT容器"
    exit 1
fi
echo "✓ 找到MQTT容器: $MQTT_CONTAINER"

# 检查推流是否在运行
echo ""
echo "1. 检查推流状态"
echo "----------------------------------------"
STREAMING_RUNNING=$(docker exec "$VEHICLE_CONTAINER" bash -c "ps aux | grep -E 'ffmpeg.*rtmp://.*/cam_front' | grep -v grep | grep -v pgrep | wc -l" 2>/dev/null || echo "0")
if [[ "$STREAMING_RUNNING" -gt 0 ]]; then
    echo "✓ 推流正在运行（检测到 $STREAMING_RUNNING 个 ffmpeg 进程）"
else
    echo "⊘ 推流未运行（需要先启动推流）"
    echo "  提示: 请在客户端点击「连接车端」启动推流"
    read -p "  是否继续验证？(y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 0
    fi
fi

# 检查PID文件
PIDFILE_NUSCENES=$(docker exec "$VEHICLE_CONTAINER" bash -c "test -f /tmp/push-nuscenes-cameras.pid && cat /tmp/push-nuscenes-cameras.pid 2>/dev/null || echo ''" 2>/dev/null || echo "")
PIDFILE_TESTPATTERN=$(docker exec "$VEHICLE_CONTAINER" bash -c "test -f /tmp/push-testpattern.pid && cat /tmp/push-testpattern.pid 2>/dev/null || echo ''" 2>/dev/null || echo "")
if [[ -n "$PIDFILE_NUSCENES" ]]; then
    echo "✓ 找到 NuScenes 推流 PID 文件: $PIDFILE_NUSCENES"
fi
if [[ -n "$PIDFILE_TESTPATTERN" ]]; then
    echo "✓ 找到测试图案推流 PID 文件: $PIDFILE_TESTPATTERN"
fi

# 发送停止推流指令
echo ""
echo "2. 发送停止推流指令"
echo "----------------------------------------"
STOP_CMD='{"type":"stop_stream","timestamp":'$(date +%s000)'}'
echo "发送指令: $STOP_CMD"
docker exec "$MQTT_CONTAINER" mosquitto_pub -h localhost -p 1883 -t "vehicle/control" -m "$STOP_CMD" 2>&1 || {
    echo "✗ 发送停止推流指令失败"
    exit 1
}
echo "✓ 已发送停止推流指令"

# 等待推流停止
echo ""
echo "3. 等待推流停止（最多等待5秒）"
echo "----------------------------------------"
for i in {1..10}; do
    sleep 0.5
    STREAMING_RUNNING=$(docker exec "$VEHICLE_CONTAINER" bash -c "ps aux | grep -E 'ffmpeg.*rtmp://.*/cam_front' | grep -v grep | grep -v pgrep | wc -l" 2>/dev/null || echo "0")
    if [[ "$STREAMING_RUNNING" -eq 0 ]]; then
        echo "✓ 推流已停止（第 $i 次检查，耗时 $((i * 500))ms）"
        break
    fi
    if [[ $i -eq 10 ]]; then
        echo "✗ 推流未在5秒内停止（仍有 $STREAMING_RUNNING 个进程）"
        exit 1
    fi
done

# 检查PID文件是否已删除
echo ""
echo "4. 检查PID文件"
echo "----------------------------------------"
PIDFILE_NUSCENES_AFTER=$(docker exec "$VEHICLE_CONTAINER" bash -c "test -f /tmp/push-nuscenes-cameras.pid && echo 'exists' || echo 'deleted'" 2>/dev/null || echo "deleted")
PIDFILE_TESTPATTERN_AFTER=$(docker exec "$VEHICLE_CONTAINER" bash -c "test -f /tmp/push-testpattern.pid && echo 'exists' || echo 'deleted'" 2>/dev/null || echo "deleted")
if [[ "$PIDFILE_NUSCENES_AFTER" == "deleted" ]] && [[ "$PIDFILE_TESTPATTERN_AFTER" == "deleted" ]]; then
    echo "✓ PID文件已清理"
elif [[ "$PIDFILE_NUSCENES_AFTER" == "exists" ]] || [[ "$PIDFILE_TESTPATTERN_AFTER" == "exists" ]]; then
    echo "⊘ 部分PID文件仍存在（可能正在清理中）"
fi

# 检查车端日志
echo ""
echo "5. 检查车端日志"
echo "----------------------------------------"
LOG_LINES=$(docker logs "$VEHICLE_CONTAINER" --tail 50 2>&1 | grep -E "stop_stream|停止推流|停止推流进程" | tail -5)
if [[ -n "$LOG_LINES" ]]; then
    echo "✓ 车端已收到停止推流指令:"
    echo "$LOG_LINES" | sed 's/^/  /'
else
    echo "⊘ 未在日志中找到停止推流相关记录（可能日志已滚动）"
fi

# 总结
echo ""
echo "=========================================="
echo "验证结果"
echo "=========================================="
if [[ "$STREAMING_RUNNING" -eq 0 ]]; then
    echo "✓ 停止推流功能验证通过"
    echo "  - 推流进程已停止"
    echo "  - 停止指令已发送并处理"
    exit 0
else
    echo "✗ 停止推流功能验证失败"
    echo "  - 仍有 $STREAMING_RUNNING 个推流进程在运行"
    exit 1
fi
