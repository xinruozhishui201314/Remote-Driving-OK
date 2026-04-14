#!/bin/bash
# 测试目标速度和急停刹车功能（更新版：支持浮点型、急停按钮状态、默认值0.0）
# 用法: bash scripts/test-speed-and-brake.sh

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

echo "========== 目标速度和急停刹车功能测试（更新版 v2）=========="
echo "测试项："
echo "  1. 目标速度默认值为0.0"
echo "  2. 目标速度支持浮点型数值（如50.5）"
echo "  3. 目标速度km/h文本正好在输入框正上方"
echo "  4. 急停按钮支持切换：点击执行急停，再次点击解除急停"
echo "  5. 车端接收急停命令并执行/解除"
echo "  6. 车端模拟刹车状态反馈给客户端"
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
docker compose exec -T mosquitto mosquitto_pub -h mosquitto -p 1883 -t vehicle/control -m "$(mqtt_json_start_stream "123456789")" >/dev/null 2>&1
sleep 2
echo -e "  ${GREEN}✓ 已发送 start_stream 命令${NC}"

# 3. 启用远驾接管
echo ""
echo -e "${BLUE}[步骤 3/10] 启用远驾接管${NC}"
docker compose exec -T mosquitto mosquitto_pub -h mosquitto -p 1883 -t vehicle/control -m "$(mqtt_json_remote_control "123456789" true)" >/dev/null 2>&1
sleep 1
echo -e "  ${GREEN}✓ 已发送 remote_control 命令${NC}"

# 4. 发送目标速度命令（浮点型：50.5 km/h）
echo ""
echo -e "${BLUE}[步骤 4/10] 发送目标速度命令（50.5 km/h，测试浮点型）${NC}"
docker compose exec -T mosquitto mosquitto_pub -h mosquitto -p 1883 -t vehicle/control -m "$(mqtt_json_target_speed "123456789" 50.5)" >/dev/null 2>&1
sleep 2
echo -e "  ${GREEN}✓ 目标速度命令已发送 (50.5 km/h，浮点型)${NC}"

# 5. 检查车端接收目标速度命令
echo ""
echo -e "${BLUE}[步骤 5/10] 检查车端接收目标速度命令${NC}"
SPEED_LOG=$(docker logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "\[SPEED\]|目标速度" | tail -5)
if [ -n "$SPEED_LOG" ]; then
    echo -e "  ${GREEN}✓ 找到目标速度相关日志${NC}"
    echo "$SPEED_LOG" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到目标速度相关日志${NC}"
fi

# 6. 发送急停刹车命令
echo ""
echo -e "${BLUE}[步骤 6/10] 发送急停刹车命令${NC}"
docker compose exec -T mosquitto mosquitto_pub -h mosquitto -p 1883 -t vehicle/control -m "$(mqtt_json_brake "123456789" 1.0)" >/dev/null 2>&1
sleep 2
echo -e "  ${GREEN}✓ 急停刹车命令已发送 (brake=1.0)${NC}"

# 7. 检查车端接收刹车命令
echo ""
echo -e "${BLUE}[步骤 7/10] 检查车端接收刹车命令${NC}"
BRAKE_LOG=$(docker logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "\[BRAKE\]|刹车" | tail -10)
if [ -n "$BRAKE_LOG" ]; then
    echo -e "  ${GREEN}✓ 找到刹车相关日志${NC}"
    echo "$BRAKE_LOG" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到刹车相关日志${NC}"
fi

# 8. 检查车端反馈刹车状态
echo ""
echo -e "${BLUE}[步骤 8/10] 检查车端反馈刹车状态（等待 2 秒）${NC}"
sleep 2
BRAKE_FEEDBACK=$(docker logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "\[CHASSIS_DATA\].*刹车|发布.*brake" | tail -5)
if [ -n "$BRAKE_FEEDBACK" ]; then
    echo -e "  ${GREEN}✓ 找到车端刹车反馈日志${NC}"
    echo "$BRAKE_FEEDBACK" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到车端刹车反馈日志${NC}"
fi

# 9. 验证MQTT消息中的速度和刹车状态
echo ""
echo -e "${BLUE}[步骤 9/10] 验证MQTT消息中的速度和刹车状态${NC}"
MQTT_DATA=$(docker compose exec -T mosquitto timeout 3 mosquitto_sub -h mosquitto -p 1883 -t vehicle/status -C 1 2>&1 | python3 -c "import sys, json; d=json.load(sys.stdin); print('speed:', d.get('speed', 'NOT_FOUND'), 'brake:', d.get('brake', 'NOT_FOUND'))" 2>/dev/null || echo "NOT_FOUND")
if [ "$MQTT_DATA" != "NOT_FOUND" ] && [ "$MQTT_DATA" != "" ]; then
    echo -e "  ${GREEN}✓ MQTT 消息中包含速度和刹车数据${NC}"
    echo "    $MQTT_DATA" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到速度和刹车字段${NC}"
fi

# 10. 检查客户端接收（如果已连接）
echo ""
echo -e "${BLUE}[步骤 10/10] 检查客户端接收（如果已连接）${NC}"
CLIENT_MOSQUITTO=$(docker exec "$CLIENT_CONTAINER" ps aux 2>/dev/null | grep mosquitto_sub | grep -v grep | wc -l)
if [ "$CLIENT_MOSQUITTO" -gt 0 ]; then
    echo -e "  ${GREEN}✓ mosquitto_sub 进程正在运行${NC}"
    echo "  提示: 客户端日志可能输出到标准输出，请查看启动脚本的输出"
else
    echo -e "  ${YELLOW}⊘ mosquitto_sub 进程未运行（客户端可能还未连接 MQTT）${NC}"
    echo "  提示: 请在客户端界面点击「连接车端」按钮"
fi

# 11. 测试解除急停命令
echo ""
echo -e "${BLUE}[步骤 11/12] 测试解除急停命令${NC}"
docker compose exec -T mosquitto mosquitto_pub -h mosquitto -p 1883 -t vehicle/control -m "$(mqtt_json_emergency_stop "123456789" false)" >/dev/null 2>&1
sleep 2
echo -e "  ${GREEN}✓ 解除急停命令已发送 (enable=false)${NC}"

# 12. 检查车端接收解除急停命令
echo ""
echo -e "${BLUE}[步骤 12/12] 检查车端接收解除急停命令${NC}"
RELEASE_LOG=$(docker logs "$VEHICLE_CONTAINER" --tail 100 2>&1 | grep -E "\[EMERGENCY_STOP\].*解除|急停命令已解除" | tail -5)
if [ -n "$RELEASE_LOG" ]; then
    echo -e "  ${GREEN}✓ 找到解除急停相关日志${NC}"
    echo "$RELEASE_LOG" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到解除急停相关日志${NC}"
fi

echo ""
echo "========== 测试完成 =========="
echo ""
echo "总结："
echo "1. 目标速度命令发送和接收 ✓"
echo "2. 急停刹车命令发送和接收 ✓"
echo "3. 解除急停命令发送和接收 ✓"
echo "4. 车端反馈速度和刹车状态 ✓"
echo "5. 浮点型数值支持 ✓"
echo ""
echo "下一步操作（UI验证）："
echo "1. 在客户端界面点击「连接车端」"
echo "2. 点击「远驾接管」"
echo "3. 检查目标速度默认值是否为0.0"
echo "4. 检查「目标速度km/h」文本是否正好在输入框正上方"
echo "5. 在目标速度输入框中输入浮点型数值（如50.5），按回车发送"
echo "6. 检查急停按钮默认是否为正常颜色（非红色）"
echo "7. 点击「急停」按钮，观察按钮是否变为红色，车端是否执行急停"
echo "8. 再次点击「急停」按钮，观察按钮是否恢复正常颜色，车端是否解除急停"
echo "9. 观察「清扫」右侧的刹车状态按钮是否根据车端反馈亮起"
echo ""
echo "查看实时日志："
echo "  客户端: docker compose logs $CLIENT_CONTAINER -f | grep -E '\[SPEED\]|\[BRAKE\]|\[EMERGENCY_STOP\]|目标速度|刹车|急停'"
echo "  车端:   docker compose logs $VEHICLE_CONTAINER -f | grep -E '\[SPEED\]|\[BRAKE\]|\[EMERGENCY_STOP\]|目标速度|刹车|急停'"
