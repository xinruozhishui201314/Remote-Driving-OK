#!/bin/bash
# 验证远驾接管确认消息接收功能
# 用法: bash scripts/verify-remote-control-ack.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "========== 远驾接管确认消息接收验证 =========="
echo ""

# 1. 检查客户端容器是否运行
echo -n "[1/7] 检查客户端容器状态... "
if docker compose ps client-dev | grep -q "Up"; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC} 客户端容器未运行"
    exit 1
fi

# 2. 检查车端容器是否运行
echo -n "[2/7] 检查车端容器状态... "
if docker compose ps | grep -E "vehicle|remote-driving" | grep -q "Up"; then
    echo -e "${GREEN}✓${NC}"
    VEHICLE_CONTAINER=$(docker compose ps | grep -E "vehicle|remote-driving" | grep "Up" | awk '{print $1}' | head -1)
    echo "  容器名称: $VEHICLE_CONTAINER"
else
    echo -e "${RED}✗${NC} 车端容器未运行"
    exit 1
fi

# 3. 检查 MQTT Broker 是否运行
echo -n "[3/7] 检查 MQTT Broker 状态... "
if docker compose ps mosquitto | grep -q "Up"; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC} MQTT Broker 未运行"
    exit 1
fi

# 4. 检查客户端日志中是否有 mosquitto_sub 启动成功的日志
echo -n "[4/7] 检查客户端 mosquitto_sub 启动状态... "
CLIENT_LOG=$(docker compose logs client-dev --tail 200 2>&1 | grep -E "mosquitto_sub|MQTT.*订阅" | tail -5)
if echo "$CLIENT_LOG" | grep -q "mosquitto_sub.*已启动\|订阅完成"; then
    echo -e "${GREEN}✓${NC}"
    echo "  日志片段:"
    echo "$CLIENT_LOG" | head -3 | sed 's/^/    /'
else
    echo -e "${YELLOW}⊘${NC} 未找到 mosquitto_sub 启动日志（可能还未连接）"
fi

# 5. 检查车端是否已发送 remote_control_ack 消息（通过 MQTT Broker 日志）
echo -n "[5/7] 检查车端 remote_control_ack 消息发送... "
VEHICLE_CONTAINER=$(docker compose ps | grep -E "vehicle|remote-driving" | grep "Up" | awk '{print $1}' | head -1)
if [ -z "$VEHICLE_CONTAINER" ]; then
    VEHICLE_CONTAINER="remote-driving-vehicle-1"  # 默认容器名
fi
VEHICLE_LOG=$(docker compose logs "$VEHICLE_CONTAINER" --tail 500 2>&1 | grep -E "publishRemoteControlAck|remote_control_ack|已成功发送远驾接管确认" | tail -10)
if echo "$VEHICLE_LOG" | grep -q "已成功发送远驾接管确认\|publishRemoteControlAck"; then
    echo -e "${GREEN}✓${NC}"
    echo "  日志片段:"
    echo "$VEHICLE_LOG" | head -5 | sed 's/^/    /'
else
    echo -e "${YELLOW}⊘${NC} 未找到车端发送确认消息的日志（可能还未点击按钮）"
fi

# 6. 检查客户端是否收到 remote_control_ack 消息
echo -n "[6/7] 检查客户端 remote_control_ack 消息接收... "
CLIENT_ACK_LOG=$(docker compose logs client-dev --tail 500 2>&1 | grep -E "收到远驾接管确认|remote_control_ack|mosquitto_sub.*收到消息" | tail -10)
if echo "$CLIENT_ACK_LOG" | grep -q "收到远驾接管确认\|remote_control_ack"; then
    echo -e "${GREEN}✓${NC}"
    echo "  日志片段:"
    echo "$CLIENT_ACK_LOG" | head -5 | sed 's/^/    /'
else
    echo -e "${YELLOW}⊘${NC} 未找到客户端接收确认消息的日志（可能还未点击按钮或消息未到达）"
fi

# 7. 检查客户端状态更新日志
echo -n "[7/7] 检查客户端状态更新日志... "
STATUS_LOG=$(docker compose logs client-dev --tail 500 2>&1 | grep -E "远驾接管状态变化|驾驶模式变化|remoteControlEnabled.*true" | tail -10)
if echo "$STATUS_LOG" | grep -q "远驾接管状态变化.*true\|驾驶模式变化.*远驾"; then
    echo -e "${GREEN}✓${NC}"
    echo "  日志片段:"
    echo "$STATUS_LOG" | head -5 | sed 's/^/    /'
else
    echo -e "${YELLOW}⊘${NC} 未找到状态更新日志（可能还未点击按钮）"
fi

echo ""
echo "========== 验证完成 =========="
echo ""
echo "提示："
echo "1. 如果看到 ⊘，表示该步骤可能还未执行（需要手动点击按钮）"
echo "2. 请在客户端界面："
echo "   - 登录（123/123）"
echo "   - 选择车辆（123456789）"
echo "   - 点击「连接车端」"
echo "   - 点击「远驾接管」按钮"
echo "3. 然后重新运行此脚本验证"
echo ""
echo "查看完整日志："
echo "  客户端: docker compose logs client-dev --tail 200 -f"
echo "  车端:   docker compose logs vehicle --tail 200 -f"
