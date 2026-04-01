#!/bin/bash
# 验证推流自动恢复功能
# 用途：验证推流进程在容器重启或进程意外退出后能够自动恢复

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo "=== 推流自动恢复验证 ==="
echo ""

# 1. 检查推流进程
echo "[1] 检查推流进程..."
if pgrep -f "ffmpeg.*rtmp://.*/cam_front" >/dev/null 2>&1; then
    echo -e "  ${GREEN}✓${NC} 推流进程正在运行"
    ps aux | grep -E "ffmpeg.*rtmp://.*/cam_front" | grep -v grep | head -1
else
    echo -e "  ${YELLOW}⊘${NC} 推流进程未运行（可能未启动推流）"
fi

# 2. 检查 PID 文件
echo ""
echo "[2] 检查 PID 文件..."
if [ -f /tmp/push-nuscenes-cameras.pid ]; then
    pid=$(cat /tmp/push-nuscenes-cameras.pid 2>/dev/null || echo "")
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        echo -e "  ${GREEN}✓${NC} NuScenes 推流 PID 文件存在且进程存活 (PID: $pid)"
    else
        echo -e "  ${YELLOW}⊘${NC} NuScenes 推流 PID 文件存在但进程不存在 (PID: $pid)"
    fi
elif [ -f /tmp/push-testpattern.pid ]; then
    pid=$(cat /tmp/push-testpattern.pid 2>/dev/null || echo "")
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        echo -e "  ${GREEN}✓${NC} 测试图案推流 PID 文件存在且进程存活 (PID: $pid)"
    else
        echo -e "  ${YELLOW}⊘${NC} 测试图案推流 PID 文件存在但进程不存在 (PID: $pid)"
    fi
else
    echo -e "  ${YELLOW}⊘${NC} PID 文件不存在（可能未启动推流）"
fi

# 3. 检查推流日志
echo ""
echo "[3] 检查推流日志..."
if [ -f /tmp/push-stream.log ]; then
    echo -e "  ${GREEN}✓${NC} 推流日志文件存在: /tmp/push-stream.log"
    echo "  最近 5 行日志："
    tail -5 /tmp/push-stream.log | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘${NC} 推流日志文件不存在"
fi

# 4. 检查车端容器状态
echo ""
echo "[4] 检查车端容器状态..."
if command -v docker >/dev/null 2>&1; then
    if docker ps --format '{{.Names}}' | grep -q "remote-driving-vehicle\|vehicle"; then
        container_name=$(docker ps --format '{{.Names}}' | grep -E "remote-driving-vehicle|vehicle" | head -1)
        echo -e "  ${GREEN}✓${NC} 车端容器正在运行: $container_name"
        
        # 检查车端日志中的推流相关消息
        echo "  最近推流相关日志："
        docker logs "$container_name" 2>&1 | grep -iE "stream|推流|start_stream" | tail -5 | sed 's/^/    /' || echo "    (无相关日志)"
    else
        echo -e "  ${YELLOW}⊘${NC} 车端容器未运行"
    fi
else
    echo -e "  ${YELLOW}⊘${NC} Docker 不可用，跳过容器检查"
fi

# 5. 模拟进程退出测试（可选）
echo ""
echo "[5] 模拟推流进程退出测试（可选）..."
read -p "  是否执行模拟测试？(y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "  正在停止推流进程..."
    pkill -f "ffmpeg.*rtmp://.*/cam_front" 2>/dev/null || true
    sleep 2
    
    echo "  等待自动恢复（最多 15 秒）..."
    recovered=false
    for i in {1..15}; do
        if pgrep -f "ffmpeg.*rtmp://.*/cam_front" >/dev/null 2>&1; then
            echo -e "  ${GREEN}✓${NC} 推流已自动恢复（${i} 秒后）"
            recovered=true
            break
        fi
        sleep 1
    done
    
    if [ "$recovered" = false ]; then
        echo -e "  ${YELLOW}⊘${NC} 推流未自动恢复（可能需要手动触发 start_stream 或等待健康检查）"
        echo "  提示：车端健康检查每 10 秒执行一次，重启间隔至少 5 秒"
    fi
else
    echo "  跳过模拟测试"
fi

echo ""
echo "=== 验证完成 ==="
echo ""
echo "提示："
echo "  - 如果推流未运行，请在客户端点击「连接车端」触发 start_stream"
echo "  - 容器重启后，推流会在 10-15 秒内自动恢复（如果之前收到过 start_stream）"
echo "  - 查看详细日志：docker compose logs vehicle | grep -i stream"
