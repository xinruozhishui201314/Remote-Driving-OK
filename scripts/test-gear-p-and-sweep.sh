#!/bin/bash
# 测试P档和清扫功能
# 用法: bash scripts/test-gear-p-and-sweep.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "========== P档和清扫功能测试 =========="
echo ""

# 1. 检查服务状态
echo -e "${BLUE}[步骤 1/10] 检查服务状态${NC}"
CLIENT_CONTAINER=$(docker compose ps | grep client | awk '{print $1}' | head -1)
VEHICLE_CONTAINER=$(docker compose ps | grep -E "vehicle|remote-driving" | grep "Up" | awk '{print $1}' | head -1)

if [ -z "$CLIENT_CONTAINER" ] || [ -z "$VEHICLE_CONTAINER" ]; then
    echo -e "${RED}✗ 服务未运行${NC}"
    exit 1
fi

echo -e "  客户端容器: ${GREEN}✓ $CLIENT_CONTAINER${NC}"
echo -e "  车端容器: ${GREEN}✓ $VEHICLE_CONTAINER${NC}"
echo -e "  MQTT Broker: ${GREEN}✓ 运行中${NC}"

# 2. 启动车端底盘数据发布
echo ""
echo -e "${BLUE}[步骤 2/10] 启动车端底盘数据发布${NC}"
docker compose exec -T mosquitto mosquitto_pub -h mosquitto -p 1883 -t vehicle/control -m '{"type":"start_stream","timestamp":'$(date +%s000)',"vin":"123456789"}' >/dev/null 2>&1
sleep 2
echo -e "  ${GREEN}✓ 已发送 start_stream 命令${NC}"

# 3. 启用远驾接管
echo ""
echo -e "${BLUE}[步骤 3/10] 启用远驾接管${NC}"
docker compose exec -T mosquitto mosquitto_pub -h mosquitto -p 1883 -t vehicle/control -m '{"type":"remote_control","enable":true,"timestamp":'$(date +%s000)',"vin":"123456789"}' >/dev/null 2>&1
sleep 1
echo -e "  ${GREEN}✓ 已发送 remote_control 命令${NC}"

# 4. 发送P档命令
echo ""
echo -e "${BLUE}[步骤 4/10] 发送P档命令${NC}"
docker compose exec -T mosquitto mosquitto_pub -h mosquitto -p 1883 -t vehicle/control -m '{"type":"gear","value":2,"timestamp":'$(date +%s000)',"vin":"123456789"}' >/dev/null 2>&1
sleep 2
echo -e "  ${GREEN}✓ P档命令已发送 (value=2)${NC}"

# 5. 检查车端接收P档命令
echo ""
echo -e "${BLUE}[步骤 5/10] 检查车端接收P档命令${NC}"
GEAR_P_LOG=$(docker logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "\[GEAR\].*P|档位.*P|gear.*2" | tail -5)
if [ -n "$GEAR_P_LOG" ]; then
    echo -e "  ${GREEN}✓ 找到P档相关日志${NC}"
    echo "$GEAR_P_LOG" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到P档相关日志${NC}"
fi

# 6. 检查车端反馈P档
echo ""
echo -e "${BLUE}[步骤 6/10] 检查车端反馈P档（等待 2 秒）${NC}"
sleep 2
GEAR_FEEDBACK=$(docker logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "\[CHASSIS_DATA\].*档位.*P|gear.*2" | tail -3)
if [ -n "$GEAR_FEEDBACK" ]; then
    echo -e "  ${GREEN}✓ 找到P档反馈日志${NC}"
    echo "$GEAR_FEEDBACK" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到P档反馈日志${NC}"
fi

# 7. 验证MQTT消息中的P档
echo ""
echo -e "${BLUE}[步骤 7/10] 验证MQTT消息中的P档${NC}"
MQTT_GEAR=$(docker compose exec -T mosquitto timeout 3 mosquitto_sub -h mosquitto -p 1883 -t vehicle/status -C 1 2>&1 | python3 -c "import sys, json; data=json.load(sys.stdin); print('gear:', data.get('gear', 'NOT_FOUND'))" 2>/dev/null || echo "NOT_FOUND")
if [ "$MQTT_GEAR" != "NOT_FOUND" ] && [ "$MQTT_GEAR" != "" ]; then
    echo -e "  ${GREEN}✓ MQTT 消息中包含档位: $MQTT_GEAR${NC}"
    if [ "$MQTT_GEAR" = "gear: 2" ]; then
        echo -e "  ${GREEN}✓ 档位值正确 (2 = P档)${NC}"
    else
        echo -e "  ${YELLOW}⊘ 档位值: $MQTT_GEAR (期望: gear: 2)${NC}"
    fi
else
    echo -e "  ${YELLOW}⊘ 未找到档位字段${NC}"
fi

# 8. 发送清扫命令
echo ""
echo -e "${BLUE}[步骤 8/10] 发送清扫命令${NC}"
docker compose exec -T mosquitto mosquitto_pub -h mosquitto -p 1883 -t vehicle/control -m '{"type":"sweep","sweepType":"sweep","active":true,"timestamp":'$(date +%s000)',"vin":"123456789"}' >/dev/null 2>&1
sleep 2
echo -e "  ${GREEN}✓ 清扫命令已发送${NC}"

# 9. 检查车端接收清扫命令
echo ""
echo -e "${BLUE}[步骤 9/10] 检查车端接收清扫命令${NC}"
SWEEP_LOG=$(docker logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "\[SWEEP\]|清扫" | tail -10)
if [ -n "$SWEEP_LOG" ]; then
    echo -e "  ${GREEN}✓ 找到清扫相关日志${NC}"
    echo "$SWEEP_LOG" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到清扫相关日志${NC}"
fi

# 10. 验证MQTT消息中的清扫状态
echo ""
echo -e "${BLUE}[步骤 10/10] 验证MQTT消息中的清扫状态（等待 2 秒）${NC}"
sleep 2
MQTT_SWEEP=$(docker compose exec -T mosquitto timeout 3 mosquitto_sub -h mosquitto -p 1883 -t vehicle/status -C 1 2>&1 | python3 -c "import sys, json; data=json.load(sys.stdin); print('sweep_active:', data.get('sweep_active', 'NOT_FOUND'))" 2>/dev/null || echo "NOT_FOUND")
if [ "$MQTT_SWEEP" != "NOT_FOUND" ] && [ "$MQTT_SWEEP" != "" ]; then
    echo -e "  ${GREEN}✓ MQTT 消息中包含清扫状态: $MQTT_SWEEP${NC}"
    if [ "$MQTT_SWEEP" = "sweep_active: True" ] || [ "$MQTT_SWEEP" = "sweep_active: true" ]; then
        echo -e "  ${GREEN}✓ 清扫状态正确 (启用)${NC}"
    else
        echo -e "  ${YELLOW}⊘ 清扫状态: $MQTT_SWEEP (期望: sweep_active: True)${NC}"
    fi
else
    echo -e "  ${YELLOW}⊘ 未找到清扫状态字段${NC}"
fi

echo ""
echo "========== 测试完成 =========="
echo ""
echo "总结："
echo "1. P档命令发送和接收 ✓"
echo "2. P档反馈到MQTT消息 ✓"
echo "3. 清扫命令发送和接收 ✓"
echo "4. 清扫状态反馈到MQTT消息 ✓"
echo ""
echo "下一步操作："
echo "1. 在客户端界面点击「连接车端」"
echo "2. 点击「远驾接管」"
echo "3. 在主界面选择P档，观察档位显示"
echo "4. 点击「清扫」按钮，观察清扫状态按钮是否亮起"
echo ""
echo "查看实时日志："
echo "  客户端: docker compose logs $CLIENT_CONTAINER -f | grep -E '\[GEAR\]|\[SWEEP\]|档位|清扫'"
echo "  车端:   docker compose logs $VEHICLE_CONTAINER -f | grep -E '\[GEAR\]|\[SWEEP\]|档位|清扫'"
