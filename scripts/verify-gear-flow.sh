#!/bin/bash
# 验证档位选择流程
# 用法: bash scripts/verify-gear-flow.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."
# shellcheck source=lib/mqtt_control_json.sh
source "$SCRIPT_DIR/lib/mqtt_control_json.sh"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "========== 档位选择流程验证 =========="
echo ""

# 1. 检查服务状态
echo -e "${BLUE}[步骤 1/7] 检查服务状态${NC}"
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

# 2. 发送测试档位命令
echo ""
echo -e "${BLUE}[步骤 2/7] 发送测试档位命令${NC}"
TEST_MSG="$(mqtt_json_gear "123456789" 1)"
echo "  消息内容: $TEST_MSG"
if docker compose exec -T mosquitto mosquitto_pub -h mosquitto -p 1883 -t vehicle/control -m "$TEST_MSG" 2>&1; then
    echo -e "  ${GREEN}✓ 档位命令已发送 (D档)${NC}"
else
    echo -e "  ${RED}✗ 档位命令发送失败${NC}"
    exit 1
fi

# 3. 等待并检查车端接收
echo ""
echo -e "${BLUE}[步骤 3/7] 检查车端接收档位命令（等待 1 秒）${NC}"
sleep 1
VEHICLE_GEAR_LOG=$(docker compose logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "\[GEAR\]|档位命令|gear" | tail -10)
if [ -n "$VEHICLE_GEAR_LOG" ]; then
    echo -e "  ${GREEN}✓ 找到车端档位日志${NC}"
    echo "$VEHICLE_GEAR_LOG" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到车端档位日志${NC}"
fi

# 4. 检查车端处理档位命令
echo ""
echo -e "${BLUE}[步骤 4/7] 检查车端处理档位命令${NC}"
VEHICLE_PROCESS_LOG=$(docker compose logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "\[GEAR\].*档位命令已应用|applyGear|processCommand.*gear" | tail -5)
if [ -n "$VEHICLE_PROCESS_LOG" ]; then
    echo -e "  ${GREEN}✓ 找到车端处理日志${NC}"
    echo "$VEHICLE_PROCESS_LOG" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到车端处理日志${NC}"
fi

# 5. 检查车端发送档位反馈
echo ""
echo -e "${BLUE}[步骤 5/7] 检查车端发送档位反馈（等待 1 秒）${NC}"
sleep 1
VEHICLE_FEEDBACK_LOG=$(docker compose logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "\[CHASSIS_DATA\].*档位|发布.*gear" | tail -5)
if [ -n "$VEHICLE_FEEDBACK_LOG" ]; then
    echo -e "  ${GREEN}✓ 找到车端反馈日志${NC}"
    echo "$VEHICLE_FEEDBACK_LOG" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到车端反馈日志${NC}"
fi

# 6. 检查客户端接收档位
echo ""
echo -e "${BLUE}[步骤 6/7] 检查客户端接收档位${NC}"
CLIENT_GEAR_LOG=$(docker compose logs client-dev --tail 200 2>&1 | grep -E "\[GEAR\]|档位|gear" | tail -10)
if [ -n "$CLIENT_GEAR_LOG" ]; then
    echo -e "  ${GREEN}✓ 找到客户端档位日志${NC}"
    echo "$CLIENT_GEAR_LOG" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到客户端档位日志（可能还未连接或消息未到达）${NC}"
fi

# 7. 检查客户端状态更新
echo ""
echo -e "${BLUE}[步骤 7/7] 检查客户端状态更新${NC}"
CLIENT_STATUS_LOG=$(docker compose logs client-dev --tail 200 2>&1 | grep -E "\[GEAR\].*档位变化|档位已更新" | tail -5)
if [ -n "$CLIENT_STATUS_LOG" ]; then
    echo -e "  ${GREEN}✓ 找到客户端状态更新日志${NC}"
    echo "$CLIENT_STATUS_LOG" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到客户端状态更新日志${NC}"
fi

echo ""
echo "========== 验证完成 =========="
echo ""
echo "提示："
echo "1. 如果看到 ⊘，表示该步骤可能还未执行（需要手动操作）"
echo "2. 请在客户端界面："
echo "   - 登录（123/123）"
echo "   - 选择车辆（123456789）"
echo "   - 点击「连接车端」"
echo "   - 点击「远驾接管」"
echo "   - 在主界面选择档位（P/N/R/D）"
echo "3. 然后重新运行此脚本验证"
echo ""
echo "查看实时日志："
echo "  客户端: docker compose logs client-dev -f | grep -E '\[GEAR\]|档位'"
echo "  车端:   docker compose logs $VEHICLE_CONTAINER -f | grep -E '\[GEAR\]|档位'"
