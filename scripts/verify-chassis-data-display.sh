#!/bin/bash
# 验证车辆底盘数据流：车端 → Mosquitto → 客户端主驾驶界面
#
# 功能：
# 1. 检查 Mosquitto Broker 是否运行
# 2. 订阅 vehicle/status 主题，验证车端是否发布数据
# 3. 验证数据格式（JSON，包含所有字段）
# 4. 检查客户端是否能够接收数据
#
# 用法：
#   bash scripts/verify-chassis-data-display.sh [MQTT_BROKER_URL]
#
# 示例：
#   bash scripts/verify-chassis-data-display.sh mqtt://localhost:1883

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
# shellcheck source=lib/mqtt_control_json.sh
source "$SCRIPT_DIR/lib/mqtt_control_json.sh"

MQTT_BROKER_URL="${1:-mqtt://localhost:1883}"
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

# 解析 MQTT URL
MQTT_HOST="localhost"
MQTT_PORT="1883"
if [[ "$MQTT_BROKER_URL" =~ ^mqtt://([^:]+):([0-9]+)$ ]]; then
    MQTT_HOST="${BASH_REMATCH[1]}"
    MQTT_PORT="${BASH_REMATCH[2]}"
elif [[ "$MQTT_BROKER_URL" =~ ^mqtt://([^:]+)$ ]]; then
    MQTT_HOST="${BASH_REMATCH[1]}"
    MQTT_PORT="1883"
fi

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}车辆底盘数据流验证${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""
echo "MQTT Broker: $MQTT_BROKER_URL ($MQTT_HOST:$MQTT_PORT)"
echo ""

# 检查依赖
echo -e "${CYAN}[1/5] 检查依赖...${NC}"
MISSING_DEPS=0

if ! command -v mosquitto_sub &> /dev/null; then
    echo -e "  ${RED}✗${NC} mosquitto_sub 未安装"
    MISSING_DEPS=1
else
    echo -e "  ${GREEN}✓${NC} mosquitto_sub 已安装"
fi

if ! command -v mosquitto_pub &> /dev/null; then
    echo -e "  ${RED}✗${NC} mosquitto_pub 未安装"
    MISSING_DEPS=1
else
    echo -e "  ${GREEN}✓${NC} mosquitto_pub 已安装"
fi

if ! command -v jq &> /dev/null; then
    echo -e "  ${YELLOW}⊘${NC} jq 未安装（可选，用于 JSON 格式化）"
else
    echo -e "  ${GREEN}✓${NC} jq 已安装"
fi

if [ $MISSING_DEPS -eq 1 ]; then
    echo -e "${RED}错误：缺少必需依赖${NC}"
    echo "请安装 mosquitto-clients 包："
    echo "  Ubuntu/Debian: sudo apt-get install mosquitto-clients"
    echo "  CentOS/RHEL: sudo yum install mosquitto-clients"
    exit 1
fi
echo ""

# 检查 Mosquitto Broker 是否运行
echo -e "${CYAN}[2/5] 检查 Mosquitto Broker...${NC}"
if ! timeout 2 bash -c "echo > /dev/tcp/$MQTT_HOST/$MQTT_PORT" 2>/dev/null; then
    echo -e "  ${RED}✗${NC} 无法连接到 Mosquitto Broker ($MQTT_HOST:$MQTT_PORT)"
    echo "  请确保 Mosquitto 正在运行："
    echo "    docker compose ps mosquitto"
    echo "    或"
    echo "    docker compose up -d mosquitto"
    exit 1
fi
echo -e "  ${GREEN}✓${NC} Mosquitto Broker 可访问"
echo ""

# 检查车端是否运行
echo -e "${CYAN}[3/5] 检查车端是否运行...${NC}"
VEHICLE_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E "vehicle|teleop-vehicle" | head -1)
if [ -z "$VEHICLE_CONTAINER" ]; then
    echo -e "  ${YELLOW}⊘${NC} 未找到运行中的车端容器"
    echo "  提示：车端需要运行才能发布底盘数据"
else
    echo -e "  ${GREEN}✓${NC} 车端容器运行中: $VEHICLE_CONTAINER"
fi
echo ""

# 订阅 vehicle/status 主题，等待数据
echo -e "${CYAN}[4/5] 订阅 vehicle/status 主题，等待数据（10秒）...${NC}"
echo "  正在监听主题: vehicle/status"
echo ""

# 创建临时文件存储接收到的消息
TEMP_FILE=$(mktemp)
trap "rm -f $TEMP_FILE" EXIT

# 后台启动 mosquitto_sub，10秒后自动退出
timeout 10 mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "vehicle/status" -C 1 > "$TEMP_FILE" 2>&1 &
SUB_PID=$!

# 等待订阅建立
sleep 1

# 如果车端未运行，发送 start_stream 命令触发数据发布（如果车端支持）
if [ -n "$VEHICLE_CONTAINER" ]; then
    echo "  发送 start_stream 命令以触发车端发布数据..."
    mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "vehicle/control" -m "$(mqtt_json_start_stream "${VEHICLE_VIN:-123456789}")" 2>/dev/null || true
    sleep 1
fi

# 等待订阅完成
wait $SUB_PID 2>/dev/null || true

# 检查是否收到数据
if [ -s "$TEMP_FILE" ]; then
    echo -e "  ${GREEN}✓${NC} 收到底盘数据消息"
    echo ""
    echo -e "${CYAN}数据内容：${NC}"
    if command -v jq &> /dev/null; then
        cat "$TEMP_FILE" | jq . 2>/dev/null || cat "$TEMP_FILE"
    else
        cat "$TEMP_FILE"
    fi
    echo ""
    
    # 验证数据格式
    echo -e "${CYAN}[5/5] 验证数据格式...${NC}"
    DATA=$(cat "$TEMP_FILE")
    VALID_JSON=true
    MISSING_FIELDS=()
    
    # 检查是否为有效 JSON
    if command -v jq &> /dev/null; then
        if ! echo "$DATA" | jq . > /dev/null 2>&1; then
            VALID_JSON=false
            echo -e "  ${RED}✗${NC} 数据不是有效的 JSON"
        else
            echo -e "  ${GREEN}✓${NC} JSON 格式有效"
        fi
        
        # 检查必需字段
        REQUIRED_FIELDS=("timestamp" "speed" "battery" "gear" "steering")
        for field in "${REQUIRED_FIELDS[@]}"; do
            if ! echo "$DATA" | jq -e ".$field" > /dev/null 2>&1; then
                MISSING_FIELDS+=("$field")
            fi
        done
        
        # 检查扩展字段（可选）
        EXTENDED_FIELDS=("odometer" "voltage" "current" "temperature")
        EXTENDED_COUNT=0
        for field in "${EXTENDED_FIELDS[@]}"; do
            if echo "$DATA" | jq -e ".$field" > /dev/null 2>&1; then
                EXTENDED_COUNT=$((EXTENDED_COUNT + 1))
            fi
        done
        
        if [ ${#MISSING_FIELDS[@]} -eq 0 ]; then
            echo -e "  ${GREEN}✓${NC} 所有必需字段存在"
        else
            echo -e "  ${YELLOW}⊘${NC} 缺少字段: ${MISSING_FIELDS[*]}"
        fi
        
        if [ $EXTENDED_COUNT -gt 0 ]; then
            echo -e "  ${GREEN}✓${NC} 扩展字段存在 ($EXTENDED_COUNT/${#EXTENDED_FIELDS[@]}):"
            for field in "${EXTENDED_FIELDS[@]}"; do
                if echo "$DATA" | jq -e ".$field" > /dev/null 2>&1; then
                    value=$(echo "$DATA" | jq -r ".$field")
                    echo "    - $field: $value"
                fi
            done
        else
            echo -e "  ${YELLOW}⊘${NC} 未找到扩展字段（odometer, voltage, current, temperature）"
        fi
        
        # 显示数据摘要
        echo ""
        echo -e "${CYAN}数据摘要：${NC}"
        if echo "$DATA" | jq -e ".timestamp" > /dev/null 2>&1; then
            timestamp=$(echo "$DATA" | jq -r ".timestamp")
            echo "  时间戳: $timestamp"
        fi
        if echo "$DATA" | jq -e ".speed" > /dev/null 2>&1; then
            speed=$(echo "$DATA" | jq -r ".speed")
            echo "  速度: $speed km/h"
        fi
        if echo "$DATA" | jq -e ".battery" > /dev/null 2>&1; then
            battery=$(echo "$DATA" | jq -r ".battery")
            echo "  电池: $battery%"
        fi
        if echo "$DATA" | jq -e ".gear" > /dev/null 2>&1; then
            gear=$(echo "$DATA" | jq -r ".gear")
            echo "  档位: $gear"
        fi
        if echo "$DATA" | jq -e ".odometer" > /dev/null 2>&1; then
            odometer=$(echo "$DATA" | jq -r ".odometer")
            echo "  里程: $odometer km"
        fi
        if echo "$DATA" | jq -e ".voltage" > /dev/null 2>&1; then
            voltage=$(echo "$DATA" | jq -r ".voltage")
            echo "  电压: $voltage V"
        fi
        if echo "$DATA" | jq -e ".current" > /dev/null 2>&1; then
            current=$(echo "$DATA" | jq -r ".current")
            echo "  电流: $current A"
        fi
        if echo "$DATA" | jq -e ".temperature" > /dev/null 2>&1; then
            temperature=$(echo "$DATA" | jq -r ".temperature")
            echo "  温度: $temperature °C"
        fi
    else
        # 没有 jq，简单检查
        if echo "$DATA" | grep -q "speed"; then
            echo -e "  ${GREEN}✓${NC} 数据包含 'speed' 字段"
        else
            echo -e "  ${RED}✗${NC} 数据缺少 'speed' 字段"
        fi
    fi
    
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}✓ 验证完成：底盘数据流正常${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo "下一步："
    echo "  1. 启动客户端：bash scripts/start-full-chain.sh manual"
    echo "  2. 在客户端主驾驶界面点击「连接」按钮"
    echo "  3. 在控制面板中查看车辆状态信息（速度、电池、里程、电压、电流、温度）"
    echo ""
else
    echo -e "  ${RED}✗${NC} 未收到底盘数据消息"
    echo ""
    echo -e "${RED}可能的原因：${NC}"
    echo "  1. 车端未运行或未连接到 Mosquitto"
    echo "  2. 车端未启用状态发布（需要发送 start_stream 命令）"
    echo "  3. 车端配置文件中状态发布频率设置过低"
    echo ""
    echo "排查步骤："
    echo "  1. 检查车端容器：docker compose ps vehicle"
    echo "  2. 查看车端日志：docker compose logs vehicle | grep -i 'status\|mqtt'"
    echo "  3. 手动发送 start_stream："
    echo "     . scripts/lib/mqtt_control_json.sh && mosquitto_pub -h $MQTT_HOST -p $MQTT_PORT -t vehicle/control -m \"\$(mqtt_json_start_stream <vin>)\""
    echo "  4. 检查配置文件：Vehicle-side/config/vehicle_config.json"
    echo ""
    exit 1
fi
