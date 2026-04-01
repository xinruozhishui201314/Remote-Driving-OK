#!/bin/bash
# 完整测试远驾接管流程
# 用法: bash scripts/test-remote-control-flow.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "========== 远驾接管流程完整测试 =========="
echo ""

# 1. 检查服务状态
echo -e "${BLUE}[步骤 1/6] 检查服务状态${NC}"
echo -n "  客户端容器: "
if docker compose ps client-dev | grep -q "Up"; then
    echo -e "${GREEN}✓ 运行中${NC}"
else
    echo -e "${RED}✗ 未运行${NC}"
    exit 1
fi

echo -n "  车端容器: "
VEHICLE_CONTAINER=$(docker compose ps | grep -E "vehicle|remote-driving" | grep "Up" | awk '{print $1}' | head -1)
if [ -n "$VEHICLE_CONTAINER" ]; then
    echo -e "${GREEN}✓ 运行中 ($VEHICLE_CONTAINER)${NC}"
else
    echo -e "${RED}✗ 未运行${NC}"
    exit 1
fi

echo -n "  MQTT Broker: "
if docker compose ps mosquitto | grep -q "Up"; then
    echo -e "${GREEN}✓ 运行中${NC}"
else
    echo -e "${RED}✗ 未运行${NC}"
    exit 1
fi

# 2. 检查客户端进程
echo ""
echo -e "${BLUE}[步骤 2/6] 检查客户端进程${NC}"
CLIENT_PID=$(docker compose exec client-dev ps aux | grep RemoteDrivingClient | grep -v grep | awk '{print $2}' | head -1)
if [ -n "$CLIENT_PID" ]; then
    echo -e "  客户端进程: ${GREEN}✓ 运行中 (PID: $CLIENT_PID)${NC}"
else
    echo -e "  客户端进程: ${RED}✗ 未运行${NC}"
    exit 1
fi

# 3. 检查 mosquitto_sub 进程
echo ""
echo -e "${BLUE}[步骤 3/6] 检查 mosquitto_sub 进程${NC}"
MOSQUITTO_SUB_PID=$(docker compose exec client-dev ps aux | grep mosquitto_sub | grep -v grep | awk '{print $2}' | head -1)
if [ -n "$MOSQUITTO_SUB_PID" ]; then
    echo -e "  mosquitto_sub: ${GREEN}✓ 运行中 (PID: $MOSQUITTO_SUB_PID)${NC}"
    docker compose exec client-dev ps aux | grep mosquitto_sub | grep -v grep | head -1 | sed 's/^/    /'
else
    echo -e "  mosquitto_sub: ${YELLOW}⊘ 未运行（可能还未连接 MQTT）${NC}"
fi

# 4. 发送测试 remote_control 消息
echo ""
echo -e "${BLUE}[步骤 4/6] 发送测试 remote_control 消息${NC}"
TEST_MSG='{"type":"remote_control","enable":true,"timestamp":'$(date +%s000)',"vin":"123456789"}'
echo "  消息内容: $TEST_MSG"
if docker compose exec -T mosquitto mosquitto_pub -h mosquitto -p 1883 -t vehicle/control -m "$TEST_MSG" 2>&1; then
    echo -e "  ${GREEN}✓ 消息已发送${NC}"
else
    echo -e "  ${RED}✗ 消息发送失败${NC}"
    exit 1
fi

# 5. 等待并检查车端响应
echo ""
echo -e "${BLUE}[步骤 5/6] 检查车端响应（等待 2 秒）${NC}"
sleep 2
VEHICLE_ACK_LOG=$(docker compose logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "publishRemoteControlAck|已成功发送远驾接管确认|remote_control_ack" | tail -5)
if [ -n "$VEHICLE_ACK_LOG" ]; then
    echo -e "  ${GREEN}✓ 车端已发送确认消息${NC}"
    echo "$VEHICLE_ACK_LOG" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到车端确认消息日志${NC}"
fi

# 6. 检查客户端是否收到消息
echo ""
echo -e "${BLUE}[步骤 6/6] 检查客户端消息接收（等待 1 秒）${NC}"
sleep 1
# 检查 mosquitto_sub 进程的输出（通过检查进程状态）
if [ -n "$MOSQUITTO_SUB_PID" ]; then
    echo -e "  ${GREEN}✓ mosquitto_sub 进程正在运行${NC}"
    echo "  提示: 客户端日志可能输出到标准输出，请查看启动脚本的输出"
else
    echo -e "  ${YELLOW}⊘ mosquitto_sub 进程未运行${NC}"
    echo "  可能原因:"
    echo "    1. 客户端还未调用 connectToBroker()"
    echo "    2. connectToBroker() 调用失败"
    echo "    3. mosquitto_sub 启动失败"
fi

echo ""
echo "========== 测试完成 =========="
echo ""
echo "下一步操作："
echo "1. 在客户端界面点击「连接车端」按钮（这会触发 connectToBroker()）"
echo "2. 等待 mosquitto_sub 进程启动"
echo "3. 点击「远驾接管」按钮"
echo "4. 重新运行此脚本验证消息流"
echo ""
echo "查看实时日志："
echo "  客户端: docker compose logs client-dev -f"
echo "  车端:   docker compose logs $VEHICLE_CONTAINER -f"
echo "  MQTT:   docker compose exec mosquitto mosquitto_sub -h localhost -p 1883 -t 'vehicle/status' -v"
