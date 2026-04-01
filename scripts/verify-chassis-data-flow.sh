#!/bin/bash
# 验证车端底盘数据流：车端 → MQTT → 客户端主驾驶界面
# 
# 验证步骤：
# 1. 检查MQTT Broker是否运行
# 2. 启动车端（模拟发布底盘数据）
# 3. 启动客户端并连接
# 4. 验证数据是否正确接收和显示
#
# 用法：
#   bash scripts/verify-chassis-data-flow.sh [mqtt_broker_url] [zlm_url]
#
# 示例：
#   bash scripts/verify-chassis-data-flow.sh mqtt://localhost:1883 http://localhost:8080

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 参数
MQTT_BROKER="${1:-mqtt://localhost:1883}"
ZLM_URL="${2:-http://localhost:8080}"
VEHICLE_VIN="${3:-123456789}"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}车端底盘数据流验证脚本${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "MQTT Broker: $MQTT_BROKER"
echo "ZLMediaKit URL: $ZLM_URL"
echo "车辆 VIN: $VEHICLE_VIN"
echo ""

# 检查依赖
check_dependencies() {
    echo -e "${YELLOW}[1/6] 检查依赖...${NC}"
    
    local missing=0
    
    # 检查 mosquitto_sub
    if ! command -v mosquitto_sub &> /dev/null; then
        echo -e "${RED}  ✗ mosquitto_sub 未安装（用于订阅MQTT消息）${NC}"
        missing=1
    else
        echo -e "${GREEN}  ✓ mosquitto_sub 已安装${NC}"
    fi
    
    # 检查 jq（可选，用于JSON格式化）
    if ! command -v jq &> /dev/null; then
        echo -e "${YELLOW}  ⚠ jq 未安装（可选，用于JSON格式化）${NC}"
    else
        echo -e "${GREEN}  ✓ jq 已安装${NC}"
    fi
    
    # 检查 curl
    if ! command -v curl &> /dev/null; then
        echo -e "${RED}  ✗ curl 未安装${NC}"
        missing=1
    else
        echo -e "${GREEN}  ✓ curl 已安装${NC}"
    fi
    
    if [ $missing -eq 1 ]; then
        echo -e "${RED}请安装缺失的依赖后重试${NC}"
        exit 1
    fi
    
    echo ""
}

# 检查MQTT Broker
check_mqtt_broker() {
    echo -e "${YELLOW}[2/6] 检查MQTT Broker连接...${NC}"
    
    # 解析MQTT URL
    local host=$(echo "$MQTT_BROKER" | sed 's|mqtt://||' | cut -d: -f1)
    local port=$(echo "$MQTT_BROKER" | sed 's|mqtt://||' | cut -d: -f2)
    port=${port:-1883}
    
    # 尝试连接
    if timeout 2 bash -c "echo > /dev/tcp/$host/$port" 2>/dev/null; then
        echo -e "${GREEN}  ✓ MQTT Broker ($host:$port) 可连接${NC}"
    else
        echo -e "${RED}  ✗ 无法连接到 MQTT Broker ($host:$port)${NC}"
        echo -e "${YELLOW}  提示: 请确保MQTT Broker正在运行${NC}"
        echo -e "${YELLOW}  启动示例: docker run -it -p 1883:1883 eclipse-mosquitto${NC}"
        exit 1
    fi
    
    echo ""
}

# 检查ZLMediaKit
check_zlm() {
    echo -e "${YELLOW}[3/6] 检查ZLMediaKit连接...${NC}"
    
    local zlm_host=$(echo "$ZLM_URL" | sed 's|http://||' | sed 's|https://||' | cut -d: -f1)
    local zlm_port=$(echo "$ZLM_URL" | sed 's|http://||' | sed 's|https://||' | cut -d: -f2 | cut -d/ -f1)
    zlm_port=${zlm_port:-8080}
    
    if curl -s --max-time 2 "$ZLM_URL/index/api/getServerConfig" > /dev/null 2>&1; then
        echo -e "${GREEN}  ✓ ZLMediaKit ($zlm_host:$zlm_port) 可连接${NC}"
    else
        echo -e "${YELLOW}  ⚠ 无法连接到 ZLMediaKit ($zlm_host:$zlm_port)${NC}"
        echo -e "${YELLOW}  提示: 视频流功能可能不可用，但MQTT数据流仍可验证${NC}"
    fi
    
    echo ""
}

# 启动MQTT订阅监听（后台）
start_mqtt_listener() {
    echo -e "${YELLOW}[4/6] 启动MQTT消息监听...${NC}"
    
    local log_file="/tmp/chassis_data_mqtt_$(date +%s).log"
    
    # 后台订阅并记录
    (
        mosquitto_sub -h "$(echo "$MQTT_BROKER" | sed 's|mqtt://||' | cut -d: -f1)" \
                      -p "$(echo "$MQTT_BROKER" | sed 's|mqtt://||' | cut -d: -f2 | cut -d/ -f1)" \
                      -t "vehicle/status" \
                      -t "vehicle/+/status" \
                      -v 2>&1 | tee "$log_file"
    ) &
    
    local listener_pid=$!
    echo "$listener_pid" > /tmp/mqtt_listener_pid.txt
    echo "$log_file" > /tmp/mqtt_listener_log.txt
    
    echo -e "${GREEN}  ✓ MQTT监听已启动 (PID: $listener_pid)${NC}"
    echo -e "${BLUE}  日志文件: $log_file${NC}"
    echo ""
    
    # 等待一下确保订阅成功
    sleep 2
}

# 停止MQTT监听
stop_mqtt_listener() {
    if [ -f /tmp/mqtt_listener_pid.txt ]; then
        local pid=$(cat /tmp/mqtt_listener_pid.txt)
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            echo -e "${GREEN}  ✓ MQTT监听已停止${NC}"
        fi
        rm -f /tmp/mqtt_listener_pid.txt
    fi
}

# 模拟车端发布数据（用于测试）
simulate_vehicle_publish() {
    echo -e "${YELLOW}[5/6] 模拟车端发布底盘数据...${NC}"
    
    local host=$(echo "$MQTT_BROKER" | sed 's|mqtt://||' | cut -d: -f1)
    local port=$(echo "$MQTT_BROKER" | sed 's|mqtt://||' | cut -d: -f2 | cut -d/ -f1)
    port=${port:-1883}
    
    # 发布测试数据
    local test_data='{"timestamp":'$(date +%s000)',"vin":"'$VEHICLE_VIN'","speed":25.5,"gear":1,"steering":0.2,"throttle":0.3,"brake":0.0,"battery":95.5,"odometer":1234.56,"voltage":48.2,"current":15.3,"temperature":28.5}'
    
    mosquitto_pub -h "$host" -p "$port" -t "vehicle/status" -m "$test_data"
    mosquitto_pub -h "$host" -p "$port" -t "vehicle/$VEHICLE_VIN/status" -m "$test_data"
    
    echo -e "${GREEN}  ✓ 测试数据已发布${NC}"
    echo -e "${BLUE}  主题: vehicle/status, vehicle/$VEHICLE_VIN/status${NC}"
    echo -e "${BLUE}  数据: $test_data${NC}"
    echo ""
}

# 验证数据格式
verify_data_format() {
    echo -e "${YELLOW}[6/6] 验证数据格式...${NC}"
    
    if [ ! -f /tmp/mqtt_listener_log.txt ]; then
        echo -e "${RED}  ✗ 未找到MQTT监听日志${NC}"
        return 1
    fi
    
    local log_file=$(cat /tmp/mqtt_listener_log.txt)
    
    if [ ! -f "$log_file" ]; then
        echo -e "${RED}  ✗ 日志文件不存在${NC}"
        return 1
    fi
    
    # 等待数据
    sleep 3
    
    # 检查是否有数据
    local line_count=$(wc -l < "$log_file" 2>/dev/null || echo "0")
    
    if [ "$line_count" -eq 0 ]; then
        echo -e "${RED}  ✗ 未收到任何MQTT消息${NC}"
        echo -e "${YELLOW}  提示: 请确保车端正在运行并发布数据${NC}"
        return 1
    fi
    
    echo -e "${GREEN}  ✓ 收到 $line_count 条消息${NC}"
    
    # 检查最后一条消息的格式
    local last_line=$(tail -1 "$log_file" 2>/dev/null || echo "")
    
    if echo "$last_line" | grep -q "vehicle/.*/status"; then
        local topic=$(echo "$last_line" | cut -d' ' -f1)
        local payload=$(echo "$last_line" | cut -d' ' -f2-)
        
        echo -e "${BLUE}  最后一条消息:${NC}"
        echo -e "${BLUE}  主题: $topic${NC}"
        
        # 尝试解析JSON
        if command -v jq &> /dev/null; then
            if echo "$payload" | jq . > /dev/null 2>&1; then
                echo -e "${GREEN}  ✓ JSON格式正确${NC}"
                echo -e "${BLUE}  内容:${NC}"
                echo "$payload" | jq .
                
                # 检查必需字段
                local required_fields=("timestamp" "speed" "gear" "battery")
                local missing_fields=()
                
                for field in "${required_fields[@]}"; do
                    if ! echo "$payload" | jq -e ".$field" > /dev/null 2>&1; then
                        missing_fields+=("$field")
                    fi
                done
                
                if [ ${#missing_fields[@]} -eq 0 ]; then
                    echo -e "${GREEN}  ✓ 所有必需字段都存在${NC}"
                else
                    echo -e "${YELLOW}  ⚠ 缺失字段: ${missing_fields[*]}${NC}"
                fi
            else
                echo -e "${RED}  ✗ JSON格式错误${NC}"
                echo -e "${BLUE}  原始内容: $payload${NC}"
                return 1
            fi
        else
            echo -e "${BLUE}  内容: $payload${NC}"
            echo -e "${YELLOW}  ⚠ 未安装jq，无法验证JSON格式${NC}"
        fi
    else
        echo -e "${YELLOW}  ⚠ 未找到有效的状态消息${NC}"
    fi
    
    echo ""
}

# 清理函数
cleanup() {
    echo ""
    echo -e "${YELLOW}清理资源...${NC}"
    stop_mqtt_listener
    rm -f /tmp/mqtt_listener_*.txt
}

# 注册清理函数
trap cleanup EXIT INT TERM

# 主流程
main() {
    check_dependencies
    check_mqtt_broker
    check_zlm
    start_mqtt_listener
    simulate_vehicle_publish
    verify_data_format
    
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}验证完成${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo -e "${BLUE}下一步验证:${NC}"
    echo "1. 启动车端: cd Vehicle-side && ./run.sh $MQTT_BROKER"
    echo "2. 启动客户端并连接"
    echo "3. 在主驾驶界面观察底盘数据是否实时更新"
    echo ""
    echo -e "${BLUE}MQTT监听将继续运行，按 Ctrl+C 停止${NC}"
    echo ""
    
    # 保持运行，显示实时消息
    if [ -f /tmp/mqtt_listener_log.txt ]; then
        local log_file=$(cat /tmp/mqtt_listener_log.txt)
        echo -e "${BLUE}实时消息流:${NC}"
        tail -f "$log_file" 2>/dev/null || sleep 10
    else
        sleep 10
    fi
}

# 运行主流程
main "$@"
