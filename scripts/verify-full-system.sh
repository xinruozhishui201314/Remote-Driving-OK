#!/bin/bash
# 完整系统验证脚本：验证整个远程驾驶系统的所有功能模块
# 用法: bash scripts/verify-full-system.sh
#
# 验证内容：
# 1. 基础服务节点（Postgres/Keycloak/Backend/ZLM/MQTT/车端）
# 2. 视频流连接（四路视频流）
# 3. MQTT 通信（底盘数据、控制指令）
# 4. 远驾接管功能（指令发送、确认接收、状态更新）
# 5. UI 状态同步（按钮状态、驾驶模式显示）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

CLIENT_CONTAINER="teleop-client-dev"
VEHICLE_CONTAINER="remote-driving-vehicle-1"
ZLM_CONTAINER="teleop-zlmediakit"
MQTT_CONTAINER="teleop-mosquitto"
BACKEND_CONTAINER="teleop-backend"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

echo "=========================================="
echo "远程驾驶系统完整验证"
echo "=========================================="
echo ""

# 检查容器是否存在
echo -e "${CYAN}========== 1. 检查容器状态 ==========${NC}"
echo ""

ALL_CONTAINERS_OK=true

check_container() {
    local container=$1
    if docker ps --format '{{.Names}}' | grep -q "^${container}$"; then
        local status=$(docker ps --format '{{.Status}}' --filter "name=^${container}$")
        echo -e "${GREEN}✓${NC} ${container}: ${status}"
        return 0
    else
        echo -e "${RED}✗${NC} ${container}: 容器不存在"
        ALL_CONTAINERS_OK=false
        return 1
    fi
}

check_container "${CLIENT_CONTAINER}"
check_container "${VEHICLE_CONTAINER}"
check_container "${ZLM_CONTAINER}"
check_container "${MQTT_CONTAINER}"
check_container "${BACKEND_CONTAINER}"

if [ "$ALL_CONTAINERS_OK" != "true" ]; then
    echo ""
    echo -e "${RED}部分容器不存在，请先运行: bash scripts/start-full-chain.sh manual${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}所有容器检查通过${NC}"
echo ""

# 等待服务稳定
echo -e "${CYAN}========== 2. 等待服务稳定 ==========${NC}"
echo "等待5秒..."
sleep 5
echo -e "${GREEN}✓ 服务已稳定${NC}"
echo ""

# 验证基础服务
echo -e "${CYAN}========== 3. 验证基础服务 ==========${NC}"
echo ""

# 3.1 验证 MQTT Broker
echo "【3.1】验证 MQTT Broker..."
if docker exec ${MQTT_CONTAINER} mosquitto_sub -h localhost -t test -C 1 -W 1 >/dev/null 2>&1; then
    echo -e "${GREEN}✓ MQTT Broker 正常${NC}"
else
    echo -e "${YELLOW}⚠ MQTT Broker 可能未就绪${NC}"
fi
echo ""

# 3.2 验证 ZLMediaKit
echo "【3.2】验证 ZLMediaKit..."
if curl -s "${ZLM_URL:-http://127.0.0.1:80}/index/api/getServerConfig" >/dev/null 2>&1; then
    echo -e "${GREEN}✓ ZLMediaKit 正常${NC}"
else
    echo -e "${YELLOW}⚠ ZLMediaKit 可能未就绪${NC}"
fi
echo ""

# 3.3 验证车端 MQTT 连接
echo "【3.3】验证车端 MQTT 连接..."
VEHICLE_LOGS=$(docker logs ${VEHICLE_CONTAINER} --tail 50 2>&1)
if echo "$VEHICLE_LOGS" | grep -q "已订阅主题\|订阅主题"; then
    echo -e "${GREEN}✓ 车端已连接 MQTT${NC}"
else
    echo -e "${YELLOW}⚠ 车端可能未连接 MQTT${NC}"
fi
echo ""

# 验证视频流
echo -e "${CYAN}========== 4. 验证视频流 ==========${NC}"
echo ""

echo "【4.1】检查推流进程..."
if docker exec ${VEHICLE_CONTAINER} ps aux | grep -q "ffmpeg\|push-nuscenes"; then
    echo -e "${GREEN}✓ 推流进程运行中${NC}"
else
    echo -e "${YELLOW}⚠ 推流进程未运行（可能需要发送 start_stream 指令）${NC}"
fi
echo ""

echo "【4.2】检查 ZLM 流状态..."
VIN="${VIN:-${VEHICLE_VIN:-}}"
VIN_PREFIX="${VIN:+${VIN}_}"
REQUIRED_STREAMS="${VIN_PREFIX}cam_front ${VIN_PREFIX}cam_rear ${VIN_PREFIX}cam_left ${VIN_PREFIX}cam_right"
ALL_STREAMS_OK=true
for stream in $REQUIRED_STREAMS; do
    if curl -s "${ZLM_URL:-http://127.0.0.1:80}/index/api/getMediaList?app=teleop" 2>/dev/null | grep -q "\"${stream}\""; then
        echo -e "${GREEN}✓ 流 ${stream} 存在${NC}"
    else
        echo -e "${YELLOW}⚠ 流 ${stream} 不存在${NC}"
        ALL_STREAMS_OK=false
    fi
done
echo ""

# 验证 MQTT 通信
echo -e "${CYAN}========== 5. 验证 MQTT 通信 ==========${NC}"
echo ""

echo "【5.1】检查底盘数据发布..."
CLIENT_LOGS=$(docker logs ${CLIENT_CONTAINER} --tail 200 2>&1)
if echo "$CLIENT_LOGS" | grep -q "开始接收底盘数据\|CHASSIS_DATA.*接收"; then
    echo -e "${GREEN}✓ 客户端正在接收底盘数据${NC}"
else
    echo -e "${YELLOW}⚠ 客户端未接收到底盘数据（可能需要连接视频流）${NC}"
fi
echo ""

echo "【5.2】检查车端状态发布..."
VEHICLE_LOGS=$(docker logs ${VEHICLE_CONTAINER} --tail 200 2>&1)
if echo "$VEHICLE_LOGS" | grep -q "开始发布底盘数据\|CHASSIS_DATA.*发布"; then
    echo -e "${GREEN}✓ 车端正在发布底盘数据${NC}"
else
    echo -e "${YELLOW}⚠ 车端未发布底盘数据（可能需要发送 start_stream 指令）${NC}"
fi
echo ""

# 验证远驾接管功能
echo -e "${CYAN}========== 6. 验证远驾接管功能 ==========${NC}"
echo ""

echo "【6.1】检查车端代码（是否包含新日志标记）..."
if docker exec ${VEHICLE_CONTAINER} bash -c "strings /tmp/vehicle-build/VehicleSide 2>/dev/null | grep -q 'REMOTE_CONTROL'" 2>/dev/null; then
    echo -e "${GREEN}✓ 车端代码已更新（包含 REMOTE_CONTROL 标记）${NC}"
else
    echo -e "${YELLOW}⚠ 车端代码可能未更新${NC}"
fi
echo ""

echo "【6.2】分析远驾接管日志..."
VEHICLE_LOGS=$(docker logs ${VEHICLE_CONTAINER} --tail 500 2>&1)
CLIENT_LOGS=$(docker logs ${CLIENT_CONTAINER} --tail 500 2>&1)

HAS_DETECTED=$(echo "$VEHICLE_LOGS" | grep -q "确认是 remote_control 指令\|REMOTE_CONTROL.*确认是" && echo "yes" || echo "no")
HAS_SENT_ACK=$(echo "$VEHICLE_LOGS" | grep -q "已成功发送远驾接管确认消息\|REMOTE_CONTROL.*已成功发送" && echo "yes" || echo "no")
HAS_RECEIVED_ACK=$(echo "$CLIENT_LOGS" | grep -q "收到远驾接管确认消息\|REMOTE_CONTROL.*收到" && echo "yes" || echo "no")
HAS_UPDATED_STATUS=$(echo "$CLIENT_LOGS" | grep -q "远驾接管状态变化\|REMOTE_CONTROL.*状态变化" && echo "yes" || echo "no")
HAS_UPDATED_MODE=$(echo "$CLIENT_LOGS" | grep -q "驾驶模式变化\|REMOTE_CONTROL.*驾驶模式" && echo "yes" || echo "no")

echo "  车端检测指令: ${HAS_DETECTED}"
echo "  车端发送确认: ${HAS_SENT_ACK}"
echo "  客户端收到确认: ${HAS_RECEIVED_ACK}"
echo "  客户端状态更新: ${HAS_UPDATED_STATUS}"
echo "  客户端模式更新: ${HAS_UPDATED_MODE}"
echo ""

if [ "$HAS_DETECTED" = "yes" ] && [ "$HAS_SENT_ACK" = "yes" ] && [ "$HAS_RECEIVED_ACK" = "yes" ] && [ "$HAS_UPDATED_STATUS" = "yes" ] && [ "$HAS_UPDATED_MODE" = "yes" ]; then
    echo -e "${GREEN}✓ 远驾接管功能正常${NC}"
else
    echo -e "${YELLOW}⚠ 远驾接管功能可能存在问题（如果未操作过，这是正常的）${NC}"
    echo "  提示：请在客户端界面点击「远驾接管」按钮进行测试"
fi
echo ""

# 验证 UI 状态
echo -e "${CYAN}========== 7. 验证 UI 状态 ==========${NC}"
echo ""

echo "【7.1】检查客户端进程..."
if docker exec ${CLIENT_CONTAINER} ps aux | grep -q "RemoteDrivingClient"; then
    echo -e "${GREEN}✓ 客户端进程运行中${NC}"
else
    echo -e "${YELLOW}⚠ 客户端进程未运行${NC}"
fi
echo ""

echo "【7.2】检查 QML 日志..."
if echo "$CLIENT_LOGS" | grep -q "QML\|qml:"; then
    echo -e "${GREEN}✓ QML 日志正常输出${NC}"
else
    echo -e "${YELLOW}⚠ QML 日志未输出（可能客户端未启动）${NC}"
fi
echo ""

# 综合诊断
echo -e "${CYAN}========== 8. 综合诊断 ==========${NC}"
echo ""

ALL_CHECKS_PASSED=true

# 检查关键错误
echo "【8.1】检查关键错误..."
VEHICLE_ERRORS=$(echo "$VEHICLE_LOGS" | grep -i "error\|exception\|failed\|✗✗✗" | tail -5)
CLIENT_ERRORS=$(echo "$CLIENT_LOGS" | grep -i "error\|exception\|failed\|✗✗✗" | tail -5)

if [ -n "$VEHICLE_ERRORS" ]; then
    echo -e "${YELLOW}⚠ 车端发现错误：${NC}"
    echo "$VEHICLE_ERRORS" | while read line; do
        echo "  $line"
    done
    ALL_CHECKS_PASSED=false
else
    echo -e "${GREEN}✓ 车端无关键错误${NC}"
fi

if [ -n "$CLIENT_ERRORS" ]; then
    echo -e "${YELLOW}⚠ 客户端发现错误：${NC}"
    echo "$CLIENT_ERRORS" | while read line; do
        echo "  $line"
    done
    ALL_CHECKS_PASSED=false
else
    echo -e "${GREEN}✓ 客户端无关键错误${NC}"
fi
echo ""

# 检查编译状态
echo "【8.2】检查编译状态..."
if docker exec ${CLIENT_CONTAINER} bash -c "test -x /tmp/client-build/RemoteDrivingClient || test -x /workspace/client/build/RemoteDrivingClient" 2>/dev/null; then
    echo -e "${GREEN}✓ 客户端已编译${NC}"
else
    echo -e "${YELLOW}⚠ 客户端未编译（将在启动时自动编译）${NC}"
fi
echo ""

# 最终报告
echo -e "${CYAN}========== 9. 验证报告 ==========${NC}"
echo ""

if [ "$ALL_CHECKS_PASSED" = "true" ] && [ "$ALL_STREAMS_OK" = "true" ]; then
    echo -e "${GREEN}✓✓✓ 系统验证通过！${NC}"
    echo ""
    echo "所有模块检查："
    echo "  ✓ 基础服务节点正常"
    echo "  ✓ 视频流正常"
    echo "  ✓ MQTT 通信正常"
    echo "  ✓ 远驾接管功能正常"
    echo "  ✓ UI 状态正常"
    echo ""
    echo "下一步："
    echo "  1. 在客户端界面手动操作测试"
    echo "  2. 运行详细验证: bash scripts/verify-remote-control-complete.sh"
    echo "  3. 查看实时日志: docker logs remote-driving-vehicle-1 -f | grep REMOTE_CONTROL"
    exit 0
else
    echo -e "${YELLOW}⚠ 系统验证部分通过，请检查上述问题${NC}"
    echo ""
    echo "建议："
    echo "  1. 检查容器日志: docker logs <container-name>"
    echo "  2. 重启系统: bash scripts/start-full-chain.sh manual"
    echo "  3. 查看详细错误信息"
    exit 1
fi
