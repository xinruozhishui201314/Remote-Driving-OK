#!/bin/bash
# 一键启动全链路并逐环体验证，最后启动客户端供界面操作
#
# 链路：客户端 → 鉴权(Keycloak)/后端 → 选车/会话 → MQTT(start_stream) → 车端或 CARLA Bridge → 推流 → ZLM → 客户端拉流
# 本脚本：启动 Compose(dev Backend 8081) + 默认 CARLA 仿真(remote-driving/carla-with-bridge) → 验证 → 启动客户端
#
# 用法：
#   bash scripts/start-full-chain.sh              # 启动全链路 + 验证 + 从登录页启动客户端（默认跳过 2c 自动连视频，便于完整人工测登录/选车/连接）
#   bash scripts/start-full-chain.sh auto-connect # 在 2c 额外跑 verify-connect-feature（CLIENT_AUTO_CONNECT_VIDEO=1，约 18s，跳过登录，仅自动化拉流）
#   bash scripts/start-full-chain.sh manual       # 与默认相同（显式声明「跳过 2c」）；兼容旧脚本与文档
#   bash scripts/start-full-chain.sh no-verify    # 启动全链路 + 启动客户端（跳过验证）
#   bash scripts/start-full-chain.sh no-client    # 仅启动全链路 + 验证；结束后打印本机手动启动客户端的命令
#   bash scripts/start-full-chain.sh no-cleanup   # 跳过清理步骤（快速重启，不停止旧容器）
#   bash scripts/start-full-chain.sh cleanup      # 强制清理所有旧容器后启动
#   bash scripts/start-full-chain.sh no-build     # 跳过客户端编译步骤（客户端将在启动时自动编译）
#   bash scripts/start-full-chain.sh skip-carla   # 无 GPU/不跑仿真时跳过 CARLA 容器
#
# 环境变量（CARLA）：
#   SKIP_CARLA=1          等价 skip-carla
#   CARLA_NO_GPU=1        叠加 docker-compose.carla.nogpu.yml（无 nvidia-container-toolkit 时）
#   CARLA_MAP=Town02      仿真地图
#   CARLA_IMAGE_TAR=...   预导出镜像 tar（默认 deploy/carla/carla-with-bridge-latest.tar）
#
# 四路流 E2E（2b）：
#   默认后台运行，不阻塞客户端窗口；同步阻塞旧行为：STREAM_E2E_SYNC=1
#   缩短等待：STREAM_E2E_WAIT_MAX=30（传给 verify-stream-e2e.sh）
#
# 环境变量（日志）：
#   TELEOP_RUN_ID=my-run-1     自定义本会话子目录名（默认 YYYY-MM-DD-HHMMSS）；与 logs/<TELEOP_RUN_ID>/ 对应
#
# 环境变量（2c 自动化，可选）：
#   RUN_AUTO_CONNECT_VERIFY=1  与传参 auto-connect 等效：在 no-client 等场景也尝试跑 verify-connect-feature（需 DISPLAY）
#
# 日志：每次运行在本机 logs/<YYYY-MM-DD-HHMMSS>/ 下归档本会话（可通过 TELEOP_RUN_ID 覆盖运行 ID）；
#   compose 挂载 ./logs→/workspace/logs；客户端 CLIENT_LOG_FILE、验证脚本、docker-*.log 均在该子目录。
#
# 参数组合示例：
#   bash scripts/start-full-chain.sh no-cleanup           # 从登录页起客户端 + 跳过清理
#   bash scripts/start-full-chain.sh auto-connect no-cleanup # 含 2c 自动化 + 跳过清理
#   RUN_AUTO_CONNECT_VERIFY=1 bash scripts/start-full-chain.sh no-client # 无 UI 时也跑 2c（需 DISPLAY）
#   bash scripts/start-full-chain.sh manual no-build       # 兼容：与默认相同 + 跳过编译
clear

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# 与 start-all-nodes.sh 一致：dev backend 宿主机 8081；CARLA 为第四文件（可选 nogpu 覆盖）
dc_main() {
  docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml "$@"
}
dc_carla() {
  local a=( -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml -f docker-compose.carla.yml )
  [ "${CARLA_NO_GPU:-0}" = "1" ] && a+=( -f docker-compose.carla.nogpu.yml )
  docker compose "${a[@]}" "$@"
}
dc_all_down() {
  dc_carla down "$@"
}

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

# shellcheck disable=SC1091
source "$SCRIPT_DIR/lib/teleop-logs.sh"
source "$SCRIPT_DIR/lib/teleop-docker-log-followers.sh"
teleop_logs_init
teleop_logs_init_run_subdir
trap 'teleop_stop_docker_log_followers 2>/dev/null || true' EXIT
echo -e "${CYAN}本会话日志目录: ${TELEOP_LOGS_RUN_DIR}${NC}"
echo -e "${CYAN}（仓库 logs 根: ${TELEOP_LOGS_DIR}；运行 ID: ${TELEOP_RUN_ID}）${NC}"

BACKEND_URL="${BACKEND_URL:-http://127.0.0.1:8081}"
CARLA_MAP="${CARLA_MAP:-Town01}"
CARLA_VIN="${CARLA_VIN:-carla-sim-001}"
USE_PYTHON_BRIDGE="${USE_PYTHON_BRIDGE:-1}"
ZLM_URL="${ZLM_URL:-http://127.0.0.1:80}"
# ZLM_SECRET 从环境变量读取；不提供硬编码默认值，避免与实际配置不一致
ZLM_SECRET="${ZLM_SECRET:-}"
# VIN-prefixed 流名（多车隔离）：优先 CARLA_VIN，其次 VIN/VEHICLE_VIN 环境变量
_STREAM_VIN="${CARLA_VIN:-${VIN:-${VEHICLE_VIN:-}}}"
_VIN_PREFIX="${_STREAM_VIN:+${_STREAM_VIN}_}"
REQUIRED_STREAMS="${_VIN_PREFIX}cam_front ${_VIN_PREFIX}cam_rear ${_VIN_PREFIX}cam_left ${_VIN_PREFIX}cam_right"
CLIENT_DEV_IMAGE="${CLIENT_DEV_IMAGE:-remote-driving-client-dev:full}"

# ---------- Ctrl+C 时停止所有容器并退出 ----------
stop_all_on_sigint() {
    echo ""
    echo -e "${YELLOW}收到 Ctrl+C，正在停止所有容器并退出...${NC}"
    trap - INT TERM
    set +e
    teleop_stop_docker_log_followers 2>/dev/null || true
    dc_all_down --remove-orphans 2>/dev/null || true
    exit 130
}
trap stop_all_on_sigint INT TERM

# ---------- 确保 client-dev 完备镜像存在（缺则宿主机构建并 commit 打 tag）----------
ensure_client_dev_image() {
    if docker image inspect "$CLIENT_DEV_IMAGE" >/dev/null 2>&1; then
        echo -e "${GREEN}client-dev 镜像已存在: $CLIENT_DEV_IMAGE${NC}"
        return 0
    fi
    echo -e "${YELLOW}未找到镜像 $CLIENT_DEV_IMAGE，正在宿主机构建完备镜像（挂载 libdatachannel 后 commit 打 tag）...${NC}"
    bash "$SCRIPT_DIR/build-client-dev-full-image.sh" "$CLIENT_DEV_IMAGE"
}

# ---------- 停止并清理所有相关容器（完整重启） ----------
stop_and_cleanup_all() {
    echo -e "${CYAN}========== 0. 停止并清理旧容器 ==========${NC}"
    teleop_stop_docker_log_followers 2>/dev/null || true

    # 停止所有相关服务
    echo -e "${YELLOW}停止所有相关服务...${NC}"
    dc_all_down --remove-orphans 2>/dev/null || true
    
    # 查找并停止所有相关容器（包括不在 compose 中的）
    echo -e "${YELLOW}查找并停止残留容器...${NC}"
    local containers_to_stop=(
        "teleop-postgres"
        "teleop-keycloak"
        "teleop-coturn"
        "teleop-zlmediakit"
        "teleop-backend"
        "teleop-mosquitto"
        "teleop-client-dev"
        "teleop-mqtt"
        "remote-driving-vehicle"
        "carla-server"
    )
    
    for container_name in "${containers_to_stop[@]}"; do
        # 查找所有匹配的容器（包括部分匹配）
        local found_containers=$(docker ps -a --format '{{.Names}}' | grep -E "^${container_name}" || true)
        if [ -n "$found_containers" ]; then
            echo "$found_containers" | while read -r container; do
                if docker ps --format '{{.Names}}' | grep -q "^${container}$"; then
                    echo -e "  停止容器: $container"
                    docker stop "$container" 2>/dev/null || true
                fi
                echo -e "  删除容器: $container"
                docker rm "$container" 2>/dev/null || true
            done
        fi
    done
    
    # 查找 vehicle 相关容器（名称可能不同）
    local vehicle_containers=$(docker ps -a --format '{{.Names}}' | grep -i "vehicle" || true)
    if [ -n "$vehicle_containers" ]; then
        echo -e "${YELLOW}清理车端容器...${NC}"
        echo "$vehicle_containers" | while read -r container; do
            if docker ps --format '{{.Names}}' | grep -q "^${container}$"; then
                echo -e "  停止容器: $container"
                docker stop "$container" 2>/dev/null || true
            fi
            echo -e "  删除容器: $container"
            docker rm "$container" 2>/dev/null || true
        done
    fi
    
    # 检查并清理占用关键端口的进程
    echo -e "${YELLOW}检查关键端口占用...${NC}"
    local ports=(1883 8080 8081 5432 80 3478 2000)
    for port in "${ports[@]}"; do
        if command -v lsof &>/dev/null; then
            local pid=$(lsof -ti :$port 2>/dev/null | head -1)
            if [ -n "$pid" ]; then
                echo -e "${YELLOW}  端口 $port 被进程 $pid 占用（Docker容器停止后应自动释放）${NC}"
            fi
        fi
    done
    
    echo -e "${GREEN}清理完成${NC}"
    echo ""
    
    # 等待一下确保端口释放
    sleep 2
}

# ---------- 清理孤儿容器（旧服务，轻量级清理） ----------
cleanup_orphans() {
    echo -e "${YELLOW}检查并清理孤儿容器...${NC}"
    # 检查是否有旧的 teleop-mqtt 容器占用端口
    if docker ps --format '{{.Names}}' | grep -q "^teleop-mqtt$"; then
        echo -e "${YELLOW}  发现旧的 teleop-mqtt 容器，正在停止并删除...${NC}"
        docker stop teleop-mqtt 2>/dev/null || true
        docker rm teleop-mqtt 2>/dev/null || true
    fi
    # 检查端口 1883 是否被占用
    if command -v lsof &>/dev/null; then
        if lsof -i :1883 >/dev/null 2>&1; then
            local pid=$(lsof -ti :1883 | head -1)
            if [ -n "$pid" ]; then
                echo -e "${YELLOW}  端口 1883 被进程 $pid 占用，请手动处理${NC}"
            fi
        fi
    fi
}

# ---------- 启动 CARLA + Bridge（与 start-all-nodes.sh / docker-compose.carla.yml 对齐）----------
start_carla_stack() {
    echo -e "${CYAN}========== 1b. 启动 CARLA + Bridge（容器内）==========${NC}"
    export CARLA_MAP
    export USE_PYTHON_BRIDGE
    export DISPLAY="${DISPLAY:-:0}"
    # 只有当 DISPLAY 实际可用（X socket 存在）时才启用窗口模式；无 DISPLAY 则强制 offscreen
    # 避免 GPU 可用但无 X11 时 CARLA 强制 DISPLAY=:0 导致启动失败（entrypoint 超时退出）
    if [ -n "$DISPLAY" ] && [ -S "/tmp/.X11-unix/X${DISPLAY#*:}" ] 2>/dev/null; then
        export CARLA_SHOW_WINDOW="${CARLA_SHOW_WINDOW:-1}"
    else
        export CARLA_SHOW_WINDOW="${CARLA_SHOW_WINDOW:-0}"
    fi
    echo -e "${YELLOW}  CARLA_MAP=${CARLA_MAP} USE_PYTHON_BRIDGE=${USE_PYTHON_BRIDGE} DISPLAY=${DISPLAY} CARLA_SHOW_WINDOW=${CARLA_SHOW_WINDOW}${NC}"
    local CARLA_IMG="remote-driving/carla-with-bridge:latest"
    if ! docker image inspect "$CARLA_IMG" >/dev/null 2>&1; then
        local CARLA_TAR="${CARLA_IMAGE_TAR:-$PROJECT_ROOT/deploy/carla/carla-with-bridge-latest.tar}"
        if [ -f "$CARLA_TAR" ]; then
            echo -e "${YELLOW}  从 tar 加载镜像: $CARLA_TAR${NC}"
            docker load -i "$CARLA_TAR" 2>/dev/null || true
        fi
    fi
    if ! docker image inspect "$CARLA_IMG" >/dev/null 2>&1; then
        echo -e "${YELLOW}  正在构建 CARLA 镜像（耗时很长，请耐心等待）...${NC}"
        local _blog
        _blog="$(mktemp)"
        if dc_carla build carla >"$_blog" 2>&1; then
            tail -25 "$_blog"
            rm -f "$_blog"
        else
            tail -45 "$_blog"
            rm -f "$_blog"
            echo -e "${RED}✗ CARLA 镜像构建失败${NC}"
            return 1
        fi
    fi
    if command -v xhost &>/dev/null; then
        xhost +local:docker 2>/dev/null || true
    fi
    local _up
    _up="$(mktemp)"
    if dc_carla up -d carla >"$_up" 2>&1; then
        rm -f "$_up"
    else
        cat "$_up"
        rm -f "$_up"
        echo -e "${RED}✗ CARLA 容器启动失败。无 NVIDIA 时可设 CARLA_NO_GPU=1；不跑仿真请使用参数 skip-carla${NC}"
        return 1
    fi
    if docker ps --format '{{.Names}}' | grep -q '^carla-server$'; then
        echo -e "${GREEN}  ✓ carla-server 已运行${NC}"
        echo -e "${YELLOW}  等待 CARLA 仿真与 Bridge 初始化（约 25s）...${NC}"
        sleep 25
        docker logs carla-server 2>&1 | tail -35 | sed 's/^/    /' || true
        return 0
    fi
    echo -e "${RED}✗ 未检测到运行中的 carla-server${NC}"
    return 1
}

# ---------- 启动所有节点（Compose dev + CARLA 仿真，默认启用 CARLA）----------
start_all_nodes() {
    echo -e "${CYAN}========== 1. 启动全链路节点 ==========${NC}"
    echo "  若车端镜像有更新，请先: docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml build vehicle"
    echo ""

    docker network create teleop-network 2>/dev/null || true
    cleanup_orphans

    dc_main up -d --remove-orphans teleop-postgres zlmediakit teleop-mosquitto
    sleep 4
    dc_main up -d --remove-orphans keycloak
    sleep 3

    docker rm -f remote-driving-backend-1 2>/dev/null || true
    dc_main up -d --remove-orphans backend

    echo -e "${YELLOW}等待 Keycloak / Backend(${BACKEND_URL}) / ZLM — dev Backend 首次编译可能需数分钟...${NC}"
    local w=0
    while [ "$w" -lt 240 ]; do
        if curl -sf "http://127.0.0.1:8080/realms/teleop/.well-known/openid-configuration" >/dev/null 2>&1 && \
           curl -sf "${BACKEND_URL}/health" >/dev/null 2>&1 && \
           curl -sf "${ZLM_URL}/index/api/getServerConfig" >/dev/null 2>&1; then
            echo -e "${GREEN}  基础服务已就绪（约 $((w * 3))s）${NC}"
            break
        fi
        sleep 3
        w=$((w + 1))
        if [ $((w % 20)) -eq 0 ]; then
            echo -e "  ${CYAN}... 仍等待（$((w * 3))s / 720s）${NC}"
        fi
    done

    if ! docker image inspect "${CLIENT_DEV_IMAGE}" >/dev/null 2>&1; then
        echo -e "${YELLOW}未找到 ${CLIENT_DEV_IMAGE}，尝试构建...${NC}"
        bash "$SCRIPT_DIR/build-client-dev-full-image.sh" "${CLIENT_DEV_IMAGE}" 2>/dev/null || true
    fi
    dc_main up -d --no-build --remove-orphans vehicle client-dev 2>/dev/null || dc_main up -d --remove-orphans vehicle client-dev
    # client-dev 仅 depends_on backend；vehicle 失败或退出时不应阻止 UI 容器
    dc_main up -d --remove-orphans client-dev 2>/dev/null || true

    if [ "${SKIP_CARLA:-0}" != "1" ]; then
        if ! start_carla_stack; then
            echo -e "${RED}CARLA 环节失败。安装 nvidia-container-toolkit 并重启 Docker，或 CARLA_NO_GPU=1 / skip-carla 后重试。${NC}"
            return 1
        fi
    else
        echo -e "${YELLOW}已跳过 CARLA（SKIP_CARLA=1 或参数 skip-carla）${NC}"
    fi
    echo ""
}

# ---------- 节点未就绪时打印排查信息 ----------
print_node_diagnostics() {
    echo -e "${YELLOW}---------- 排查: teleop-vehicle / teleop-client-dev ----------${NC}"
    docker ps -a --filter name=teleop-vehicle --filter name=teleop-client-dev --format 'table {{.Names}}\t{{.Status}}\t{{.Ports}}' 2>/dev/null || true
    if docker ps -a --format '{{.Names}}' | grep -q '^teleop-vehicle$'; then
        echo -e "${CYAN}teleop-vehicle 最近日志:${NC}"
        docker logs teleop-vehicle --tail 40 2>&1 | sed 's/^/  /' || true
    fi
    echo -e "${YELLOW}提示: CARLA 仿真时 client-dev 不再强依赖 vehicle；若仍失败请检查 client-dev 镜像与 backend。${NC}"
}

# ---------- 检查节点均在运行 ----------
check_nodes_up() {
    local failed=0
    for name in teleop-postgres teleop-keycloak teleop-zlmediakit teleop-backend teleop-mosquitto teleop-client-dev; do
        if ! docker ps --format '{{.Names}}' | grep -q "^${name}$" || ! docker ps --format '{{.Status}}' --filter "name=^${name}$" | grep -qi "up"; then
            echo -e "  ${RED}✗${NC} $name 未运行"; failed=1
        fi
    done
    local vname
    vname=$(docker ps --format '{{.Names}}' | grep "vehicle" | head -1)
    if [ -z "$vname" ] || ! docker ps --format '{{.Status}}' --filter "name=${vname}" | grep -qi "up"; then
        # CARLA + Bridge 推流时不需要 teleop-vehicle；仅 NuScenes 车端或未启 CARLA 时强依赖
        if [ "${SKIP_CARLA:-0}" != "1" ] && docker ps --format '{{.Names}}' | grep -q '^carla-server$'; then
            echo -e "  ${YELLOW}⊘${NC} vehicle 未运行（已启用 CARLA，可仅用仿真推流；客户端 UI 不依赖此容器）"
        else
            echo -e "  ${RED}✗${NC} vehicle 未运行"; failed=1
        fi
    fi
    if [ "${SKIP_CARLA:-0}" != "1" ]; then
        if ! docker ps --format '{{.Names}}' | grep -q '^carla-server$'; then
            echo -e "  ${RED}✗${NC} carla-server 未运行（仿真链未就绪）"; failed=1
        fi
    fi
    if [ "$failed" -eq 1 ]; then
        print_node_diagnostics
    fi
    return $failed
}

# ---------- 逐环体验证 ----------
verify_chain() {
    echo -e "${CYAN}========== 2. 逐环体验证（客户端→后端→ZLM→MQTT→车端→四路流）==========${NC}"
    local failed=0

    # 2.1 Postgres
    echo -n "  [2.1] Postgres ... "
    if dc_main exec -T teleop-postgres pg_isready -U postgres -d teleop >/dev/null 2>&1; then
        echo -e "${GREEN}✓${NC}"
    else
        echo -e "${RED}✗${NC}"; failed=1
    fi

    # 2.2 Keycloak（增加重试机制，Keycloak 启动可能需要时间）
    echo -n "  [2.2] Keycloak ... "
    local keycloak_ok=0
    for i in 1 2 3 4 5; do
        if curl -sf -o /dev/null "http://127.0.0.1:8080/health/ready" 2>/dev/null; then
            keycloak_ok=1; break
        fi
        sleep 2
    done
    if [ $keycloak_ok -eq 1 ]; then
        echo -e "${GREEN}✓${NC}"
    else
        # 如果 /health/ready 失败，尝试检查 /health 或实际功能端点
        if curl -sf -o /dev/null "http://127.0.0.1:8080/health" 2>/dev/null || \
           curl -sf -o /dev/null "http://127.0.0.1:8080/realms/teleop/.well-known/openid-configuration" 2>/dev/null; then
            echo -e "${GREEN}✓${NC} (健康检查端点未就绪，但服务可用)"
        else
            echo -e "${RED}✗${NC}"; failed=1
        fi
    fi

    # 2.3 Backend（与启动阶段等待一致，此处再兜底）
    echo -n "  [2.3] Backend /health ... "
    local backend_ok=0
    for i in $(seq 1 60); do
        if curl -sf -o /dev/null "${BACKEND_URL}/health" 2>/dev/null; then
            backend_ok=1; break
        fi
        sleep 3
    done
    if [ $backend_ok -eq 1 ]; then echo -e "${GREEN}✓${NC}"; else echo -e "${RED}✗${NC}"; failed=1; fi

    # 2.3b CARLA RPC（本机 2000 端口可连表示仿真进程在监听）
    if [ "${SKIP_CARLA:-0}" != "1" ]; then
        echo -n "  [2.3b] CARLA :2000 ... "
        if timeout 3 bash -c 'exec 3<>/dev/tcp/127.0.0.1/2000' 2>/dev/null; then
            echo -e "${GREEN}✓${NC}"
        else
            echo -e "${YELLOW}⊘${NC} (端口未就绪，仿真可能仍在加载)"
        fi
    fi

    # 2.4 ZLM API
    echo -n "  [2.4] ZLMediaKit API ... "
    if curl -sf -o /dev/null "${ZLM_URL}/index/api/getServerConfig" 2>/dev/null; then
        echo -e "${GREEN}✓${NC}"
    else
        echo -e "${RED}✗${NC}"; failed=1
    fi

    # 2.5 MQTT Broker 验证（使用独立验证脚本）
    echo -n "  [2.5] MQTT Broker ... "
    if bash "$SCRIPT_DIR/verify-mosquitto.sh" >/dev/null 2>&1; then
        echo -e "${GREEN}✓${NC}"
    else
        echo -e "${YELLOW}⊘${NC} (MQTT Broker 验证失败，但继续执行)"
    fi

    # 2.5b 底盘数据流验证（车端 → Mosquitto → 客户端）
    echo -n "  [2.5b] 底盘数据流（vehicle/status）... "
    local chassis_data_ok=0
    # 尝试使用容器内的 mosquitto_sub（如果可用）
    if dc_main run --rm --no-deps teleop-mosquitto timeout 3 mosquitto_sub -h teleop-mosquitto -p 1883 -t "vehicle/status" -C 1 >/dev/null 2>&1; then
        chassis_data_ok=1
    # 或者尝试宿主机（如果安装了 mosquitto-clients）
    elif command -v mosquitto_sub &>/dev/null && \
         timeout 3 mosquitto_sub -h 127.0.0.1 -p 1883 -t "vehicle/status" -C 1 >/dev/null 2>&1; then
        chassis_data_ok=1
    fi
    if [ $chassis_data_ok -eq 1 ]; then
        echo -e "${GREEN}✓${NC} (车端正在发布底盘数据)"
    else
        echo -e "${YELLOW}⊘${NC} (未检测到底盘数据，将在客户端连接后自动开始)"
    fi

    # 2.6 车端 / CARLA Bridge 收 MQTT + 推流 → ZLM 四路流
    echo -n "  [2.6] start_stream → ZLM 四路流 ... "
    local stream_vin="${STREAM_TEST_VIN:-}"
    if [ -z "$stream_vin" ]; then
        if [ "${SKIP_CARLA:-0}" != "1" ] && docker ps --format '{{.Names}}' | grep -q '^carla-server$'; then
            stream_vin="$CARLA_VIN"
        else
            stream_vin="${VEHICLE_STREAM_VIN:-E2ETESTVIN0000001}"
        fi
    fi
    local msg
    msg=$(printf '%s' "{\"type\":\"start_stream\",\"vin\":\"${stream_vin}\",\"timestampMs\":0}")
    if dc_main exec -T teleop-mosquitto mosquitto_pub -h localhost -p 1883 \
        -u client_user -P "${MQTT_CLIENT_PASSWORD:-client_password_change_in_prod}" \
        -t vehicle/control -m "$msg" >/dev/null 2>&1; then
        echo -e "${GREEN}✓${NC} (已发送 start_stream vin=${stream_vin})"
    elif command -v mosquitto_pub &>/dev/null && \
         mosquitto_pub -h 127.0.0.1 -p 1883 -t vehicle/control -m "$msg" >/dev/null 2>&1; then
        echo -e "${GREEN}✓${NC} (已发送 start_stream 宿主机 vin=${stream_vin})"
    else
        echo -e "${YELLOW}⊘${NC} (无法发送 start_stream，将在客户端手动触发)"
    fi
    local wait_max=30 interval=2 elapsed=0 found=0
    while [ $elapsed -lt $wait_max ]; do
        local json
        json="$(curl -sf "${ZLM_URL}/index/api/getMediaList?secret=${ZLM_SECRET}&app=teleop" 2>/dev/null || echo '{"code":-1}')"
        if echo "$json" | grep -q '"code":0'; then
            local missing=""
            for s in $REQUIRED_STREAMS; do
                echo "$json" | grep -q "\"stream\":\"$s\"" || missing="$missing $s"
            done
            if [ -z "$missing" ]; then found=1; break; fi
        fi
        sleep $interval
        elapsed=$((elapsed + interval))
    done
    if [ $found -eq 1 ]; then
        echo -e "${GREEN}✓${NC} (${_VIN_PREFIX}cam_front/${_VIN_PREFIX}cam_rear/${_VIN_PREFIX}cam_left/${_VIN_PREFIX}cam_right)"
    else
        echo -e "${YELLOW}⊘${NC} 超时未就绪，可在客户端点击「连接车端」后自动推流"
    fi

    echo ""
    if [ $failed -eq 1 ]; then
        echo -e "${RED}部分环节验证未通过，请检查上述输出。${NC}"
        return 1
    fi
    echo -e "${GREEN}逐环体验证通过。${NC}"
    return 0
}

# ---------- 在 Vehicle-side 容器内自动运行车端数据集本地校验（已挂载数据集时）----------
ensure_vehicle_dataset_verified() {
    echo -e "${CYAN}========== 车端数据集本地校验（Vehicle-side 容器内）==========${NC}"
    if dc_main exec -T -e SWEEPS_PATH="${SWEEPS_PATH:-/data/sweeps}" vehicle bash /app/scripts/verify-vehicle-dataset.sh 2>/dev/null; then
        echo -e "${GREEN}车端数据集本地校验通过。${NC}"
        echo ""
        return 0
    fi
    echo -e "${YELLOW}跳过车端数据集本地校验（未挂载数据集或 SWEEPS_PATH 不可用）。${NC}"
    echo ""
    return 0
}

# ---------- 检查 client-dev 容器内中文字体（镜像应已预装，仅检查）----------
ensure_client_chinese_font() {
    if dc_main exec -T -u root client-dev bash -c 'dpkg -l fonts-wqy-zenhei 2>/dev/null | grep -q ^ii' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} 中文字体已安装（fonts-wqy-zenhei）"
        return 0
    fi
    echo -e "${YELLOW}⊘${NC} 中文字体未安装（建议在构建镜像时安装）"
    echo -e "${YELLOW}  提示：${NC}如需安装，请修改 client/Dockerfile.client-dev 添加 fonts-wqy-zenhei"
    # 不再自动安装，避免启动时安装
    # echo -e "${CYAN}========== 安装中文字体（fonts-wqy-zenhei）==========${NC}"
    # dc_main exec -T -u root client-dev bash -c 'apt-get update -qq && apt-get install -y -qq --no-install-recommends fonts-wqy-zenhei >/dev/null && rm -rf /var/lib/apt/lists/*' 2>/dev/null || true
    echo ""
}

# ---------- 确保 client-dev 容器内有 mosquitto_pub（无 Paho 时客户端用其发送 start_stream）----------
ensure_client_mosquitto_pub() {
    if dc_main exec -T client-dev bash -c 'command -v mosquitto_pub >/dev/null 2>&1' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} mosquitto_pub 已可用"
        return 0
    fi
    # 现成镜像未预装时，在容器内运行时安装一次（不重建镜像）
    echo -e "${YELLOW}容器内未检测到 mosquitto_pub，正在安装 mosquitto-clients...${NC}"
    if dc_main exec -T -u root client-dev bash -c 'apt-get update -qq && apt-get install -y -qq --no-install-recommends mosquitto-clients && rm -rf /var/lib/apt/lists/*' 2>/dev/null; then
        if dc_main exec -T client-dev bash -c 'command -v mosquitto_pub >/dev/null 2>&1' 2>/dev/null; then
            echo -e "${GREEN}✓${NC} mosquitto_pub 已安装并可用"
            return 0
        fi
    fi
    echo -e "${RED}✗${NC} mosquitto_pub 不可用，且运行时安装失败"
    echo -e "${YELLOW}  可尝试: bash scripts/build-client-dev-full-image.sh 重新构建镜像${NC}"
    echo ""
    return 1
}

# ---------- 确保 client-dev 内有 OpenGL 头文件（Qt QOpenGLContext / GL/gl.h 编译依赖）----------
ensure_client_libgl_mesa_dev() {
    if dc_main exec -T client-dev bash -c 'test -f /usr/include/GL/gl.h' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} OpenGL 开发头文件已存在（libgl1-mesa-dev，GL/gl.h）"
        return 0
    fi
    echo -e "${YELLOW}容器内未检测到 GL/gl.h，正在安装 libgl1-mesa-dev...${NC}"
    if dc_main exec -T -u root client-dev bash -c \
        'apt-get update -qq && apt-get install -y -qq --no-install-recommends libgl1-mesa-dev && rm -rf /var/lib/apt/lists/*' 2>/dev/null; then
        if dc_main exec -T client-dev bash -c 'test -f /usr/include/GL/gl.h' 2>/dev/null; then
            echo -e "${GREEN}✓${NC} libgl1-mesa-dev 已安装，GL/gl.h 可用"
            return 0
        fi
    fi
    echo -e "${RED}✗${NC} libgl1-mesa-dev 安装失败或仍缺少 GL/gl.h"
    echo -e "${YELLOW}  可尝试: 以 root 进入容器手动 apt-get install libgl1-mesa-dev，或重建镜像（client/Dockerfile.client-dev 已包含该包）${NC}"
    echo ""
    return 1
}

# ---------- 确保 client-dev 内有 qsb（Qt ShaderTools，用于编译 .qsb 着色器；无 qsb → 视频渲染黑屏）----------
# 容器内运行时安装：镜像构建时未装 qsb 时，通过 aqt 追加安装；已装则跳过
ensure_qsb_in_container() {
    if dc_main exec -T client-dev bash -c '[ -x /opt/Qt/6.8.0/gcc_64/bin/qsb ]' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} qsb 已存在（/opt/Qt/6.8.0/gcc_64/bin/qsb）"
        return 0
    fi
    echo -e "${YELLOW}容器内未检测到 qsb，正在安装 qtshadertools 模块...${NC}"
    # 容器内以 root 安装 aqtinstall，再安装 qtshadertools（含 qsb），安装后清理 aqt
    if dc_main exec -T -u root client-dev bash -c "
        set -e
        export DEBIAN_FRONTEND=noninteractive
        export QT_DEBUG_PLUGINS=0

        # 安装 pip + 编译工具（pyzstd 是 C 扩展，pip install aqtinstall 依赖它，缺少 python3-dev + build-essential 会导致 wheel 构建失败）
        if ! command -v pip3 &>/dev/null; then
            apt-get update -qq
            apt-get install -y --no-install-recommends python3-pip 2>&1 | tail -1
            rm -rf /var/lib/apt/lists/partial
        fi
        apt-get update -qq
        apt-get install -y --no-install-recommends python3-dev build-essential 2>&1 | tail -1
        rm -rf /var/lib/apt/lists/partial

        # 安装/升级 aqtinstall
        pip3 install --no-cache-dir -i https://pypi.tuna.tsinghua.edu.cn/simple aqtinstall 2>&1 | tail -1

        # 通过 aqt 追加安装 qtshadertools 模块（含 qsb 工具）
        # 关键参数：linux desktop + linux_gcc_64（不是 gcc_64）+ -m qtshadertools
        echo '安装 qtshadertools 模块（包含 qsb）...'
        aqt install-qt -O /opt/Qt \
            linux desktop 6.8.0 linux_gcc_64 \
            -m qtshadertools 2>&1 | tail -3

        # 清理 aqt 及 pip（节省镜像层空间）
        pip3 uninstall -y aqtinstall pyzstd brotli py7zr pybcj psutil 2>/dev/null || true

        echo '验证 qsb...'
        /opt/Qt/6.8.0/gcc_64/bin/qsb --version 2>&1 | grep -v 'Detected locale' | head -1
        echo '✓ qtshadertools 模块（含 qsb）安装成功'
    " 2>/dev/null; then
        if dc_main exec -T client-dev bash -c '[ -x /opt/Qt/6.8.0/gcc_64/bin/qsb ]' 2>/dev/null; then
            echo -e "${GREEN}✓${NC} qsb 安装成功，着色器编译将正常工作"
            return 0
        fi
    fi
    echo -e "${RED}✗${NC} qsb 安装失败，视频渲染可能黑屏"
    echo -e "${YELLOW}  建议：重建 client-dev 镜像（修复了 Dockerfile）: bash scripts/build-client-dev-full-image.sh${NC}"
    echo ""
    return 1
}

# ---------- 确保 client-dev 内有 libxkbcommon-dev（Qt6Gui 依赖 XKB::XKB CMake 目标）----------
ensure_client_libxkbcommon_dev() {
    if dc_main exec -T client-dev bash -c 'test -f /usr/include/xkbcommon/xkbcommon.h' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} libxkbcommon-dev 已存在（XKB::XKB CMake 目标可用）"
        return 0
    fi
    echo -e "${YELLOW}容器内未检测到 libxkbcommon-dev，正在安装...${NC}"
    if dc_main exec -T -u root client-dev bash -c \
        'apt-get update -qq && apt-get install -y -qq --no-install-recommends libxkbcommon-dev && rm -rf /var/lib/apt/lists/*' 2>/dev/null; then
        if dc_main exec -T client-dev bash -c 'test -f /usr/include/xkbcommon/xkbcommon.h' 2>/dev/null; then
            echo -e "${GREEN}✓${NC} libxkbcommon-dev 已安装，XKB::XKB CMake 目标可用"
            return 0
        fi
    fi
    echo -e "${RED}✗${NC} libxkbcommon-dev 安装失败，Qt6Gui 将找不到 XKB::XKB 目标"
    echo -e "${YELLOW}  可尝试: 以 root 进入容器手动 apt-get install libxkbcommon-dev${NC}"
    echo ""
    return 1
}

# ---------- 强制重新编译客户端（确保使用最新代码）----------
ensure_client_built() {
    echo -e "${CYAN}========== 编译客户端（强制重新编译以确保使用最新代码）==========${NC}"
    
    # 停止可能正在运行的客户端进程
    dc_main exec -T client-dev bash -c 'pkill -9 RemoteDrivingClient 2>/dev/null || true; sleep 1' 2>/dev/null || true
    
    # 清理旧的构建目录（如果 build 目录被占用，使用 /tmp/client-build）
    if ! dc_main exec -T client-dev bash -c 'cd /workspace/client && rm -rf build/.qt 2>/dev/null || true' 2>/dev/null; then
        echo -e "${YELLOW}⚠ build 目录被占用，将使用 /tmp/client-build 目录${NC}"
    fi
    
    # 强制重新编译（使用 /tmp/client-build 确保干净编译）
    if dc_main exec -T client-dev bash -c '
        set -e
        mkdir -p /tmp/client-build && cd /tmp/client-build
        echo "配置 CMake..."
        cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug
        echo "编译客户端..."
        make -j4
        if [ ! -x ./RemoteDrivingClient ]; then
            echo "错误: 编译完成但可执行文件不存在"
            exit 1
        fi
        echo "编译成功: $(pwd)/RemoteDrivingClient"
        # 验证 QML 文件存在
        if [ ! -f /workspace/client/qml/DrivingInterface.qml ]; then
            echo "错误: QML 文件不存在"
            exit 1
        fi
        # 验证驾驶页关键布局块（宽度已改为 dashboard* 变量，非写死 80/70）
        if ! grep -A 6 "水箱 + 垃圾箱" /workspace/client/qml/DrivingInterface.qml | grep -q "Layout.preferredWidth: dashboardTankWidth"; then
            echo "警告: 未找到水箱/垃圾箱区域 Layout.preferredWidth: dashboardTankWidth，QML 可能未同步"
        fi
        if ! grep -A 6 "速度控制" /workspace/client/qml/DrivingInterface.qml | grep -q "Layout.preferredWidth: dashboardSpeedWidth"; then
            echo "警告: 未找到速度控制区域 Layout.preferredWidth: dashboardSpeedWidth，QML 可能未同步"
        fi
    ' 2>&1; then
        echo -e "${GREEN}✓ 客户端编译成功（可执行文件: 容器内 /tmp/client-build/RemoteDrivingClient）${NC}"
        echo ""
        return 0
    else
        echo -e "${RED}❌ 客户端编译失败！${NC}"
        echo -e "${RED}请检查编译错误信息 above，修复后重新运行。${NC}"
        echo ""
        exit 1  # 编译失败，直接退出
    fi
}

# 若后台跑了 verify-stream-e2e，在适当时机等待并汇总结果
wait_stream_e2e_background() {
    [ -n "${STREAM_E2E_PID:-}" ] || return 0
    if kill -0 "$STREAM_E2E_PID" 2>/dev/null; then
        echo -e "${CYAN}等待后台四路流验证结束 (PID $STREAM_E2E_PID)，日志: ${STREAM_E2E_LOG:-}${NC}"
        wait "$STREAM_E2E_PID" || true
    fi
    if [ -n "${STREAM_E2E_LOG:-}" ] && [ -f "${STREAM_E2E_LOG}.rc" ]; then
        _rc=$(cat "${STREAM_E2E_LOG}.rc" 2>/dev/null || echo 1)
        if [ "$_rc" -eq 0 ]; then
            echo -e "${GREEN}后台四路流 E2E: VERIFY_OK（见 ${STREAM_E2E_LOG}）${NC}"
        else
            echo -e "${YELLOW}后台四路流 E2E: 未通过或未就绪（见 ${STREAM_E2E_LOG}）${NC}"
            VERIFY_STREAM_FAILED=1
        fi
    fi
}

# ---------- 启动客户端（容器内，带全链路环境变量） ----------
# 若当前环境无可用 DISPLAY（如 SSH、无图形），则只打印手动启动命令并退出，不阻塞
start_client() {
    local _client_log
    _client_log="$(teleop_client_log_container_path)"
    echo -e "${CYAN}========== 3. 启动远程驾驶客户端（可手动操作）==========${NC}"
    # 优先保留当前会话 DISPLAY（如 CARLA 在 :1）；仅当对应 socket 不存在时再试 :0 → :1
    if [ -n "${DISPLAY:-}" ]; then
        _dnum="${DISPLAY#*:}"
        _dnum="${_dnum%%.*}"
        if [ ! -S "/tmp/.X11-unix/X${_dnum}" ]; then
            unset DISPLAY
        fi
    fi
    if [ -z "${DISPLAY:-}" ]; then
        if [ -S /tmp/.X11-unix/X0 ]; then
            export DISPLAY=:0
        elif [ -S /tmp/.X11-unix/X1 ]; then
            export DISPLAY=:1
        else
            export DISPLAY="${DISPLAY:-:0}"
        fi
    fi
    if command -v xhost &>/dev/null; then
        xhost +local:docker 2>/dev/null || true
    fi
    # 检查当前终端是否有可用的 X11 socket（无则说明在 SSH/无图形，只给出手动命令）
    if [ ! -S "/tmp/.X11-unix/X${DISPLAY#*:}" ]; then
        echo -e "${YELLOW}当前环境无可用图形界面（无 X11 socket），无法在此终端直接打开客户端。${NC}"
        echo -e "${GREEN}所有节点已运行。请在本机有图形界面的终端执行以下命令启动远程驾驶客户端：${NC}"
        echo ""
        echo "  xhost +local:docker"
        echo "  docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml exec -it -e DISPLAY=:0 -e CLIENT_AUTO_CONNECT_VIDEO=0 -e CLIENT_LOG_FILE=${_client_log} -e ZLM_VIDEO_URL=http://zlmediakit:80 -e MQTT_BROKER_URL=mqtt://teleop-mosquitto:1883 client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login'"
        echo ""
        return 0
    fi
    # 容器内使用服务名访问 ZLM 和 MQTT
    export ZLM_VIDEO_URL="${ZLM_VIDEO_URL:-http://zlmediakit:80}"
    export MQTT_BROKER_URL="${MQTT_BROKER_URL:-mqtt://teleop-mosquitto:1883}"
    export CLIENT_RESET_LOGIN=1
    # 2c 自动化曾用 CLIENT_AUTO_CONNECT_VIDEO=1 跳过登录；手动客户端强制关闭，避免宿主机 export 遗留进 exec
    export CLIENT_AUTO_CONNECT_VIDEO=0
    echo ""
    echo -e "${CYAN}若无法打开客户端窗口（如报 Authorization required），请在本机有图形界面的终端执行：${NC}"
    echo "  xhost +local:docker"
    echo "  docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml exec -it -e DISPLAY=:0 -e CLIENT_AUTO_CONNECT_VIDEO=0 -e CLIENT_LOG_FILE=${_client_log} -e ZLM_VIDEO_URL=$ZLM_VIDEO_URL -e MQTT_BROKER_URL=$MQTT_BROKER_URL client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login'"
    echo ""
    echo -e "${GREEN}请按以下步骤在客户端界面操作验证：${NC}"
    echo "  1) 登录（如 123 / 123）"
    echo "  2) 选车（E2ETESTVIN0000001 或 carla-sim-001）→ 确认并进入驾驶"
    echo "  3) 点击「连接车端」→ 约 6s 后拉四路流；若流未就绪会自动重试（最多 8 次）"
    echo "  4) 确认四路视频有画面"
    echo -e "  ${CYAN}仿真：${NC}宿主机启动 CARLA + carla-bridge 后，选车 carla-sim-001 可看仿真推流；整链验证: ./scripts/verify-full-chain-with-carla.sh"
    echo ""
    echo -e "${CYAN}【底盘数据流验证】${NC}"
    echo "  5) 在右侧控制面板的「📊 车辆状态」区域查看实时更新的底盘数据："
    echo "     - 速度：实时显示车辆速度（km/h）"
    echo "     - 电池：显示电池电量和进度条（绿色/红色）"
    echo "     - 里程：显示累计里程（km）"
    echo "     - 电压：显示电池电压（V）"
    echo "     - 电流：显示实时电流（A）"
    echo "     - 温度：显示温度值（°C，根据温度变色）"
    echo "  6) 确认数据实时更新（50Hz，约每20ms更新一次）"
    echo "  7) 在主界面中央速度表查看速度、档位、转向角度"
    echo ""
    echo -e "${YELLOW}提示：${NC}如果底盘数据未显示，请检查："
    echo "  - 车端是否已连接到 MQTT Broker"
    echo "  - 是否已发送 start_stream 命令（点击「连接车端」会自动发送）"
    echo "  - 查看车端日志：docker compose logs vehicle | grep -i 'status\|mqtt'"
    echo ""
    echo -e "${CYAN}按 Ctrl+C 将关闭客户端；停止全栈（含 CARLA）请另开终端: docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml -f docker-compose.carla.yml down${NC}"
    echo ""
    # 使用 -it 以便 Ctrl+C/关闭窗口能正确结束容器内进程
    # 设置 Qt 平台插件和日志级别，避免黑屏问题
    dc_main exec -it \
        -e DISPLAY="$DISPLAY" \
        -e QT_QPA_PLATFORM=xcb \
        -e QT_LOGGING_RULES="qt.qpa.*=false" \
        -e ZLM_VIDEO_URL="$ZLM_VIDEO_URL" \
        -e MQTT_BROKER_URL="$MQTT_BROKER_URL" \
        -e CLIENT_RESET_LOGIN=1 \
        -e CLIENT_AUTO_CONNECT_VIDEO=0 \
        -e "CLIENT_LOG_FILE=${_client_log}" \
        client-dev bash -c '
        set -e  # 任何错误立即退出
        mkdir -p /tmp/client-build && cd /tmp/client-build
        
        # 优先使用 /tmp/client-build（确保是最新编译的）
        if [ -x ./RemoteDrivingClient ]; then
            echo "✓ 使用已编译的客户端: /tmp/client-build/RemoteDrivingClient"
            echo "  验证 QML 文件..."
            if [ ! -f /workspace/client/qml/DrivingInterface.qml ]; then
                echo "❌ 错误: QML 文件不存在"
                exit 1
            fi
            exec ./RemoteDrivingClient --reset-login
        elif [ -x /workspace/client/build/RemoteDrivingClient ]; then
            echo "⚠ 使用 /workspace/client/build 中的客户端（可能不是最新编译）"
            echo "  建议重新编译以确保使用最新代码"
            exec /workspace/client/build/RemoteDrivingClient --reset-login
        else
            # 未编译，强制重新编译（确保使用最新代码）
            echo "客户端未编译，正在强制重新编译（确保使用最新代码）..."
            set -e  # 编译失败时立即退出
            if [ ! -f CMakeCache.txt ]; then
                echo "配置 CMake..."
                cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug
            fi
            echo "编译客户端..."
            make -j4
            if [ ! -x ./RemoteDrivingClient ]; then
                echo "❌ 错误: 编译完成但可执行文件不存在"
                exit 1
            fi
            echo "✓ 编译成功，启动客户端..."
            exec ./RemoteDrivingClient --reset-login
        fi
    '
}

# ---------- main ----------
SKIP_CARLA="${SKIP_CARLA:-0}"
DO_VERIFY=1
DO_CLIENT=1
DO_MANUAL_ONLY=1   # 默认跳过 2c（自动连视频会跳过登录）；需要 2c 时传 auto-connect 或设 RUN_AUTO_CONNECT_VERIFY=1
DO_CLEANUP=1  # 默认启用清理
DO_BUILD=1    # 默认启用编译检查
for arg in "$@"; do
    case "$arg" in
        no-verify)   DO_VERIFY=0 ;;
        no-client)   DO_CLIENT=0 ;;
        manual)      DO_MANUAL_ONLY=1 ;;  # 与默认相同：跳过 2c
        auto-connect) DO_MANUAL_ONLY=0 ;; # 运行 2c verify-connect-feature（跳过登录的自动化拉流）
        no-cleanup)  DO_CLEANUP=0 ;;  # 跳过清理步骤（快速重启）
        cleanup)     DO_CLEANUP=1 ;;  # 强制清理（默认行为）
        no-build)    DO_BUILD=0 ;;  # 跳过编译步骤（客户端启动时自动编译）
        skip-carla)  SKIP_CARLA=1 ;; # 无 GPU / 不跑 CARLA 仿真
    esac
done
# CI/无头：需要无客户端但仍跑 2c 时 export RUN_AUTO_CONNECT_VERIFY=1（仍需 DISPLAY 与 X11）
if [ "${RUN_AUTO_CONNECT_VERIFY:-0}" = "1" ]; then
    DO_MANUAL_ONLY=0
fi

ensure_client_dev_image

# 执行清理（如果启用）
if [ "$DO_CLEANUP" -eq 1 ]; then
    stop_and_cleanup_all
else
    echo -e "${YELLOW}跳过清理步骤（no-cleanup 模式）${NC}"
    echo ""
fi

start_all_nodes
if ! check_nodes_up; then
    echo -e "${RED}部分节点未就绪，请检查: docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml ps${NC}"
    exit 1
fi
echo -e "${GREEN}所有节点已运行。${NC}"
{
    echo "TELEOP_RUN_ID=${TELEOP_RUN_ID}"
    echo "started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host=$(hostname 2>/dev/null || true)"
} >"${TELEOP_LOGS_RUN_DIR}/SESSION.txt"
teleop_start_docker_log_followers
echo -e "${CYAN}  各容器 Docker 日志追加写入: ${TELEOP_LOGS_RUN_DIR}/docker-<服务名>.log${NC}"
if [ "${SKIP_CARLA:-0}" != "1" ]; then
    echo -e "${CYAN}  CARLA/Bridge 亦见: ${TELEOP_LOGS_RUN_DIR}/docker-carla-server.log（若已启动）${NC}"
fi
echo ""

# 自动在 Vehicle-side 容器内运行车端数据集本地校验（已挂载数据集则校验，未挂载则跳过）
ensure_vehicle_dataset_verified

# 若需要验证或启动客户端，检查中文字体和客户端编译状态（不强制安装/编译）
if [ "$DO_VERIFY" -eq 1 ] || [ "$DO_CLIENT" -eq 1 ]; then
    # 仅检查，不安装（镜像应已预装）
    ensure_client_chinese_font
    ensure_client_mosquitto_pub
    ensure_client_libgl_mesa_dev
    ensure_client_libxkbcommon_dev
    ensure_qsb_in_container
    
    # 强制编译客户端（如果设置了 no-build，则跳过编译，客户端启动时自动编译）
    if [ "$DO_BUILD" -eq 1 ]; then
        ensure_client_built  # 编译失败会直接退出（set -e）
    else
        echo -e "${YELLOW}跳过客户端编译步骤（no-build 模式），客户端将在启动时自动编译${NC}"
        echo -e "${YELLOW}⚠ 警告: 如果客户端编译失败，脚本将停止执行${NC}"
        echo ""
    fi
fi

STREAM_E2E_PID=""
STREAM_E2E_LOG=""
if [ "$DO_VERIFY" -eq 1 ]; then
    verify_chain || exit 1
    echo ""
    # 四路流验证动辄 60s+，同步执行会推迟 UI；Ctrl+C 还会让整个脚本退出导致窗口永远不启动 → 默认后台跑
    if [ "${STREAM_E2E_SYNC:-0}" = "1" ]; then
        echo -e "${CYAN}========== 2b. 四路流 E2E 验证（同步 STREAM_E2E_SYNC=1，会阻塞至完成）==========${NC}"
        STREAM_E2E_WAIT_MAX="${STREAM_E2E_WAIT_MAX:-}" bash "$SCRIPT_DIR/verify-stream-e2e.sh" || { echo -e "${YELLOW}四路流未就绪，将仍启动客户端供你手动点「连接车端」拉流。${NC}"; VERIFY_STREAM_FAILED=1; }
    else
        echo -e "${CYAN}========== 2b. 四路流 E2E 验证（后台运行，不阻塞客户端启动）==========${NC}"
        STREAM_E2E_LOG="${STREAM_E2E_LOG:-$(teleop_log_path_session stream-e2e)}"
        rm -f "$STREAM_E2E_LOG" "${STREAM_E2E_LOG}.rc" 2>/dev/null || true
        echo -e "  日志: ${STREAM_E2E_LOG} ；同步阻塞请设 ${YELLOW}STREAM_E2E_SYNC=1${NC}"
        (
            STREAM_E2E_WAIT_MAX="${STREAM_E2E_WAIT_MAX:-}" bash "$SCRIPT_DIR/verify-stream-e2e.sh" >"$STREAM_E2E_LOG" 2>&1
            echo $? >"${STREAM_E2E_LOG}.rc"
        ) &
        STREAM_E2E_PID=$!
    fi
    echo ""
    if [ "$DO_MANUAL_ONLY" -eq 0 ]; then
        echo -e "${CYAN}========== 2c. 连接功能自动化验证（客户端 18s 自动连接，Ctrl+C 可提前结束并停止所有容器）==========${NC}"
        echo -e "${YELLOW}提示: 2c 使用 CLIENT_AUTO_CONNECT_VIDEO=1，本窗口会跳过登录（仅自动测拉流）。随后第 3 步客户端固定 CLIENT_AUTO_CONNECT_VIDEO=0，从登录页开始。默认已跳过 2c；无需再传 ${CYAN}manual${NC}。"
        _x_ok=0
        if [ -n "${DISPLAY:-}" ]; then
            _dnum="${DISPLAY#*:}"
            _dnum="${_dnum%%.*}"
            [ -S "/tmp/.X11-unix/X${_dnum}" ] && _x_ok=1
        fi
        if [ "$_x_ok" -eq 0 ]; then
            if [ -S /tmp/.X11-unix/X0 ]; then export DISPLAY=:0; _x_ok=1
            elif [ -S /tmp/.X11-unix/X1 ]; then export DISPLAY=:1; _x_ok=1
            fi
        fi
        if [ "$_x_ok" -eq 1 ]; then
            bash "$SCRIPT_DIR/verify-connect-feature.sh" ; r=$?
            if [ "$r" -eq 130 ]; then
                echo -e "${YELLOW}已中断，正在停止所有容器...${NC}"
                wait_stream_e2e_background 2>/dev/null || true
                dc_all_down --remove-orphans 2>/dev/null || true
                exit 130
            fi
            [ "$r" -eq 0 ] || true
        else
            echo -e "${YELLOW}未检测到可用 X11 socket（:0 / :1），跳过 2c；将直接进入手动客户端。${NC}"
        fi
        if [ -n "${STREAM_E2E_PID:-}" ]; then
            echo -e "${GREEN}逐环节点验证已通过；四路流 E2E 在后台运行（日志: ${STREAM_E2E_LOG}）。关闭客户端后脚本会汇总该结果。${NC}"
        else
            [ -z "${VERIFY_STREAM_FAILED:-}" ] && echo -e "${GREEN}全链路自动化验证通过。${NC}" || true
        fi
        echo -e "${GREEN}所有节点已运行，可手动操作远程驾驶客户端。${NC}"
    else
        echo -e "${GREEN}已跳过 2c（默认从登录页完整测试）；需要自动化拉流验证请追加参数: ${CYAN}auto-connect${NC} 或 ${CYAN}RUN_AUTO_CONNECT_VERIFY=1${NC}（无客户端场景）。${NC}"
        echo ""
        echo -e "${CYAN}========== 2d. 底盘数据流详细验证（可选）==========${NC}"
        echo -e "${YELLOW}提示：${NC}如需验证底盘数据流，可运行："
        echo "  bash scripts/verify-chassis-data-display.sh"
        echo "  或在客户端连接后，查看右侧控制面板的「📊 车辆状态」区域"
        echo ""
        if [ -n "${STREAM_E2E_PID:-}" ]; then
            echo -e "${GREEN}逐环节点验证已通过；四路流 E2E 在后台运行（日志: ${STREAM_E2E_LOG}）。关闭客户端后脚本会汇总该结果。${NC}"
        else
            [ -z "${VERIFY_STREAM_FAILED:-}" ] && echo -e "${GREEN}全链路自动化验证通过。${NC}" || true
        fi
        echo -e "${GREEN}所有节点已运行，可手动操作远程驾驶客户端（登录 → 选车 → 连接车端）。${NC}"
    fi
    echo ""
else
    echo -e "${YELLOW}已跳过逐环体验证（no-verify）。${NC}"
    echo ""
fi

if [ "$DO_CLIENT" -eq 1 ]; then
    start_client
    wait_stream_e2e_background
else
    echo -e "${CYAN}未启动客户端（no-client）。所有节点已运行。手动启动远程驾驶客户端请执行：${NC}"
    echo "  xhost +local:docker"
    echo "  docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml exec -it -e DISPLAY=:0 -e CLIENT_AUTO_CONNECT_VIDEO=0 -e CLIENT_LOG_FILE=$(teleop_client_log_container_path) -e ZLM_VIDEO_URL=http://zlmediakit:80 -e MQTT_BROKER_URL=mqtt://teleop-mosquitto:1883 client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login'"
    wait_stream_e2e_background
fi
