#!/bin/bash
# 统一启动与验证脚本：启动各组件，或随时执行验证
# 用法：
#   bash scripts/run.sh              # 启动客户端（默认）
#   bash scripts/run.sh client        # 启动客户端
#   bash scripts/run.sh client -r    # 启动客户端并清除登录状态
#   bash scripts/run.sh verify        # 仅验证（编译 + 运行 6 秒无崩溃）
#   bash scripts/run.sh backend       # 启动后端容器
#   bash scripts/run.sh vehicle       # 启动车辆端（本机）
#   bash scripts/run.sh media         # 启动媒体服务器（本机）
#   bash scripts/run.sh help         # 打印帮助

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# shellcheck disable=SC1091
source "$SCRIPT_DIR/lib/teleop-logs.sh"
teleop_logs_init

# client-dev 需 vehicle.dev 叠加层以挂载宿主机 ./logs → /workspace/logs
client_compose() {
    docker compose -f "$PROJECT_ROOT/docker-compose.yml" -f "$PROJECT_ROOT/docker-compose.vehicle.dev.yml" "$@"
}

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

print_help() {
    echo -e "${CYAN}============================================${NC}"
    echo -e "${CYAN}  远程驾驶 - 统一启动与验证${NC}"
    echo -e "${CYAN}============================================${NC}"
    echo ""
    echo -e "${GREEN}启动（可随时使用）：${NC}"
    echo "  ${BLUE}bash scripts/run.sh${NC}               # 启动客户端（默认）"
    echo "  ${BLUE}bash scripts/run.sh client${NC}        # 启动客户端"
    echo "  ${BLUE}bash scripts/run.sh client -r${NC}     # 启动客户端并清除登录状态"
    echo "  ${BLUE}bash scripts/run.sh backend${NC}       # 启动后端容器"
    echo "  ${BLUE}bash scripts/run.sh vehicle${NC}       # 启动车辆端容器"
    echo "  ${BLUE}bash scripts/run.sh media${NC}         # 启动 ZLMediaKit 容器"
    echo ""
    echo -e "${GREEN}验证（随时执行，约 10 秒）：${NC}"
    echo "  ${BLUE}bash scripts/run.sh verify${NC}       # 编译 + 运行客户端 6 秒，无崩溃即通过"
    echo ""
    echo -e "${GREEN}其他：${NC}"
    echo "  ${BLUE}bash scripts/run.sh help${NC}         # 显示本帮助"
    echo ""
}

# 子命令：验证（编译 + 6 秒运行）
cmd_verify() {
    exec bash "$SCRIPT_DIR/verify-client-ui.sh"
}

# 子命令：启动客户端（复用 run-client-ui.sh 逻辑，支持 -r / --reset-login）
cmd_client() {
    # 同步启动 CARLA 仿真（若未运行）；SKIP_CARLA=1 可跳过
    if [ -f "$SCRIPT_DIR/ensure-carla-running.sh" ]; then
        bash "$SCRIPT_DIR/ensure-carla-running.sh" 2>/dev/null || true
    fi

    # X11 与 DISPLAY
    export DISPLAY="${DISPLAY:-:0}"
    if command -v xhost &>/dev/null && ! xhost 2>&1 | grep -q "LOCAL:"; then
        echo -e "${YELLOW}设置 X11 权限...${NC}"
        xhost +local:docker 2>/dev/null || true
    fi

    # 若未启动 client-dev 容器则先启动
    if ! client_compose ps client-dev 2>/dev/null | grep -q Up; then
        echo -e "${YELLOW}启动 client-dev 容器...${NC}"
        client_compose up -d client-dev
        sleep 3
    fi

    # 未编译则先编译
    if ! client_compose exec -T client-dev bash -c "test -f /tmp/client-build/RemoteDrivingClient" 2>/dev/null; then
        echo -e "${YELLOW}客户端未编译，开始编译...${NC}"
        client_compose exec -T client-dev bash -c "mkdir -p /tmp/client-build && cd /tmp/client-build && ( [ ! -f CMakeCache.txt ] && cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug || true ) && make -j4" 2>&1 | tail -12
        echo -e "${GREEN}✓ 编译完成${NC}"
    fi
    _client_log="/workspace/logs/client-${TELEOP_LOG_DATE}.log"
    if [ -n "$CLIENT_RESET_LOGIN" ] || [ "$1" = "--reset-login" ] || [ "$1" = "-r" ]; then
        export CLIENT_RESET_LOGIN=1
        echo -e "${BLUE}启动客户端（清除登录状态）...${NC}"
        client_compose exec -T -e DISPLAY="$DISPLAY" -e CLIENT_RESET_LOGIN=1 -e "CLIENT_LOG_FILE=${CLIENT_LOG_FILE:-$_client_log}" ${ZLM_VIDEO_URL:+ -e ZLM_VIDEO_URL="$ZLM_VIDEO_URL"} client-dev bash -c "cd /tmp/client-build && ./RemoteDrivingClient --reset-login"
    else
        echo -e "${BLUE}启动客户端（正常使用，关闭窗口后退出）...${NC}"
        client_compose exec -T -e DISPLAY="$DISPLAY" -e "CLIENT_LOG_FILE=${CLIENT_LOG_FILE:-$_client_log}" ${ZLM_VIDEO_URL:+ -e ZLM_VIDEO_URL="$ZLM_VIDEO_URL"} client-dev bash -c "cd /tmp/client-build && ./RemoteDrivingClient"
    fi
    echo -e "${GREEN}客户端已退出。${NC}"
}

# 子命令：启动后端
cmd_backend() {
    echo -e "${BLUE}启动后端容器...${NC}"
    docker compose up -d backend
    echo -e "${GREEN}✓ 后端已启动${NC}"
}

# 子命令：启动车辆端（容器内运行）
cmd_vehicle() {
    echo -e "${BLUE}启动车辆端容器...${NC}"
    docker compose -f "$PROJECT_ROOT/docker-compose.yml" -f "$PROJECT_ROOT/docker-compose.vehicle.dev.yml" up -d vehicle
    echo -e "${GREEN}✓ 车辆端容器已启动${NC}"
}

# 子命令：启动媒体服务器（ZLMediaKit 容器）
cmd_media() {
    echo -e "${BLUE}启动 ZLMediaKit 容器...${NC}"
    docker compose up -d zlmediakit
    echo -e "${GREEN}✓ zlmediakit 已启动${NC}"
}

# 分发子命令
SUB="${1:-client}"
shift || true

case "$SUB" in
    client)
        cmd_client "$@"
        ;;
    verify)
        cmd_verify
        ;;
    backend)
        cmd_backend
        ;;
    vehicle)
        cmd_vehicle
        ;;
    media)
        cmd_media
        ;;
    help|--help|-h)
        print_help
        ;;
    *)
        echo -e "${RED}未知子命令: $SUB${NC}"
        echo ""
        print_help
        exit 1
        ;;
esac
