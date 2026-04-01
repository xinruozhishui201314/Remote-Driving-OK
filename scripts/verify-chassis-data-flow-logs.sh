#!/bin/bash
# 验证车辆底盘数据流日志分析脚本
#
# 功能：
# 1. 检查车端是否收到 start_stream 命令
# 2. 检查车端是否开始发布底盘数据
# 3. 检查客户端是否订阅了 vehicle/status 主题
# 4. 检查客户端是否接收到底盘数据
# 5. 检查客户端是否更新了车辆状态
#
# 用法：
#   bash scripts/verify-chassis-data-flow-logs.sh

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
echo -e "${CYAN}车辆底盘数据流日志分析${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# 查找容器名称
VEHICLE_CONTAINER=$(docker ps --format '{{.Names}}' | grep -i vehicle | head -1)
CLIENT_CONTAINER=$(docker ps --format '{{.Names}}' | grep -i "client-dev\|client" | head -1)

if [ -z "$VEHICLE_CONTAINER" ]; then
    echo -e "${RED}✗${NC} 未找到车端容器"
    echo "  请先启动全链路: bash scripts/start-full-chain.sh manual"
    exit 1
fi

if [ -z "$CLIENT_CONTAINER" ]; then
    echo -e "${RED}✗${NC} 未找到客户端容器"
    echo "  请先启动全链路: bash scripts/start-full-chain.sh manual"
    exit 1
fi

echo -e "${GREEN}✓${NC} 找到车端容器: $VEHICLE_CONTAINER"
echo -e "${GREEN}✓${NC} 找到客户端容器: $CLIENT_CONTAINER"
echo ""

# 1. 检查车端是否收到 start_stream
echo -e "${CYAN}[1/5] 检查车端是否收到 start_stream 命令...${NC}"
VEHICLE_START_STREAM=$(docker logs "$VEHICLE_CONTAINER" 2>&1 | grep -i "start_stream\|收到.*MQTT.*消息.*control" | tail -5)
if [ -n "$VEHICLE_START_STREAM" ]; then
    echo -e "${GREEN}✓${NC} 车端已收到 start_stream 命令"
    echo "$VEHICLE_START_STREAM" | while read line; do
        echo "  $line"
    done
else
    echo -e "${RED}✗${NC} 车端未收到 start_stream 命令"
    echo "  提示：在客户端点击「连接车端」按钮会自动发送 start_stream"
fi
echo ""

# 2. 检查车端是否启用底盘数据发布
echo -e "${CYAN}[2/5] 检查车端是否启用底盘数据发布...${NC}"
VEHICLE_ENABLED=$(docker logs "$VEHICLE_CONTAINER" 2>&1 | grep -i "CHASSIS_DATA.*启用\|底盘数据发布已启用" | tail -3)
if [ -n "$VEHICLE_ENABLED" ]; then
    echo -e "${GREEN}✓${NC} 车端已启用底盘数据发布"
    echo "$VEHICLE_ENABLED" | while read line; do
        echo "  $line"
    done
else
    echo -e "${YELLOW}⊘${NC} 未找到启用日志（可能还未启用）"
fi
echo ""

# 3. 检查车端是否发布数据
echo -e "${CYAN}[3/5] 检查车端是否发布底盘数据...${NC}"
VEHICLE_PUBLISH=$(docker logs "$VEHICLE_CONTAINER" 2>&1 | grep "CHASSIS_DATA.*发布" | tail -5)
if [ -n "$VEHICLE_PUBLISH" ]; then
    echo -e "${GREEN}✓${NC} 车端正在发布底盘数据"
    echo "$VEHICLE_PUBLISH" | while read line; do
        echo "  $line"
    done
    
    # 提取发布频率
    FREQ=$(echo "$VEHICLE_PUBLISH" | grep -oP '实际频率: [0-9.]+' | tail -1 | grep -oP '[0-9.]+' || echo "")
    if [ -n "$FREQ" ]; then
        echo "  实际发布频率: ${FREQ} Hz"
    fi
else
    echo -e "${RED}✗${NC} 车端未发布底盘数据"
    echo "  可能原因："
    echo "    1. 未收到 start_stream 命令"
    echo "    2. MQTT 连接断开"
    echo "    3. 发布功能未启用"
fi
echo ""

# 4. 检查客户端是否订阅了 vehicle/status
echo -e "${CYAN}[4/5] 检查客户端是否订阅了 vehicle/status 主题...${NC}"
CLIENT_SUBSCRIBE=$(docker logs "$CLIENT_CONTAINER" 2>&1 | grep -i "subscribe\|订阅" | grep -i "vehicle.*status\|status.*topic" | tail -5)
if [ -n "$CLIENT_SUBSCRIBE" ]; then
    echo -e "${GREEN}✓${NC} 客户端已订阅 vehicle/status 主题"
    echo "$CLIENT_SUBSCRIBE" | while read line; do
        echo "  $line"
    done
else
    echo -e "${RED}✗${NC} 客户端未订阅 vehicle/status 主题"
    echo "  检查MQTT连接日志："
    docker logs "$CLIENT_CONTAINER" 2>&1 | grep -i "mqtt.*连接\|mqtt.*connect" | tail -3
fi
echo ""

# 5. 检查客户端是否接收到数据
echo -e "${CYAN}[5/5] 检查客户端是否接收到底盘数据...${NC}"
CLIENT_RECEIVE=$(docker logs "$CLIENT_CONTAINER" 2>&1 | grep "CHASSIS_DATA.*接收" | tail -5)
if [ -n "$CLIENT_RECEIVE" ]; then
    echo -e "${GREEN}✓${NC} 客户端正在接收底盘数据"
    echo "$CLIENT_RECEIVE" | while read line; do
        echo "  $line"
    done
    
    # 提取接收频率
    FREQ=$(echo "$CLIENT_RECEIVE" | grep -oP '实际频率:[0-9.]+' | tail -1 | grep -oP '[0-9.]+' || echo "")
    if [ -n "$FREQ" ]; then
        echo "  实际接收频率: ${FREQ} Hz"
    fi
else
    echo -e "${RED}✗${NC} 客户端未接收到底盘数据"
    echo "  检查消息回调日志："
    docker logs "$CLIENT_CONTAINER" 2>&1 | grep -i "消息回调\|message.*callback" | tail -3
fi
echo ""

# 6. 检查客户端状态更新
echo -e "${CYAN}[6/6] 检查客户端是否更新车辆状态...${NC}"
CLIENT_UPDATE=$(docker logs "$CLIENT_CONTAINER" 2>&1 | grep "VEHICLE_STATUS.*更新" | tail -5)
if [ -n "$CLIENT_UPDATE" ]; then
    echo -e "${GREEN}✓${NC} 客户端正在更新车辆状态"
    echo "$CLIENT_UPDATE" | while read line; do
        echo "  $line"
    done
else
    echo -e "${YELLOW}⊘${NC} 未找到状态更新日志（可能数据未变化或日志频率控制）"
fi
echo ""

# 总结
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}数据流分析总结${NC}"
echo -e "${CYAN}========================================${NC}"

ALL_OK=1

if [ -z "$VEHICLE_START_STREAM" ]; then
    echo -e "${RED}✗${NC} 车端未收到 start_stream 命令"
    ALL_OK=0
fi

if [ -z "$VEHICLE_PUBLISH" ]; then
    echo -e "${RED}✗${NC} 车端未发布底盘数据"
    ALL_OK=0
fi

if [ -z "$CLIENT_SUBSCRIBE" ]; then
    echo -e "${RED}✗${NC} 客户端未订阅 vehicle/status 主题"
    ALL_OK=0
fi

if [ -z "$CLIENT_RECEIVE" ]; then
    echo -e "${RED}✗${NC} 客户端未接收到底盘数据"
    ALL_OK=0
fi

if [ $ALL_OK -eq 1 ]; then
    echo ""
    echo -e "${GREEN}✓ 数据流正常：车端 → MQTT → 客户端${NC}"
    echo ""
    echo "下一步：在客户端界面查看右侧控制面板的「📊 车辆状态」区域"
else
    echo ""
    echo -e "${YELLOW}部分环节异常，请检查上述输出${NC}"
    echo ""
    echo "排查建议："
    echo "  1. 确认客户端已点击「连接车端」按钮"
    echo "  2. 查看完整日志："
    echo "     docker logs $VEHICLE_CONTAINER | grep -i 'mqtt\|chassis\|status'"
    echo "     docker logs $CLIENT_CONTAINER | grep -i 'mqtt\|chassis\|status'"
    echo "  3. 检查 MQTT Broker："
    echo "     docker compose logs mosquitto | tail -20"
fi
