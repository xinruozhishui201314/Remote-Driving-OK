#!/bin/bash
# 完整的车辆底盘数据流验证脚本
# 包括：检查、重启、验证、日志分析

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}车辆底盘数据流完整验证${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# 查找容器
VEHICLE_CONTAINER=$(docker ps --format '{{.Names}}' | grep -i vehicle | head -1)
CLIENT_CONTAINER=$(docker ps --format '{{.Names}}' | grep -i "client-dev\|client" | head -1)

if [ -z "$VEHICLE_CONTAINER" ] || [ -z "$CLIENT_CONTAINER" ]; then
    echo -e "${RED}错误：未找到容器${NC}"
    echo "请先启动: bash scripts/start-full-chain.sh manual"
    exit 1
fi

echo -e "${GREEN}✓${NC} 车端容器: $VEHICLE_CONTAINER"
echo -e "${GREEN}✓${NC} 客户端容器: $CLIENT_CONTAINER"
echo ""

# 1. 检查车端MQTT连接
echo -e "${CYAN}[1/6] 检查车端MQTT连接...${NC}"
VEHICLE_MQTT=$(docker logs "$VEHICLE_CONTAINER" 2>&1 | grep -i "连接 MQTT\|MQTT.*连接\|已订阅" | tail -3)
if [ -n "$VEHICLE_MQTT" ]; then
    echo -e "${GREEN}✓${NC} 车端MQTT连接正常"
    echo "$VEHICLE_MQTT" | while read line; do echo "  $line"; done
else
    echo -e "${RED}✗${NC} 未找到车端MQTT连接日志"
fi
echo ""

# 2. 检查车端是否收到start_stream
echo -e "${CYAN}[2/6] 检查车端是否收到start_stream...${NC}"
START_STREAM=$(docker logs "$VEHICLE_CONTAINER" 2>&1 | grep -i "start_stream.*启用\|收到.*start_stream" | tail -3)
if [ -n "$START_STREAM" ]; then
    echo -e "${GREEN}✓${NC} 车端已收到start_stream"
    echo "$START_STREAM" | while read line; do echo "  $line"; done
else
    echo -e "${YELLOW}⊘${NC} 车端未收到start_stream（需要在客户端点击「连接车端」）"
fi
echo ""

# 3. 检查车端发布状态
echo -e "${CYAN}[3/6] 检查车端发布状态（最近10秒）...${NC}"
# 等待一下让数据发布
sleep 2
PUBLISH_LOG=$(docker logs "$VEHICLE_CONTAINER" 2>&1 | grep "CHASSIS_DATA.*发布\|状态发布未启用\|发布失败" | tail -5)
if [ -n "$PUBLISH_LOG" ]; then
    echo "$PUBLISH_LOG" | while read line; do
        if echo "$line" | grep -q "发布 #"; then
            echo -e "${GREEN}  ✓${NC} $line"
        elif echo "$line" | grep -q "状态发布未启用"; then
            echo -e "${YELLOW}  ⊘${NC} $line"
        elif echo "$line" | grep -q "发布失败\|失败"; then
            echo -e "${RED}  ✗${NC} $line"
        else
            echo "  $line"
        fi
    done
else
    echo -e "${YELLOW}⊘${NC} 未找到发布日志（可能代码未重新编译或容器未重启）"
    echo "  建议：重启车端容器以加载最新代码"
    echo "    docker compose restart vehicle"
fi
echo ""

# 4. 检查客户端MQTT连接
echo -e "${CYAN}[4/6] 检查客户端MQTT连接...${NC}"
CLIENT_MQTT=$(docker logs "$CLIENT_CONTAINER" 2>&1 | grep -i "mqtt.*连接\|MQTT connected\|Connecting to" | tail -5)
if [ -n "$CLIENT_MQTT" ]; then
    echo -e "${GREEN}✓${NC} 客户端MQTT连接日志"
    echo "$CLIENT_MQTT" | while read line; do echo "  $line"; done
else
    echo -e "${RED}✗${NC} 未找到客户端MQTT连接日志"
fi
echo ""

# 5. 检查客户端订阅
echo -e "${CYAN}[5/6] 检查客户端订阅状态...${NC}"
CLIENT_SUB=$(docker logs "$CLIENT_CONTAINER" 2>&1 | grep -i "subscribe\|订阅\|Subscribed" | tail -5)
if [ -n "$CLIENT_SUB" ]; then
    echo -e "${GREEN}✓${NC} 客户端订阅日志"
    echo "$CLIENT_SUB" | while read line; do echo "  $line"; done
else
    echo -e "${YELLOW}⊘${NC} 未找到客户端订阅日志"
fi
echo ""

# 6. 检查客户端接收数据
echo -e "${CYAN}[6/6] 检查客户端接收数据（最近10秒）...${NC}"
sleep 2
CLIENT_RECV=$(docker logs "$CLIENT_CONTAINER" 2>&1 | grep "CHASSIS_DATA.*接收\|消息回调\|VEHICLE_STATUS.*更新" | tail -5)
if [ -n "$CLIENT_RECV" ]; then
    echo -e "${GREEN}✓${NC} 客户端接收/更新日志"
    echo "$CLIENT_RECV" | while read line; do echo "  $line"; done
else
    echo -e "${YELLOW}⊘${NC} 未找到客户端接收日志"
fi
echo ""

# 总结和建议
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}验证总结${NC}"
echo -e "${CYAN}========================================${NC}"

if [ -z "$PUBLISH_LOG" ] || echo "$PUBLISH_LOG" | grep -q "状态发布未启用\|发布失败"; then
    echo ""
    echo -e "${YELLOW}建议操作：${NC}"
    echo "  1. 重启车端容器以加载最新代码："
    echo "     docker compose restart vehicle"
    echo "  2. 在客户端点击「连接车端」按钮"
    echo "  3. 等待5秒后重新运行此脚本验证"
    echo ""
fi

if [ -z "$CLIENT_RECV" ]; then
    echo ""
    echo -e "${YELLOW}客户端未接收数据，检查：${NC}"
    echo "  1. 客户端是否已点击「连接车端」"
    echo "  2. 查看完整MQTT日志："
    echo "     docker logs $CLIENT_CONTAINER | grep -i mqtt"
    echo ""
fi

echo "完整日志查看命令："
echo "  车端: docker logs $VEHICLE_CONTAINER | grep -i 'CHASSIS_DATA\|mqtt\|status'"
echo "  客户端: docker logs $CLIENT_CONTAINER | grep -i 'CHASSIS_DATA\|VEHICLE_STATUS\|mqtt\|status'"
