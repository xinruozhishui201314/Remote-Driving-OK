#!/bin/bash
# 全链路 E2E 启动：涉及到的所有节点均启动，便于验证整条链路
#
# 链路：客户端 → 鉴权(Keycloak)/后端 → 选车/会话 → MQTT(start_stream) → 车端 → 推流 → ZLM → 客户端拉流
# 依赖：Postgres → Keycloak/Backend；Coturn → ZLM；MQTT → Vehicle；以上 + client-dev 全部就绪后再启动客户端
#
# 用法：
#   bash scripts/run-e2e.sh start    # 启动所有节点并自动拉起客户端（便于界面操作验证）
#   bash scripts/run-e2e.sh start-no-client  # 仅启动所有节点，不启动客户端
#   bash scripts/run-e2e.sh start-and-verify # 启动全链路 + 逐环体验证 + 启动客户端（推荐）
#   bash scripts/run-e2e.sh stop     # 停止
#   bash scripts/run-e2e.sh client   # 仅启动客户端（前提：其余节点已 start）
#   bash scripts/run-e2e.sh status   # 查看各节点状态

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml"
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

# 全链路涉及的所有节点（按依赖顺序）
REQUIRED_SERVICES="postgres keycloak coturn zlmediakit backend mosquitto vehicle client-dev"

check_all_nodes() {
    echo -e "${CYAN}校验全链路节点是否均在运行：${NC}"
    local failed=0
    for name in teleop-postgres teleop-keycloak teleop-coturn teleop-zlmediakit teleop-backend teleop-mosquitto teleop-client-dev; do
        if docker ps --format '{{.Names}}' | grep -q "^${name}$"; then
            if docker ps --format '{{.Status}}' --filter "name=^${name}$" | grep -qi "up"; then
                echo -e "  ${GREEN}✓${NC} $name"
            else
                echo -e "  ${RED}✗${NC} $name (未运行)"
                failed=1
            fi
        else
            echo -e "  ${RED}✗${NC} $name (未找到)"
            failed=1
        fi
    done
    if docker ps --format '{{.Names}}' | grep -q "vehicle"; then
        local vname=$(docker ps --format '{{.Names}}' | grep "vehicle" | head -1)
        if docker ps --format '{{.Status}}' --filter "name=${vname}" | grep -qi "up"; then
            echo -e "  ${GREEN}✓${NC} $vname (车端)"
        else
            echo -e "  ${RED}✗${NC} vehicle (未运行)"
            failed=1
        fi
    else
        echo -e "  ${RED}✗${NC} vehicle (未找到)"
        failed=1
    fi
    return $failed
}

start_all() {
    echo -e "${CYAN}=== 全链路 E2E：启动涉及到的所有节点 ===${NC}"
    echo "  若车端镜像有更新（如首次或修改过 Vehicle-side/Dockerfile.dev），请先执行:"
    echo "  ${YELLOW}docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml build vehicle${NC}"
    echo ""
    echo "  1) postgres     - 数据库（Keycloak/Backend）"
    echo "  2) keycloak    - 鉴权"
    echo "  3) coturn      - STUN/TURN（ZLM WebRTC）"
    echo "  4) zlmediakit  - 流媒体"
    echo "  5) backend     - 业务后端（会话/车辆列表）"
    echo "  6) mosquitto   - MQTT Broker"
    echo "  7) vehicle     - 车端（收 MQTT 推流到 ZLM）"
    echo "  8) client-dev  - 客户端开发环境"
    echo ""

    $COMPOSE up -d postgres keycloak coturn
    echo -e "${YELLOW}等待 Postgres/Keycloak 就绪...${NC}"
    sleep 5

    $COMPOSE up -d zlmediakit
    sleep 2
    $COMPOSE up -d backend mosquitto
    sleep 2
    $COMPOSE up -d vehicle client-dev

    echo ""
    if ! check_all_nodes; then
        echo -e "${RED}部分节点未就绪，请检查: docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml ps${NC}"
        return 1
    fi
    echo -e "${GREEN}所有节点已启动并运行。${NC}"
    echo ""
    echo "  后端:     http://127.0.0.1:8081  (鉴权/会话)"
    echo "  ZLM:      http://127.0.0.1:80   (流媒体)"
    echo "  MQTT:     127.0.0.1:1883"
    echo "  车端:     已连 MQTT，收到 start_stream 后执行推流脚本"
    echo ""
    echo -e "${CYAN}客户端（在容器内）使用: ZLM_VIDEO_URL=http://zlmediakit:80, MQTT=mqtt://mosquitto:1883${NC}"
    echo ""
}

start_all_and_client() {
    start_all
    echo -e "${YELLOW}等待后端就绪（鉴权/会话可用）...${NC}"
    for i in 1 2 3 4 5 6 7 8 9 10; do
        if curl -sf -o /dev/null "http://127.0.0.1:8081/health" 2>/dev/null; then
            echo -e "${GREEN}后端已就绪。${NC}"
            break
        fi
        sleep 2
    done
    echo -e "${YELLOW}启动客户端...${NC}"
    export ZLM_VIDEO_URL="${ZLM_VIDEO_URL:-http://zlmediakit:80}"
    export DISPLAY="${DISPLAY:-:0}"
    echo ""
    echo -e "${GREEN}请在全链路就绪后进行界面操作验证：${NC}"
    echo "  从登录页开始 → 登录(如 123/123) → 选车 → 确认并进入驾驶 → 点击「连接车端」"
    echo "  车端收到 start_stream 后向 ZLM 推四路测试图案 → 客户端四路视频应显示画面"
    echo ""
    export CLIENT_RESET_LOGIN=1
    bash "$SCRIPT_DIR/run.sh" client --reset-login
}

stop_all() {
    echo -e "${CYAN}=== 停止全链路节点 ===${NC}"
    $COMPOSE --profile '' down
    echo -e "${GREEN}已停止。${NC}"
}

run_client() {
    echo -e "${CYAN}=== 启动客户端（需先 run-e2e start）===${NC}"
    export ZLM_VIDEO_URL="${ZLM_VIDEO_URL:-http://zlmediakit:80}"
    export DISPLAY="${DISPLAY:-:0}"
    bash "$SCRIPT_DIR/run.sh" client
}

status_all() {
    echo -e "${CYAN}=== 全链路节点状态 ===${NC}"
    $COMPOSE ps
    echo ""
    check_all_nodes && true
    echo ""
    echo "客户端 GUI 需在 client-dev 容器内通过 run.sh 启动。"
}

case "${1:-start}" in
    start) start_all_and_client ;;
    start-no-client) start_all ;;
    start-and-verify) bash "$SCRIPT_DIR/start-full-chain.sh" ;;
    stop)  stop_all ;;
    client) run_client ;;
    status) status_all ;;
    *)
        echo "用法: $0 {start|start-no-client|start-and-verify|stop|client|status}"
        echo "  start             - 启动全链路并自动启动客户端"
        echo "  start-no-client   - 仅启动所有节点，不启动客户端"
        echo "  start-and-verify  - 启动全链路 + 逐环体验证 + 从登录页启动客户端（推荐）"
        echo "  stop              - 停止所有"
        echo "  client            - 仅启动客户端（前提已 start）"
        echo "  status            - 查看各节点状态"
        exit 1
        ;;
esac
