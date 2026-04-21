#!/bin/bash
# 一键启动全链路并逐环体验证，最后启动客户端供界面操作
#
# 若仅需「与下述相同 compose/client-dev 方式」的一键自动化测试（L1+CTest+连接+可选 CARLA 四路）：
#   bash scripts/run-client-oneclick-all-tests.sh
#   WITH_CARLA=1 bash scripts/run-client-oneclick-all-tests.sh
#
# 链路：客户端 → 鉴权(Keycloak)/后端 → 选车/会话 → MQTT(start_stream) → 车端或 CARLA Bridge → 推流 → ZLM → 客户端拉流
# 本脚本：启动 Compose(dev Backend 8081) + 默认 CARLA 仿真(remote-driving/carla-with-bridge) → 验证 → 启动客户端
#
# 用法：
#   bash scripts/start-full-chain.sh              # 启动全链路 + 验证 + 从登录页启动客户端；默认要求 GPU（NVIDIA 挂载 + 硬件 GL）；无 GPU: TELEOP_GPU_OPTIONAL=1
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
# 环境变量（Qt / client-dev 容器内补装）：
#   CLIENT_QT_VERSION=6.8.0       Qt 版本（与镜像 /opt/Qt 路径一致）
#   CLIENT_QT_AQT_ARCH=linux_gcc_64  aqt install-qt 架构名（与 ensure_qsb 一致；list-qt 仍先试 gcc_64）
#
# 环境变量（client-dev 内 CMake / make；ensure_client_built 与 start_client 兜底编译）：
#   TELEOP_CLIENT_MAKE_JOBS=8           make -j 并行数；未设置或非正整数时默认宿主机 $(nproc)
#   TELEOP_CLIENT_CMAKE_BUILD_TYPE=Debug  传给 cmake -DCMAKE_BUILD_TYPE=...（如 Release、RelWithDebInfo）
#
# 重建 client-dev 镜像（利用 Docker 层缓存 + 可选 Qt 镜像源，避免每次 --no-cache 全量）：
#   export AQT_BASE=https://mirrors.tuna.tsinghua.edu.cn/qt/   # 可选，加速 aqt 下载
#   docker compose -f docker-compose.yml -f docker-compose.client-dev.yml build client-dev
#   仅当缓存异常时再：CLIENT_DEV_BUILD_NO_CACHE=1 docker compose ... build --no-cache client-dev
#
# 四路流 E2E（2b）：
#   默认后台运行，不阻塞客户端窗口；同步阻塞旧行为：STREAM_E2E_SYNC=1
#   缩短等待：STREAM_E2E_WAIT_MAX=30（传给 verify-stream-e2e.sh）
#
# 环境变量（日志）：
#   TELEOP_RUN_ID=my-run-1     自定义本会话子目录名（默认 YYYY-MM-DD-HHMMSS）；与 logs/<TELEOP_RUN_ID>/ 对应
#
# 环境变量（客户端视频可观测性 / 硬件解码，随「启动 GUI 客户端」注入 docker exec）：
#   TELEOP_CLIENT_VIDEO_DIAG_MINIMAL=1  关闭默认注入的 CLIENT_VIDEO_EVIDENCE_* / FrameSummary 等（减日志）
#   TELEOP_CLIENT_SKIP_HW_VIDEO_DECODE=1  注入 CLIENT_MEDIA_HARDWARE_DECODE=0 等（强制软解+可回退）
#
# 环境变量（2c 自动化，可选）：
#   RUN_AUTO_CONNECT_VERIFY=1  与传参 auto-connect 等效：在 no-client 等场景也尝试跑 verify-connect-feature（需 DISPLAY）
#
# 环境变量（远控台硬件 GL 门禁；默认开启，见下方 TELEOP_GPU_OPTIONAL）：
#   TELEOP_REQUIRE_HW_GL=1  启动客户端时传入 CLIENT_TELOP_STATION=1：非硬件 GL（如 llvmpipe）时进程拒绝启动（见 client_app_bootstrap / RUN_ENVIRONMENT.md）
#   默认：传参未带 no-client 时 TELEOP_REQUIRE_HW_GL=1（可用 TELEOP_REQUIRE_HW_GL=0 或 TELEOP_GPU_OPTIONAL=1 关闭）
#
# 环境变量（client-dev 使用宿主 NVIDIA + X11 cookie；默认开启）：
#   TELEOP_CLIENT_NVIDIA_GL=1  启动前强制执行 scripts/verify-client-nvidia-gl-prereqs.sh：
#     校验 Xauthority 存在且非空、DISPLAY（可 TELEOP_CLIENT_NVIDIA_GL_SKIP_DISPLAY_CHECK=1 跳过）、
#     docker run --gpus all（可 TELEOP_CLIENT_NVIDIA_GL_SKIP_DOCKER_GPU_TEST=1 跳过）、
#     compose 合并 config；自动选用 docker-compose.client-nvidia-gl.yml 或 .deploy.yml。
#   TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE  强制使用指定 overlay（相对仓库根，如 docker-compose.client-nvidia-gl.deploy.yml）
#   XAUTHORITY_HOST_PATH  显式指定 cookie 文件（否则按 XAUTHORITY → ~/.Xauthority 解析）
#
# 宿主有 NVIDIA 但 client-dev 未挂 GPU（默认启动 GUI 时直接失败退出）：
#   若宿主机 nvidia-smi -L 可见 GPU，而 teleop-client-dev 内无 /dev/nvidia0，本脚本在将启动客户端时 exit 1，
#   避免 llvmpipe+X11 客户区透底等环境问题。绕过（不推荐用于日常 UI）： TELEOP_ALLOW_CLIENT_WITHOUT_GPU=1
#   正确修复： TELEOP_CLIENT_NVIDIA_GL=1 bash scripts/start-full-chain.sh（或合并 docker-compose.client-nvidia-gl.yml）
#   注意：传参 no-client 时不做此检查（不启动 GUI）。
#
# 默认必须 GPU 硬件加速（客户端路径）：
#   未传 no-client 时默认 TELEOP_CLIENT_NVIDIA_GL=1 且 TELEOP_REQUIRE_HW_GL=1（容器挂 NVIDIA + 客户端拒绝软件光栅）。
#   无 NVIDIA / CI / 仅软件 GL： TELEOP_GPU_OPTIONAL=1
#   或分别关闭： TELEOP_CLIENT_NVIDIA_GL=0  TELEOP_REQUIRE_HW_GL=0（可与 TELEOP_ALLOW_CLIENT_WITHOUT_GPU=1 同用）
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

# ---------- 错误处理：一旦报错立即关闭所有 Docker 镜像 ----------
fail_and_cleanup() {
    local exit_code=$?
    local line_number=$1
    if [ $exit_code -ne 0 ]; then
        echo -e "\n${RED}══════════════════════════════════════════════════════════════════════${NC}"
        echo -e "${RED}脚本在第 $line_number 行执行失败 (退出码: $exit_code)${NC}"
        echo -e "${YELLOW}由于开启了「失败即清理」模式，正在关闭所有 Docker 容器...${NC}"
        
        # 停止可能正在后台运行的验证进程
        wait_stream_e2e_background 2>/dev/null || true
        
        # 彻底关闭所有 Compose 服务
        dc_all_down --remove-orphans 2>/dev/null || true
        
        echo -e "${GREEN}✓ 所有容器已停止。请根据上方日志修复问题后重试。${NC}"
        echo -e "${RED}══════════════════════════════════════════════════════════════════════${NC}\n"
    fi
    exit $exit_code
}
trap 'fail_and_cleanup $LINENO' ERR
# shellcheck source=lib/mqtt_control_json.sh
source "$SCRIPT_DIR/lib/mqtt_control_json.sh"

# ---------- 先解析影响 GPU 默认值的参数（须在 NVIDIA 预检与 dc_* 之前）----------
SKIP_CARLA="${SKIP_CARLA:-0}"
DO_VERIFY=1
DO_CLIENT=1
DO_MANUAL_ONLY=1
DO_CLEANUP=1
DO_BUILD=1
for arg in "$@"; do
    case "$arg" in
        no-verify)    DO_VERIFY=0 ;;
        no-client)    DO_CLIENT=0 ;;
        manual)       DO_MANUAL_ONLY=1 ;;
        auto-connect) DO_MANUAL_ONLY=0 ;;
        no-cleanup)   DO_CLEANUP=0 ;;
        cleanup)      DO_CLEANUP=1 ;;
        no-build)     DO_BUILD=0 ;;
        skip-carla)   SKIP_CARLA=1 ;;
    esac
done
if [ "${RUN_AUTO_CONNECT_VERIFY:-0}" = "1" ]; then
    DO_MANUAL_ONLY=0
fi

# 客户端容器内 cmake/make（ensure_client_built、start_client 兜底编译）
TELEOP_CLIENT_CMAKE_BUILD_TYPE="${TELEOP_CLIENT_CMAKE_BUILD_TYPE:-Debug}"
if [[ -n "${TELEOP_CLIENT_MAKE_JOBS:-}" ]] && [[ "${TELEOP_CLIENT_MAKE_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    :
else
    TELEOP_CLIENT_MAKE_JOBS="$(nproc)"
fi

# 默认要求 GPU：client-dev 挂 NVIDIA + 启动时硬件 GL 门禁（无 GPU/CI 用 TELEOP_GPU_OPTIONAL=1）
if [ "${TELEOP_GPU_OPTIONAL:-0}" != "1" ] && [ "$DO_CLIENT" -eq 1 ]; then
    : "${TELEOP_CLIENT_NVIDIA_GL:=1}"
    : "${TELEOP_REQUIRE_HW_GL:=1}"
fi

# 检查 Docker 是否注册了 nvidia runtime（用于 CARLA 仿真，docker-compose.carla.yml 依赖此名称）
# 若未注册且未设 CARLA_NO_GPU=1，自动降级为 nogpu 模式并给出警告
if [ "${SKIP_CARLA:-0}" != "1" ] && [ "${CARLA_NO_GPU:-0}" != "1" ]; then
    if ! docker info 2>/dev/null | grep -i "Runtimes:" | grep -qi "nvidia"; then
        echo -e "${YELLOW}警告: Docker 未注册 nvidia runtime，CARLA 容器将降级为无 GPU 模式 (CARLA_NO_GPU=1)${NC}"
        echo -e "      提示: 若需 GPU 仿真，请运行: sudo nvidia-ctk runtime configure --runtime=docker && sudo systemctl restart docker"
        export CARLA_NO_GPU=1
    fi
fi

# 与 start-all-nodes.sh 一致：dev backend 宿主机 8081；CARLA 为第四文件（可选 nogpu 覆盖）
dc_main() {
  local nvidia=()
  if [ "${TELEOP_CLIENT_NVIDIA_GL:-0}" = "1" ]; then
    : "${TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE:?TELEOP_CLIENT_NVIDIA_GL=1 但未设置 TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE（预检应已导出）}"
    nvidia+=( -f "${TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE}" )
  fi
  docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml \
    "${nvidia[@]}" "$@"
}
dc_carla() {
  local a=( -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml -f docker-compose.carla.yml )
  [ "${CARLA_NO_GPU:-0}" = "1" ] && a+=( -f docker-compose.carla.nogpu.yml )
  if [ "${TELEOP_CLIENT_NVIDIA_GL:-0}" = "1" ]; then
    : "${TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE:?TELEOP_CLIENT_NVIDIA_GL=1 但未设置 TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE（预检应已导出）}"
    a+=( -f "${TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE}" )
  fi
  docker compose "${a[@]}" "$@"
}
dc_all_down() {
  dc_carla down "$@"
}

# TELEOP_CLIENT_NVIDIA_GL=1：严格预检并导出 XAUTHORITY_HOST_PATH / TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE
if [ "${TELEOP_CLIENT_NVIDIA_GL:-0}" = "1" ]; then
  eval "$(bash "$SCRIPT_DIR/verify-client-nvidia-gl-prereqs.sh" --emit-export)"
fi

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
if [ "${TELEOP_GPU_OPTIONAL:-0}" != "1" ] && [ "${DO_CLIENT:-1}" -eq 1 ]; then
    echo -e "${GREEN}GPU 硬件加速（默认）：NVIDIA 挂载 + 硬件 GL 门禁；无 NVIDIA/CI 请设 ${CYAN}TELEOP_GPU_OPTIONAL=1${NC}"
fi

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
        echo -e "${GREEN}  ✓ carla-server 容器已启动${NC}"
        echo -e "${YELLOW}  等待 CARLA 仿真与 Bridge 初始化（约 30s）...${NC}"
        sleep 30

        # 核心改进：深度校验 CARLA 运行状态，防止带病启动
        local _logs
        _logs=$(docker logs carla-server 2>&1)
        
        # 1. 检查容器是否还在运行
        if ! docker ps --format '{{.Names}}' | grep -q '^carla-server$'; then
            echo -e "${RED}✗ CARLA 容器在初始化过程中崩溃退出！${NC}"
            echo -e "${CYAN}--- 容器最后 40 行日志 ---${NC}"
            echo "$_logs" | tail -40 | sed 's/^/    /'
            echo -e "${RED}══════════════════════════════════════════════════════════════════════${NC}"
            exit 1
        fi

        # 2. 检查日志中是否存在致命错误
        if echo "$_logs" | grep -qE "LowLevelFatalError|Segmentation fault|FATAL|链路中断致命错误|Refusing to run with the root privileges"; then
            echo -e "${RED}✗ 检测到 CARLA 仿真链致命错误！${NC}"
            local _error_line
            _error_line=$(echo "$_logs" | grep -E "LowLevelFatalError|Segmentation fault|FATAL|链路中断致命错误|Refusing to run with the root privileges" | tail -1)
            echo -e "${YELLOW}原因预判: ${_error_line}${NC}"
            if echo "$_logs" | grep -q "Refusing to run with the root privileges"; then
                echo -e "${YELLOW}💡 解决建议: 请检查 entrypoint.sh 是否错误地尝试以 root 身份运行 CARLA。${NC}"
            fi
            echo -e "${CYAN}--- 故障日志现场 ---${NC}"
            echo "$_logs" | grep -B 10 -A 5 -E "LowLevelFatalError|Segmentation fault|FATAL|链路中断致命错误|Refusing to run with the root privileges" | tail -20 | sed 's/^/    /'
            echo -e "${RED}══════════════════════════════════════════════════════════════════════${NC}"
            exit 1
        fi

        # 3. 检查 Bridge 是否已完成初始化（Python 或 C++）
        if ! echo "$_logs" | grep -qE "Python Bridge 启动入口|启动 C++ Bridge|阶段: CARLA 连接成功"; then
            echo -e "${YELLOW}⚠ 警告: CARLA 容器虽在运行，但未检测到 Bridge 成功启动关键字。${NC}"
            echo "$_logs" | tail -20 | sed 's/^/    /'
            # 此处不一定 exit，有些环境下日志输出可能延迟，交给后续 verify_chain 兜底
        fi

        echo -e "${GREEN}  ✓ CARLA 仿真链初始化验证通过${NC}"
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
            echo -e "${RED}FATAL: CARLA 仿真链启动失败。${NC}"
            echo -e "${YELLOW}请检查上述日志排查根本原因（GPU/驱动、文件描述符限制、代码语法等）。${NC}"
            false # 触发 fail_and_cleanup
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

# ---------- 宿主有 NVIDIA 时强制 client-dev 已挂 GPU（避免容器内 llvmpipe + X11 透底）----------
# 依赖：teleop-client-dev 已运行；dc_main 与当前 TELEOP_CLIENT_NVIDIA_GL / compose 一致。
assert_client_dev_gpu_when_host_has_nvidia() {
    if [ "${TELEOP_ALLOW_CLIENT_WITHOUT_GPU:-0}" = "1" ]; then
        return 0
    fi
    if [ "${DO_CLIENT:-1}" != "1" ]; then
        return 0
    fi
    if ! command -v nvidia-smi >/dev/null 2>&1; then
        return 0
    fi
    if ! nvidia-smi -L 2>/dev/null | grep -qE '^GPU[[:space:]]+[0-9]+:'; then
        return 0
    fi
    if ! docker ps --format '{{.Names}}' 2>/dev/null | grep -q '^teleop-client-dev$'; then
        return 0
    fi
    if dc_main exec -T client-dev test -c /dev/nvidia0 2>/dev/null; then
        return 0
    fi

    echo -e "${RED}══════════════════════════════════════════════════════════════════════${NC}"
    echo -e "${RED}错误: 宿主机 nvidia-smi 可见 GPU，但 teleop-client-dev 容器内不存在 /dev/nvidia0。${NC}"
    echo -e "${RED}      客户端将在容器内走 Mesa 软件光栅，易出现「仅窗框、客户区像宿桌面」等问题。${NC}"
    echo ""
    echo -e "${CYAN}修复（推荐）:${NC}"
    echo "  TELEOP_CLIENT_NVIDIA_GL=1 bash scripts/start-full-chain.sh"
    echo "  或先: eval \"\$(bash scripts/verify-client-nvidia-gl-prereqs.sh --emit-export)\""
    echo "  再合并 -f \"\${TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE}\" 启动 client-dev（见 docker-compose.client-nvidia-gl.yml 头注释）。"
    echo ""
    echo -e "${YELLOW}若确知无需 GPU（如仅 CI / 无界面调试）:${NC} TELEOP_ALLOW_CLIENT_WITHOUT_GPU=1"
    echo -e "${YELLOW}说明: 传参 no-client 时不做本检查。${NC}"
    echo -e "${RED}══════════════════════════════════════════════════════════════════════${NC}"
    exit 1
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

    # 2.4b ZLM_SECRET 验证（API 调用需要）
    echo -n "  [2.4b] ZLM API 认证 ... "
    if [ -n "${ZLM_SECRET:-}" ]; then
        local zlm_secret_check=$(curl -sf "${ZLM_URL}/index/api/getServerConfig?secret=${ZLM_SECRET}" 2>/dev/null || echo "")
        if [ -n "$zlm_secret_check" ]; then
            echo -e "${GREEN}✓${NC} (ZLM_SECRET 验证通过)"
        else
            echo -e "${YELLOW}⚠${NC} (ZLM_SECRET 已配置但 API 调用失败，可能是权限问题)"
        fi
    else
        echo -e "${YELLOW}⚠${NC} (ZLM_SECRET 未设置，部分 API 可能受限)"
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
    msg="$(mqtt_json_start_stream "${stream_vin}")"
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
    if dc_main exec -T -u root client-dev bash -c 'sed -i "s@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g" /etc/apt/sources.list && sed -i "s/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g" /etc/apt/sources.list && apt-get update -qq && apt-get install -y -qq --no-install-recommends mosquitto-clients && rm -rf /var/lib/apt/lists/*' 2>/dev/null; then
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
        'sed -i "s@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g" /etc/apt/sources.list && sed -i "s/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g" /etc/apt/sources.list && apt-get update -qq && apt-get install -y -qq --no-install-recommends libgl1-mesa-dev && rm -rf /var/lib/apt/lists/*' 2>/dev/null; then
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

# ---------- 确保 client-dev 内有 mesa-utils（glxinfo，供客户端 DisplayPolicy 启动前检测 GL 栈）----------
ensure_client_mesa_utils() {
    if dc_main exec -T client-dev bash -c 'command -v glxinfo >/dev/null 2>&1' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} mesa-utils 已安装（glxinfo 可用）"
        return 0
    fi
    echo -e "${YELLOW}容器内未检测到 glxinfo（mesa-utils），正在以 root 安装 mesa-utils...${NC}"
    if dc_main exec -T -u root client-dev bash -c \
        'sed -i "s@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g" /etc/apt/sources.list && sed -i "s/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g" /etc/apt/sources.list && apt-get update -qq && apt-get install -y -qq --no-install-recommends mesa-utils && rm -rf /var/lib/apt/lists/*' 2>/dev/null; then
        if dc_main exec -T client-dev bash -c 'command -v glxinfo >/dev/null 2>&1' 2>/dev/null; then
            echo -e "${GREEN}✓${NC} mesa-utils 已安装，glxinfo 可用"
            return 0
        fi
    fi
    echo -e "${YELLOW}⚠${NC} mesa-utils 安装失败或 glxinfo 仍不可用（客户端将仅用 nvidia-smi / 保守回退选择 GL 栈；可检查网络或手动: apt-get install mesa-utils）${NC}"
    echo ""
    return 0
}

# ---------- 增强环境检查：X11 / WSLg / Qt 平台插件兼容性 ----------
# 这些检查确保 Qt 应用能稳定运行在当前图形环境中

# ---------- 检查 WSLg 状态（WSL 环境下运行） ----------
check_wslg_status() {
    echo -e "${CYAN}========== X11 环境：WSLg 状态检查 ==========${NC}"
    
    # 检测是否在 WSL 环境下运行
    if [ -f /proc/version ] && grep -qi "microsoft\|wsl" /proc/version 2>/dev/null; then
        echo -e "${GREEN}✓${NC} 检测到 WSL 环境"
        
        # 检查 WSL 版本
        if command -v wsl.exe &>/dev/null || command -v wsl &>/dev/null; then
            local wsl_version=""
            if command -v wsl.exe &>/dev/null; then
                wsl_version=$(wsl.exe --version 2>/dev/null | head -1 || echo "未知")
            else
                wsl_version=$(wsl --version 2>/dev/null | head -1 || echo "未知")
            fi
            echo -e "  ${GREEN}WSL 版本:${NC} $wsl_version"
        fi
        
        # 检查 WSLg 是否运行
        if [ -d /mnt/wslg ]; then
            echo -e "${GREEN}✓${NC} WSLg 目录存在（/mnt/wslg）"
        elif pgrep -x "wslg" >/dev/null 2>&1 || pgrep -f "systemd" >/dev/null 2>&1; then
            echo -e "${GREEN}✓${NC} WSLg 进程正在运行"
        else
            echo -e "${YELLOW}⚠${NC} 未检测到 WSLg，可能使用传统 X11 模式"
        fi
        
        # 检查 Wayland 兼容性
        if [ -S "/mnt/wslg/wayland-0" ]; then
            echo -e "${GREEN}✓${NC} Wayland socket 存在（/mnt/wslg/wayland-0）"
        else
            echo -e "${YELLOW}⚠${NC} 未检测到 Wayland socket"
        fi
        
        echo -e "${GREEN}WSLg 环境检测完成${NC}"
        echo ""
        return 0
    else
        echo -e "${GREEN}✓${NC} 非 WSL 环境，跳过 WSLg 检查"
        echo ""
        return 0
    fi
}

# ---------- X11 socket 可用性验证（基础检查） ----------
check_x11_socket() {
    echo -e "${CYAN}========== X11 环境：Socket 可用性验证 ==========${NC}"
    local failed=0
    
    # 检查标准 X11 socket
    if [ -S /tmp/.X11-unix/X0 ]; then
        echo -e "${GREEN}✓${NC} X11 socket :0 存在"
        local x0_info=$(stat -c "权限: %a 所有者: %U:%G" /tmp/.X11-unix/X0 2>/dev/null || echo "未知")
        echo -e "  $x0_info"
    elif [ -S /tmp/.X11-unix/X1 ]; then
        echo -e "${GREEN}✓${NC} X11 socket :1 存在"
        local x1_info=$(stat -c "权限: %a 所有者: %U:%G" /tmp/.X11-unix/X1 2>/dev/null || echo "未知")
        echo -e "  $x1_info"
    else
        echo -e "${RED}✗${NC} 未找到 X11 socket（:0 或 :1）"
        failed=1
    fi
    
    # 检查 DISPLAY 环境变量
    if [ -n "${DISPLAY:-}" ]; then
        echo -e "${GREEN}✓${NC} DISPLAY 环境变量: $DISPLAY"
    else
        echo -e "${YELLOW}⚠${NC} DISPLAY 环境变量未设置"
    fi
    
    # 检查 X11 授权文件
    if [ -n "${XAUTHORITY:-}" ] && [ -f "$XAUTHORITY" ]; then
        echo -e "${GREEN}✓${NC} X11 授权文件存在: $XAUTHORITY"
    elif [ -f ~/.Xauthority ]; then
        echo -e "${GREEN}✓${NC} X11 授权文件存在于 ~/.Xauthority"
    else
        echo -e "${YELLOW}⚠${NC} 未找到 X11 授权文件（可能无需授权）"
    fi
    
    if [ $failed -eq 1 ]; then
        echo -e "${RED}✗ X11 socket 不可用${NC}"
    else
        echo -e "${GREEN}✓ X11 socket 可用性验证通过${NC}"
    fi
    echo ""
    return $failed
}

# ---------- X11 连接稳定性测试（主���探测） ----------
test_x11_connection() {
    echo -e "${CYAN}========== X11 环境：连接稳定性测试 ==========${NC}"
    local failed=0
    
    # 方法 1：使用 xdpyinfo 测试 X11 连接
    if command -v xdpyinfo &>/dev/null; then
        echo -n "  [测试 1] xdpyinfo 连接测试 ... "
        if timeout 5 xdpyinfo -display "${DISPLAY:-:0}" >/dev/null 2>&1; then
            echo -e "${GREEN}✓${NC}"
        else
            echo -e "${YELLOW}⚠${NC} xdpyinfo 测试失败（Xdpyinfo 可能未安装）"
        fi
    else
        echo -e "${YELLOW}  [测试 1] xdpyinfo 未安装，跳过${NC}"
    fi
    
    # 方法 2：使用 xauth 测试 X11 连接
    if command -v xauth &>/dev/null; then
        echo -n "  [测试 2] xauth 读取测试 ... "
        if timeout 5 xauth list "${DISPLAY:-:0}" >/dev/null 2>&1; then
            echo -e "${GREEN}✓${NC}"
        else
            echo -e "${YELLOW}⚠${NC} xauth 测试失败"
        fi
    else
        echo -e "${YELLOW}  [测试 2] xauth 未安装，跳过${NC}"
    fi
    
    # 方法 3：使用 xset 测试 X11 连接
    if command -v xset &>/dev/null; then
        echo -n "  [测试 3] xset 查询测试 ... "
        if timeout 5 xset -q >/dev/null 2>&1; then
            echo -e "${GREEN}✓${NC}"
        else
            echo -e "${YELLOW}⚠${NC} xset 测试失败"
        fi
    else
        echo -e "${YELLOW}  [测试 3] xset 未安装，跳过${NC}"
    fi
    
    # 方法 4：使用 xwininfo 测试 X11 连接
    if command -v xwininfo &>/dev/null; then
        echo -n "  [测试 4] xwininfo 测试 ... "
        if timeout 5 xwininfo -root -stats "${DISPLAY:-:0}" >/dev/null 2>&1; then
            echo -e "${GREEN}✓${NC}"
        else
            echo -e "${YELLOW}⚠${NC} xwininfo 测试失败"
        fi
    else
        echo -e "${YELLOW}  [测试 4] xwininfo 未安装，跳过${NC}"
    fi
    
    # 方法 5：TCP socket 直接测试
    echo -n "  [测试 5] X11 TCP 端口探测 ... "
    local display_num="${DISPLAY#*:}"
    display_num="${display_num%%.*}"
    if [ -n "$display_num" ] && [ "$display_num" -ge 0 ] 2>/dev/null; then
        local x_port=$((6000 + display_num))
        if timeout 2 bash -c "echo >/dev/tcp/localhost/$x_port" 2>/dev/null; then
            echo -e "${GREEN}✓${NC} X11 端口 :$display_num ($x_port) 可达"
        else
            echo -e "${YELLOW}⚠${NC} X11 端口 :$display_num ($x_port) 不可达（可能仅使用 Unix socket）"
        fi
    else
        echo -e "${YELLOW}⚠${NC} 无法解析 DISPLAY 端口"
    fi
    
    echo -e "${GREEN}✓ X11 连接稳定性测试完成${NC}"
    echo ""
    return 0
}

# ---------- Qt 平台插件兼容性测试 ----------
test_qt_platform_plugins() {
    echo -e "${CYAN}========== X11 环境：Qt 平台插件兼容性测试 ==========${NC}"
    local platform="${QT_QPA_PLATFORM:-xcb}"
    echo "测试 Qt 平台插件: $platform"
    
    # 检查 Qt QPA 平台插件
    local qt_plugin_path=""
    
    # 尝试在容器中检查 Qt 平台插件
    if docker ps --format '{{.Names}}' | grep -q '^teleop-client-dev$' 2>/dev/null; then
        qt_plugin_path=$(dc_main exec -T client-dev bash -c '
            if [ -d "/opt/Qt/6.8.0/gcc_64/plugins/platforms" ]; then
                echo "/opt/Qt/6.8.0/gcc_64/plugins/platforms"
            elif [ -d "$QT_ROOT/plugins/platforms" ]; then
                echo "$QT_ROOT/plugins/platforms"
            fi
        ' 2>/dev/null || echo "")
    fi
    
    if [ -n "$qt_plugin_path" ]; then
        echo -e "  Qt 平台插件目录: $qt_plugin_path"
        
        # 检查 xcb 插件
        if [ -f "$qt_plugin_path/libqxcb.so" ] 2>/dev/null || dc_main exec -T client-dev bash -c "test -f $qt_plugin_path/libqxcb.so" 2>/dev/null; then
            echo -e "${GREEN}✓${NC} libqxcb.so 存在（XCB 平台插件）"
        else
            echo -e "${YELLOW}⚠${NC} libqxcb.so 不存在"
        fi
        
        # 检查 wayland 插件
        if dc_main exec -T client-dev bash -c "test -f $qt_plugin_path/libqwayland-generic.so" 2>/dev/null; then
            echo -e "${GREEN}✓${NC} libqwayland-generic.so 存在（Wayland 平台插件）"
        fi
        
        # 检查 offscreen 插件
        if dc_main exec -T client-dev bash -c "test -f $qt_plugin_path/libqoffscreen.so" 2>/dev/null; then
            echo -e "${GREEN}✓${NC} libqoffscreen.so 存在（Offscreen 平台插件）"
        fi
    else
        echo -e "${YELLOW}⚠${NC} 未找到 Qt 平台插件目录（容器可能未运行）"
    fi
    
    # 检查 Qt 平台插件所需的库依赖
    echo -e "${CYAN}检查 Qt 平台插件依赖：${NC}"
    
    local required_libs=("libxcb" "libxcb-glx" "libxkbcommon" "libx11")
    for lib in "${required_libs[@]}"; do
        if ldconfig -p 2>/dev/null | grep -q "$lib"; then
            echo -e "${GREEN}✓${NC} $lib 已安装"
        else
            echo -e "${YELLOW}⚠${NC} $lib 未找到（可能不影响运行）"
        fi
    done
    
    # 测试不同平台插件的适用性
    echo -e "${CYAN}平台插件适用性分析：${NC}"
    
    # 检测 Wayland 环境
    if [ -n "$WAYLAND_DISPLAY" ] || [ -S "/run/user/$(id -u)/wayland-0" ]; then
        echo -e "${GREEN}✓${NC} Wayland 环境检测到"
        echo -e "  建议: 使用 QT_QPA_PLATFORM=wayland"
    fi
    
    # 检测 WSLg 环境
    if [ -d /mnt/wslg ]; then
        echo -e "${GREEN}✓${NC} WSLg 环境检测到"
        echo -e "  建议: 可尝试 QT_QPA_PLATFORM=wayland 或 xcb"
    fi
    
    # 检测虚拟化环境
    if [ -d /dev/dri ] && ls /dev/dri/* 2>/dev/null | grep -q "render"; then
        echo -e "${GREEN}✓${NC} GPU 加速可用（/dev/dri）"
        echo -e "  建议: 客户端会自动选择 GL 栈；硬件优先需可用的 DRI/EGL（见 [Client][GLProbe]）"
    else
        echo -e "${YELLOW}⚠${NC} 未检测到 GPU 加速（软件渲染模式）"
    fi
    
    echo -e "${GREEN}✓ Qt 平台插件兼容性测试完成${NC}"
    echo ""
    return 0
}

# ---------- 综合 X11 环境健康检查（启动前必须通过） ----------
check_x11_environment() {
    echo -e "${CYAN}======================================================================"
    echo "                     X11 环境综合健康检查"
    echo -e "======================================================================${NC}"
    echo ""
    
    local check_failed=0
    
    # 1. WSLg 状态检查
    if ! check_wslg_status; then
        check_failed=1
    fi
    
    # 2. X11 socket 可用性验证
    if ! check_x11_socket; then
        check_failed=1
    fi
    
    # 3. X11 连接稳定性测试
    if ! test_x11_connection; then
        check_failed=1
    fi
    
    # 4. Qt 平台插件兼容性测试
    if ! test_qt_platform_plugins; then
        check_failed=1
    fi
    
    echo -e "${CYAN}======================================================================"
    echo "                     X11 环境检查汇总"
    echo -e "======================================================================${NC}"
    
    if [ $check_failed -eq 1 ]; then
        echo -e "${RED}✗ X11 环境存在严重问题，客户端可能无法正常运行${NC}"
        echo ""
        echo -e "${YELLOW}建议：${NC}"
        echo "  1. 检查 WSL 版本：wsl --version"
        echo "  2. 更新 WSL：wsl --update"
        echo "  3. 重启 WSL：wsl --shutdown"
        echo "  4. 尝试使用不同的 Qt 平台插件（如 wayland 或 offscreen）"
        echo "  5. 在独立终端中运行客户端"
        echo ""
        return 1
    else
        echo -e "${GREEN}✓ X11 环境检查通过，图形环境可用${NC}"
        echo ""
        return 0
    fi
}

# ---------- 环境变量完整性检查 ----------
verify_environment_config() {
    echo -e "${CYAN}========== 环境变量完整性检查 ==========${NC}"
    local failed=0
    local warnings=0
    
    # 核心环境变量（缺少会导致功能失效）
    echo -e "${CYAN}核心环境变量：${NC}"
    
    if [ -n "${BACKEND_URL:-}" ]; then
        echo -e "${GREEN}✓${NC} BACKEND_URL=${BACKEND_URL}"
    else
        echo -e "${RED}✗${NC} BACKEND_URL 未设置（将使用默认值 http://127.0.0.1:8081）"
        failed=1
    fi
    
    if [ -n "${ZLM_URL:-}" ]; then
        echo -e "${GREEN}✓${NC} ZLM_URL=${ZLM_URL}"
    else
        echo -e "${YELLOW}⚠${NC} ZLM_URL 未设置（将使用默认值 http://127.0.0.1:80）"
        warnings=$((warnings + 1))
    fi
    
    if [ -n "${ZLM_SECRET:-}" ]; then
        echo -e "${GREEN}✓${NC} ZLM_SECRET 已配置（已设置）"
    else
        echo -e "${YELLOW}⚠${NC} ZLM_SECRET 未设置（ZLM API 可能调用失败）"
        warnings=$((warnings + 1))
    fi
    
    if [ -n "${CARLA_VIN:-}" ]; then
        echo -e "${GREEN}✓${NC} CARLA_VIN=${CARLA_VIN}"
    else
        echo -e "${YELLOW}⚠${NC} CARLA_VIN 未设置（将使用默认值 carla-sim-001）"
    fi
    
    if [ -n "${DISPLAY:-}" ]; then
        echo -e "${GREEN}✓${NC} DISPLAY=${DISPLAY}"
    else
        echo -e "${YELLOW}⚠${NC} DISPLAY 未设置（Qt 应用可能无法显示）"
        warnings=$((warnings + 1))
    fi
    
    if [ -n "${MQTT_BROKER_URL:-}" ]; then
        echo -e "${GREEN}✓${NC} MQTT_BROKER_URL=${MQTT_BROKER_URL}"
    else
        echo -e "${YELLOW}⚠${NC} MQTT_BROKER_URL 未设置（将使用默认值 mqtt://teleop-mosquitto:1883）"
    fi
    
    echo ""
    echo -e "${CYAN}Qt 运行时环境变量：${NC}"
    
    local qt_vars=("QT_QPA_PLATFORM" "LIBGL_ALWAYS_SOFTWARE" "QT_XCB_GL_INTEGRATION" "QT_LOGGING_RULES")
    for var in "${qt_vars[@]}"; do
        local val="${!var}"
        if [ -n "$val" ]; then
            echo -e "${GREEN}✓${NC} $var=${val}"
        else
            echo -e "${YELLOW}⚠${NC} $var 未设置（将使用 Qt 默认值）"
        fi
    done
    
    echo ""
    echo -e "${CYAN}客户端特定环境变量：${NC}"
    
    if [ -n "${CLIENT_LOG_FILE:-}" ]; then
        echo -e "${GREEN}✓${NC} CLIENT_LOG_FILE=${CLIENT_LOG_FILE}"
    else
        echo -e "${YELLOW}⚠${NC} CLIENT_LOG_FILE 未设置（日志将写入默认位置）"
    fi
    
    if [ -n "${CLIENT_RESET_LOGIN:-}" ]; then
        echo -e "${GREEN}✓${NC} CLIENT_RESET_LOGIN=${CLIENT_RESET_LOGIN}"
    else
        echo -e "${YELLOW}⚠${NC} CLIENT_RESET_LOGIN 未设置（将使用默认行为）"
    fi
    
    echo ""
    if [ $failed -eq 1 ]; then
        echo -e "${RED}✗ 核心环境变量缺失，客户端可能无法正常运行${NC}"
        return 1
    elif [ $warnings -gt 0 ]; then
        echo -e "${YELLOW}⚠ 环境变量配置不完整，存在 $warnings 个警告${NC}"
        echo -e "${YELLOW}  部分功能可能受影响，但不影响基本运行${NC}"
        return 0
    else
        echo -e "${GREEN}✓ 环境变量配置完整${NC}"
        return 0
    fi
}

# ---------- 宿主机 glxinfo（OpenGL 渲染器探测；缺则尝试安装 mesa-utils 等）----------
ensure_host_glxinfo() {
    if command -v glxinfo &>/dev/null; then
        return 0
    fi
    echo -e "${YELLOW}宿主机未检测到 glxinfo，尝试安装（用于 OpenGL 渲染器测试）...${NC}"
    local installed=0
    if command -v apt-get &>/dev/null; then
        if [ "$(id -u)" -eq 0 ]; then
            DEBIAN_FRONTEND=noninteractive apt-get update -qq &&
                DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends mesa-utils &&
                installed=1 || true
        elif command -v sudo &>/dev/null; then
            sudo env DEBIAN_FRONTEND=noninteractive apt-get update -qq &&
                sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends mesa-utils &&
                installed=1 || true
        fi
    elif command -v dnf &>/dev/null; then
        if [ "$(id -u)" -eq 0 ]; then
            dnf install -y glx-utils && installed=1 || true
        elif command -v sudo &>/dev/null; then
            sudo dnf install -y glx-utils && installed=1 || true
        fi
    elif command -v yum &>/dev/null; then
        if [ "$(id -u)" -eq 0 ]; then
            yum install -y glx-utils && installed=1 || true
        elif command -v sudo &>/dev/null; then
            sudo yum install -y glx-utils && installed=1 || true
        fi
    elif command -v pacman &>/dev/null; then
        if [ "$(id -u)" -eq 0 ]; then
            pacman -Sy --noconfirm mesa-utils && installed=1 || true
        elif command -v sudo &>/dev/null; then
            sudo pacman -Sy --noconfirm mesa-utils && installed=1 || true
        fi
    fi
    if command -v glxinfo &>/dev/null; then
        echo -e "${GREEN}✓${NC} glxinfo 已可用"
        return 0
    fi
    if [ "$installed" -eq 1 ]; then
        echo -e "${YELLOW}已尝试安装包，但 glxinfo 仍不可用，跳过 OpenGL 渲染器测试${NC}"
    else
        echo -e "${YELLOW}无法自动安装（需 root/sudo 或未识别包管理器）；Debian/Ubuntu: ${CYAN}sudo apt-get install -y mesa-utils${NC}；跳过 OpenGL 渲染器测试${NC}"
    fi
    return 0
}

# ---------- 验证 Qt + OpenGL + Mesa 完整可用 ----------
verify_client_rendering_stack() {
    echo -e "${CYAN}========== 客户端渲染栈验证（Qt + OpenGL + Mesa）==========${NC}"
    local failed=0
    
    # 检查容器是否运行
    if ! docker ps --format '{{.Names}}' | grep -q '^teleop-client-dev$' 2>/dev/null; then
        echo -e "${YELLOW}⚠${NC} teleop-client-dev 容器未运行，跳过渲染栈验证"
        return 0
    fi
    
    echo -e "${CYAN}1. Qt 运行时验证：${NC}"
    
    # 检查 Qt 版本
    local qt_version=$(dc_main exec -T client-dev bash -c '/opt/Qt/6.8.0/gcc_64/bin/qmake --version 2>/dev/null | head -1' 2>/dev/null || echo "")
    if [ -n "$qt_version" ]; then
        echo -e "${GREEN}✓${NC} Qt 版本: $qt_version"
    else
        echo -e "${RED}✗${NC} Qt qmake 不可用"
        failed=1
    fi
    
    # 检查 Qt 核心库（Qt 库安装在 /opt/Qt 下，需要直接检查文件）
    local qt_lib_path="/opt/Qt/6.8.0/gcc_64/lib"
    local qt_libs=("libQt6Core.so.6" "libQt6Gui.so.6" "libQt6Quick.so.6" "libQt6Qml.so.6")
    echo -e "${CYAN}  检查 Qt 库目录: $qt_lib_path${NC}"
    
    for lib in "${qt_libs[@]}"; do
        # 使用通配符匹配，因为 Qt 库可能有版本后缀
        if dc_main exec -T client-dev bash -c "ls ${qt_lib_path}/${lib}* 2>/dev/null | head -1 | grep -q '.'" 2>/dev/null; then
            echo -e "${GREEN}✓${NC} $lib*"
        else
            echo -e "${RED}✗${NC} $lib 缺失"
            failed=1
        fi
    done
    
    # 检查 Qt 插件目录
    echo -e "${CYAN}  检查 Qt 插件目录:${NC}"
    if dc_main exec -T client-dev bash -c 'test -d /opt/Qt/6.8.0/gcc_64/plugins' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} 插件目录存在"
        local plugin_count=$(dc_main exec -T client-dev bash -c 'ls /opt/Qt/6.8.0/gcc_64/plugins/*/ 2>/dev/null | wc -l' 2>/dev/null || echo "0")
        echo -e "  插件数量: $plugin_count"
    fi
    
    echo ""
    echo -e "${CYAN}2. OpenGL 验证：${NC}"
    
    ensure_host_glxinfo
    
    # 检查 OpenGL 库
    if dc_main exec -T client-dev bash -c 'ldconfig -p 2>/dev/null | grep -q "libOpenGL"' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} libOpenGL 可用"
    else
        echo -e "${YELLOW}⚠${NC} libOpenGL 未找到（可能使用 Mesa 软件渲染）"
    fi
    
    # 检查 libGL
    if dc_main exec -T client-dev bash -c 'ldconfig -p 2>/dev/null | grep -q "libGL"' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} libGL 可用"
    else
        echo -e "${YELLOW}⚠${NC} libGL 未找到"
    fi
    
    # 检查 OpenGL 上下文能力（使用 glxinfo）
    if command -v glxinfo &>/dev/null; then
        echo -n "  OpenGL 渲染器测试 ... "
        local gl_renderer=$(glxinfo 2>/dev/null | grep "OpenGL renderer" | head -1 || echo "")
        if [ -n "$gl_renderer" ]; then
            echo -e "${GREEN}✓${NC}"
            echo -e "    $gl_renderer"
        else
            echo -e "${YELLOW}⚠${NC} 无法获取 OpenGL 渲染器信息"
        fi
    else
        echo -e "${YELLOW}  glxinfo 未安装，跳过 OpenGL 渲染器测试${NC}"
    fi
    
    echo ""
    echo -e "${CYAN}3. Mesa 软件渲染验证：${NC}"
    
    # 检查 Mesa llvmpipe
    if dc_main exec -T client-dev bash -c 'ldconfig -p 2>/dev/null | grep -q "llvmpipe\|softpipe"' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Mesa 软件渲染器（llvmpipe/softpipe）可用"
    else
        echo -e "${YELLOW}⚠${NC} 未检测到 Mesa 软件渲染器"
    fi
    
    # 检查 Mesa DRI 驱动
    if [ -d /usr/lib/x86_64-linux-gnu/dri ] 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Mesa DRI 驱动目录存在"
        local dri_count=$(ls /usr/lib/x86_64-linux-gnu/dri/*.so 2>/dev/null | wc -l)
        echo -e "  驱动数量: $dri_count"
    fi
    
    # 检查 software renderer
    if [ -f /usr/lib/x86_64-linux-gnu/dri/lavapipe_dri.so ] 2>/dev/null; then
        echo -e "${GREEN}✓${NC} lavapipe 软件渲染器可用"
    fi
    
    echo ""
    echo -e "${CYAN}4. Qt 平台插件验证：${NC}"
    
    # 检查 XCB 插件
    if dc_main exec -T client-dev bash -c 'test -f /opt/Qt/6.8.0/gcc_64/plugins/platforms/libqxcb.so' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} libqxcb.so（XCB 平台插件）"
    else
        echo -e "${RED}✗${NC} libqxcb.so 缺失（XCB 平台插件）"
        failed=1
    fi
    
    # 检查 OpenGL 插件
    if dc_main exec -T client-dev bash -c 'test -f /opt/Qt/6.8.0/gcc_64/plugins/platforms/libqeglfs.so' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} libqeglfs.so（EGL 平台插件）"
    fi
    
    echo ""
    if [ $failed -eq 1 ]; then
        echo -e "${RED}✗ 渲染栈验证未通过，客户端可能无法正常渲染${NC}"
        return 1
    else
        echo -e "${GREEN}✓ 渲染栈验证通过${NC}"
        return 0
    fi
}

# ---------- 验证客户端可执行文件存在 ----------
verify_client_binary() {
    echo -e "${CYAN}========== 客户端可执行文件验证 ==========${NC}"
    
    # 检查 /tmp/client-build 中的可执行文件
    if dc_main exec -T client-dev bash -c 'test -x /tmp/client-build/RemoteDrivingClient' 2>/dev/null; then
        local binary_size=$(dc_main exec -T client-dev bash -c 'stat -c %s /tmp/client-build/RemoteDrivingClient 2>/dev/null' 2>/dev/null || echo "0")
        local build_time=$(dc_main exec -T client-dev bash -c 'stat -c %y /tmp/client-build/RemoteDrivingClient 2>/dev/null | cut -d"." -f1' 2>/dev/null || echo "未知")
        echo -e "${GREEN}✓${NC} 可执行文件存在: /tmp/client-build/RemoteDrivingClient"
        echo -e "  大小: $(numfmt --to=iec-i --suffix=B $binary_size 2>/dev/null || echo "${binary_size} bytes")"
        echo -e "  编译时间: $build_time"
        
        # 检查依赖库完整性
        echo -n "  依赖库检查 ... "
        if dc_main exec -T client-dev bash -c 'ldd /tmp/client-build/RemoteDrivingClient 2>&1 | grep -q "not found"' 2>/dev/null; then
            echo -e "${RED}✗${NC} 存在缺失的依赖库"
            dc_main exec -T client-dev bash -c 'ldd /tmp/client-build/RemoteDrivingClient 2>&1 | grep "not found"' 2>/dev/null
            return 1
        else
            echo -e "${GREEN}✓${NC} 所有依赖库完整"
        fi
        
        return 0
    else
        echo -e "${RED}✗${NC} 可执行文件 /tmp/client-build/RemoteDrivingClient 不存在或不可执行"
        
        # 提示可能的原因
        echo -e "${YELLOW}可能的原因：${NC}"
        echo "  1. 编译失败，请检查编译日志"
        echo "  2. 编译正在进行中"
        echo "  3. 镜像构建问题"
        
        return 1
    fi
}

# ---------- 容器健康检查（等待进程启动） ----------
verify_client_container_health() {
    echo -e "${CYAN}========== 客户端容器健康检查 ==========${NC}"
    
    # 检查容器是否运行
    local container_name="teleop-client-dev"
    if ! docker ps --format '{{.Names}}' | grep -q "^${container_name}$" 2>/dev/null; then
        echo -e "${RED}✗${NC} 容器 $container_name 未运行"
        return 1
    fi
    
    echo -e "${GREEN}✓${NC} 容器 $container_name 正在运行"
    
    # 检查容器健康状态
    local health_status=$(docker inspect --format='{{.State.Health.Status}}' "$container_name" 2>/dev/null || echo "none")
    if [ "$health_status" != "none" ]; then
        echo -e "  健康状态: $health_status"
    fi
    
    # 检查容器启动时间
    local started_at=$(docker inspect --format='{{.State.StartedAt}}' "$container_name" 2>/dev/null || echo "未知")
    echo -e "  启动时间: $started_at"
    
    # 检查进程是否存活（容器内）
    echo -n "  主进程存活检查 ... "
    if dc_main exec -T client-dev bash -c 'pgrep -x RemoteDrivingClient >/dev/null 2>&1' 2>/dev/null; then
        local pid=$(dc_main exec -T client-dev bash -c 'pgrep -x RemoteDrivingClient' 2>/dev/null || echo "")
        echo -e "${GREEN}✓${NC} RemoteDrivingClient 正在运行 (PID: $pid)"
    else
        echo -e "${YELLOW}⚠${NC} RemoteDrivingClient 进程未启动（可能在等待用户操作）"
    fi
    
    # 检查是否有错误日志
    echo -n "  错误日志检查 ... "
    # grep -c 在无匹配时仍输出 0 但退出码为 1，勿再接 || echo "0"（会得到两行 0，触发 [: integer expected）
    local error_count
    error_count=$(dc_main exec -T client-dev bash -c 'grep -hi "error\|fatal\|crash\|segfault" /workspace/logs/*.log 2>/dev/null | wc -l' 2>/dev/null | tr -d " \r\n" || true)
    error_count=${error_count:-0}
    if [ "$error_count" -gt 0 ]; then
        echo -e "${YELLOW}⚠${NC} 发现 $error_count 条错误日志"
    else
        echo -e "${GREEN}✓${NC} 未发现错误日志"
    fi
    
    # 等待进程启动（可选）
    local wait_for_process="${1:-0}"
    if [ "$wait_for_process" -eq 1 ]; then
        echo -e "${CYAN}等待客户端进程启动（最多 30 秒）...${NC}"
        local wait_count=0
        while [ $wait_count -lt 30 ]; do
            if dc_main exec -T client-dev bash -c 'pgrep -x RemoteDrivingClient >/dev/null 2>&1' 2>/dev/null; then
                echo -e "${GREEN}✓${NC} 客户端进程已启动"
                return 0
            fi
            sleep 1
            wait_count=$((wait_count + 1))
        done
        echo -e "${YELLOW}⚠${NC} 等待超时，进程仍未启动"
        return 0
    fi
    
    echo ""
    echo -e "${GREEN}✓ 容器健康检查完成${NC}"
    return 0
}

# ---------- 启动前自动诊断清单（容器运行条件门禁） ----------
# 目标：在编译/启动客户端前，自动确认 docker 镜像环境具备真实运行条件。
# 规则：任一“必需项”失败 -> 立即退出，避免进入编译/启动后才因 GL 门禁失败。
run_client_runtime_preflight_checklist() {
    echo -e "${CYAN}========== 客户端运行前诊断清单（自动门禁）==========${NC}"
    local failed=0
    local require_hw=0
    if [ "${TELEOP_GPU_OPTIONAL:-0}" != "1" ] && [ "${TELEOP_REQUIRE_HW_GL:-0}" = "1" ]; then
        require_hw=1
    fi

    # 1) 宿主 DISPLAY 与 X11 socket 必须一致
    echo -n "  [P1] DISPLAY 与 X11 socket ... "
    local dnum="${DISPLAY:-}"
    dnum="${dnum#:}"
    dnum="${dnum%%.*}"
    if [ -n "${dnum}" ] && [ -S "/tmp/.X11-unix/X${dnum}" ]; then
        echo -e "${GREEN}✓${NC} (DISPLAY=${DISPLAY})"
    else
        echo -e "${RED}✗${NC} (DISPLAY=${DISPLAY:-<unset>}，对应 socket 不存在)"
        failed=1
    fi

    # 2) NVIDIA GL 模式下，Xauthority 必须可读且非空
    if [ "${TELEOP_CLIENT_NVIDIA_GL:-0}" = "1" ]; then
        echo -n "  [P2] XAUTHORITY_HOST_PATH 有效性 ... "
        if [ -n "${XAUTHORITY_HOST_PATH:-}" ] && [ -r "${XAUTHORITY_HOST_PATH}" ] && [ -s "${XAUTHORITY_HOST_PATH}" ]; then
            echo -e "${GREEN}✓${NC} (${XAUTHORITY_HOST_PATH})"
        else
            echo -e "${RED}✗${NC} (XAUTHORITY_HOST_PATH 缺失/不可读/空文件)"
            failed=1
        fi
    fi

    # 3) 容器必须可执行基础图形命令
    echo -n "  [P3] 容器图形工具可用（glxinfo）... "
    if dc_main exec -T client-dev bash -c 'command -v glxinfo >/dev/null 2>&1' 2>/dev/null; then
        echo -e "${GREEN}✓${NC}"
    else
        echo -e "${RED}✗${NC} (未找到 glxinfo)"
        failed=1
    fi

    # 4) 需要硬件 GL 时，容器内必须可见 NVIDIA 设备并通过 nvidia-smi
    if [ "$require_hw" -eq 1 ]; then
        echo -n "  [P4] 容器 NVIDIA 设备与 nvidia-smi ... "
        if dc_main exec -T client-dev bash -c 'test -c /dev/nvidia0 && command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi -L >/dev/null 2>&1' 2>/dev/null; then
            echo -e "${GREEN}✓${NC}"
        else
            echo -e "${RED}✗${NC} (/dev/nvidia0 或 nvidia-smi 不可用)"
            failed=1
        fi
    fi

    # 5) 需要硬件 GL 时，容器内 OpenGL 渲染器必须不是软件光栅
    if [ "$require_hw" -eq 1 ]; then
        echo -n "  [P5] 容器 OpenGL 渲染器（必须硬件）... "
        local renderer
        renderer="$(dc_main exec -T -e DISPLAY="$DISPLAY" -e XAUTHORITY=/root/.Xauthority client-dev bash -lc 'glxinfo -B 2>/dev/null | sed -n "s/^OpenGL renderer string:[[:space:]]*//p" | head -1' 2>/dev/null || true)"
        renderer="$(printf '%s' "$renderer" | tr -d '\r')"
        if [ -z "$renderer" ]; then
            echo -e "${RED}✗${NC} (无法获取 OpenGL renderer)"
            failed=1
        elif printf '%s' "$renderer" | grep -Eqi 'llvmpipe|softpipe|swrast|lavapipe|software rasterizer'; then
            echo -e "${RED}✗${NC} ($renderer)"
            failed=1
        else
            echo -e "${GREEN}✓${NC} ($renderer)"
        fi
    else
        echo -e "  [P4/P5] ${YELLOW}⊘${NC} 已启用 GPU 可选模式（跳过硬件强制项）"
    fi

    if [ "$failed" -eq 1 ]; then
        echo -e "${RED}══════════════════════════════════════════════════════════════════════${NC}"
        echo -e "${RED}客户端运行前诊断清单未通过：已阻断后续编译与启动。${NC}"
        echo -e "${YELLOW}请先修复 DISPLAY/Xauthority/NVIDIA/GL 渲染器问题后重试。${NC}"
        echo -e "${RED}══════════════════════════════════════════════════════════════════════${NC}"
        return 1
    fi

    echo -e "${GREEN}✓ 运行前诊断清单通过，继续后续编译与启动${NC}"
    echo ""
    return 0
}

# ---------- 确保 client-dev 内已安装 FlatBuffers（用于协议解析）----------
ensure_client_flatbuffers() {
    if dc_main exec -T client-dev bash -c 'command -v flatc >/dev/null 2>&1 && test -f /usr/include/flatbuffers/flatbuffers.h' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} FlatBuffers 已安装（flatc + headers）"
        return 0
    fi
    echo -e "${YELLOW}容器内未检测到 FlatBuffers，正在安装 flatbuffers-compiler libflatbuffers-dev...${NC}"
    if dc_main exec -T -u root client-dev bash -c \
        'apt-get update -qq && apt-get install -y -qq --no-install-recommends flatbuffers-compiler libflatbuffers-dev && rm -rf /var/lib/apt/lists/*' 2>/dev/null; then
        if dc_main exec -T client-dev bash -c 'command -v flatc >/dev/null 2>&1' 2>/dev/null; then
            echo -e "${GREEN}✓${NC} FlatBuffers 安装成功"
            return 0
        fi
    fi
    echo -e "${RED}✗${NC} FlatBuffers 安装失败，客户端编译将失败"
    echo ""
    return 1
}

# ---------- 确保 client-dev 内已安装 Paho MQTT C++（用于 MQTT 通信）----------
ensure_client_paho_mqtt() {
    # 检查是否已安装到 /usr/local 或 /usr
    if dc_main exec -T client-dev bash -c 'test -f /usr/local/include/mqtt/async_client.h || test -f /usr/include/mqtt/async_client.h' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Paho MQTT C++ 已安装"
        return 0
    fi
    
    echo -e "${YELLOW}容器内未检测到 Paho MQTT C++，将尝试从 deps/ 源码构建安装（耗时较长）...${NC}"
    if dc_main exec -T -u root client-dev bash -c "
        set -e
        # 安装编译依赖
        apt-get update -qq && apt-get install -y -qq --no-install-recommends libssl-dev cmake build-essential
        
        # 创建临时构建目录（避免宿主挂载的 read-only 限制）
        rm -rf /tmp/paho-c-build /tmp/paho-cpp-build
        mkdir -p /tmp/paho-c-build /tmp/paho-cpp-build
        
        # 1. 构建 paho.mqtt.c
        cd /tmp/paho-c-build
        cmake /workspace/deps/paho.mqtt.cpp/externals/paho.mqtt.c \
            -DPAHO_WITH_SSL=ON -DPAHO_BUILD_STATIC=OFF -DPAHO_BUILD_SHARED=ON -DCMAKE_BUILD_TYPE=Release
        make -j$(nproc) install
        
        # 2. 构建 paho.mqtt.cpp
        cd /tmp/paho-cpp-build
        cmake /workspace/deps/paho.mqtt.cpp \
            -DPAHO_WITH_SSL=ON -DPAHO_BUILD_SHARED=ON -DPAHO_BUILD_STATIC=OFF -DPAHO_BUILD_SAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
        make -j$(nproc) install
        
        ldconfig
        echo '✓ Paho MQTT C++ 构建安装完成'
    " 2>&1; then
        echo -e "${GREEN}✓ Paho MQTT C++ 安装成功${NC}"
        return 0
    fi
    echo -e "${RED}✗${NC} Paho MQTT C++ 自动安装失败，MQTT 功能将受限"
    return 0 # 允许继续，CMakeLists 只是 WARNING
}

# ---------- 确保 client-dev 内已安装 Qt6 Multimedia + MultimediaQuick（CMake find_package 必需）----------
# 检测 Qt6MultimediaConfig.cmake；缺失时在容器内用 aqt 安装 qtmultimedia（与 ensure_qsb 相同 aqt 流程）
ensure_qt_multimedia_in_container() {
    local QT_VER="${CLIENT_QT_VERSION:-6.8.0}"
    local AQT_ARCH="${CLIENT_QT_AQT_ARCH:-linux_gcc_64}"
    local MM_CMAKE="/opt/Qt/${QT_VER}/gcc_64/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake"
    local MMQ_CMAKE="/opt/Qt/${QT_VER}/gcc_64/lib/cmake/Qt6MultimediaQuick/Qt6MultimediaQuickConfig.cmake"
    # 与 client/CMakeLists.txt 一致：部分镜像有 libQt6MultimediaQuick.so 但无 Qt6MultimediaQuickConfig.cmake
    local MMQ_LIB="/opt/Qt/${QT_VER}/gcc_64/lib/libQt6MultimediaQuick.so"

    if dc_main exec -T client-dev bash -c "test -f \"$MM_CMAKE\" && { test -f \"$MMQ_CMAKE\" || test -f \"$MMQ_LIB\"; }" 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Qt6 Multimedia 已安装（Qt6Multimedia + MultimediaQuick 库/CMake）"
        return 0
    fi

    echo -e "${YELLOW}容器内未检测到 Qt6 Multimedia，将尝试用 aqt 在线安装 qtmultimedia（较慢）...${NC}"
    echo -e "${YELLOW}  建议：重建镜像（默认层缓存；可选国内 Qt 源 AQT_BASE=... 见 docker-compose.client-dev.yml 头注释）:${NC}"
    echo -e "${YELLOW}    docker compose -f docker-compose.yml -f docker-compose.client-dev.yml build client-dev${NC}"
    echo -e "${YELLOW}    仅缓存异常时再加: CLIENT_DEV_BUILD_NO_CACHE=1 ... build --no-cache client-dev${NC}"
    if dc_main exec -T -u root client-dev bash -c "
        set -e
        export DEBIAN_FRONTEND=noninteractive
        QT_VER='${QT_VER}'
        AQT_ARCH='${AQT_ARCH}'

        if ! command -v pip3 &>/dev/null; then
            sed -i 's@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g' /etc/apt/sources.list && sed -i 's/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list && apt-get update -qq
            apt-get install -y --no-install-recommends python3-pip 2>&1 | tail -1
            rm -rf /var/lib/apt/lists/partial
        fi
        sed -i 's@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g' /etc/apt/sources.list && sed -i 's/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list && apt-get update -qq
        apt-get install -y --no-install-recommends python3-dev build-essential 2>&1 | tail -1
        rm -rf /var/lib/apt/lists/partial

        pip3 install --no-cache-dir -i https://pypi.tuna.tsinghua.edu.cn/simple aqtinstall 2>&1 | tail -1

        echo '========== aqt list-qt linux desktop --modules '\$QT_VER' gcc_64 =========='
        if ! aqt list-qt linux desktop --modules \"\$QT_VER\" gcc_64 2>&1; then
            echo '（gcc_64 列表失败，尝试 linux_gcc_64）'
            aqt list-qt linux desktop --modules \"\$QT_VER\" linux_gcc_64 2>&1 || true
        fi

        echo '安装 qtmultimedia 模块（Multimedia + MultimediaQuick）以及 WebSockets, QuickControls2...'
        aqt install-qt -O /opt/Qt linux desktop \"\$QT_VER\" \"\$AQT_ARCH\" \
            -m qtmultimedia -m qtwebsockets -m qtquickcontrols2 2>&1 | tail -8

        pip3 uninstall -y aqtinstall pyzstd brotli py7zr pybcj psutil 2>/dev/null || true

        test -f /opt/Qt/\$QT_VER/gcc_64/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake
        { test -f /opt/Qt/\$QT_VER/gcc_64/lib/cmake/Qt6MultimediaQuick/Qt6MultimediaQuickConfig.cmake || test -f /opt/Qt/\$QT_VER/gcc_64/lib/libQt6MultimediaQuick.so; }
        echo '✓ Qt6 Multimedia 模块安装成功'
    " 2>&1; then
        if dc_main exec -T client-dev bash -c "test -f \"$MM_CMAKE\" && { test -f \"$MMQ_CMAKE\" || test -f \"$MMQ_LIB\"; }" 2>/dev/null; then
            echo -e "${GREEN}✓${NC} Qt6 Multimedia 安装成功，CMake 可找到 Multimedia / MultimediaQuick"
            return 0
        fi
    fi
    echo -e "${RED}✗${NC} Qt6 Multimedia 自动安装失败（缺少 Qt6Multimedia 或 Qt6MultimediaQuick）"
    echo -e "${YELLOW}  建议：在 Qt Maintenance Tool 中安装 Multimedia，或重建镜像: bash scripts/build-client-dev-full-image.sh${NC}"
    echo -e "${YELLOW}  可调参: CLIENT_QT_VERSION CLIENT_QT_AQT_ARCH（install-qt 架构，默认 linux_gcc_64）${NC}"
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
            sed -i 's@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g' /etc/apt/sources.list && sed -i 's/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list && apt-get update -qq
            apt-get install -y --no-install-recommends python3-pip 2>&1 | tail -1
            rm -rf /var/lib/apt/lists/partial
        fi
        sed -i 's@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g' /etc/apt/sources.list && sed -i 's/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list && apt-get update -qq
        apt-get install -y --no-install-recommends python3-dev build-essential 2>&1 | tail -1
        rm -rf /var/lib/apt/lists/partial

        # 安装/升级 aqtinstall
        pip3 install --no-cache-dir -i https://pypi.tuna.tsinghua.edu.cn/simple aqtinstall 2>&1 | tail -1

        # 通过 aqt 追加安装 qtshadertools 模块（含 qsb 工具）
        # 关键参数：linux desktop + linux_gcc_64（不是 gcc_64）+ -m qtshadertools
        echo '安装 qtshadertools, qtwebsockets, qtquickcontrols2 模块...'
        aqt install-qt -O /opt/Qt \
            linux desktop 6.8.0 linux_gcc_64 \
            -m qtshadertools -m qtwebsockets -m qtquickcontrols2 2>&1 | tail -3

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
    if dc_main exec -T client-dev bash -c 'pkg-config --exists libxkbcommon' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} libxkbcommon-dev 已存在（XKB::XKB CMake 目标可用）"
        return 0
    fi
    echo -e "${YELLOW}容器内未检测到 libxkbcommon-dev (或缺失 .pc 文件)，正在安装...${NC}"
    if dc_main exec -T -u root client-dev bash -c \
        'sed -i "s@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g" /etc/apt/sources.list && sed -i "s/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g" /etc/apt/sources.list && apt-get update -qq && apt-get install -y -qq --no-install-recommends libxkbcommon-dev && rm -rf /var/lib/apt/lists/*' 2>/dev/null; then
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

# ---------- 确保 client-dev 容器内有硬件解码依赖（VA-API / libdrm）----------
ensure_client_hw_decode_deps() {
    if dc_main exec -T client-dev bash -c 'pkg-config --exists libva libva-drm libdrm' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} 硬件解码依赖已就绪（libva, libva-drm, libdrm）"
        return 0
    fi
    echo -e "${YELLOW}容器内未检测到硬件解码依赖，正在安装 libva-dev libdrm-dev...${NC}"
    if dc_main exec -T -u root client-dev bash -c \
        'sed -i "s@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g" /etc/apt/sources.list && sed -i "s/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g" /etc/apt/sources.list && apt-get update -qq && apt-get install -y -qq --no-install-recommends libva-dev libdrm-dev && rm -rf /var/lib/apt/lists/*' 2>/dev/null; then
        if dc_main exec -T client-dev bash -c 'pkg-config --exists libva libva-drm libdrm' 2>/dev/null; then
            echo -e "${GREEN}✓${NC} 硬件解码依赖已安装"
            return 0
        fi
    fi
    echo -e "${RED}✗${NC} 硬件解码依赖安装失败，客户端将无法使用硬件加速解码"
    echo -e "${YELLOW}  可尝试: 以 root 进入容器手动 apt-get install libva-dev libdrm-dev${NC}"
    echo ""
    return 1
}

# ---------- 确保 libpulse0 + libpulse-dev（Qt6Multimedia 依赖 libpulse；缺则链接阶段 pa_* undefined）----------
ensure_client_libpulse() {
    if dc_main exec -T client-dev bash -c '
        for f in /lib/x86_64-linux-gnu/libpulse.so.0 /usr/lib/x86_64-linux-gnu/libpulse.so.0 \
                 /lib/aarch64-linux-gnu/libpulse.so.0 /usr/lib/aarch64-linux-gnu/libpulse.so.0; do
            [ -f "$f" ] && exit 0
        done
        ldconfig -p 2>/dev/null | grep -q libpulse.so.0 && exit 0
        exit 1
    ' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} libpulse 已存在（Qt6Multimedia 链接依赖）"
        return 0
    fi
    echo -e "${YELLOW}容器内未检测到 libpulse（libpulse.so.0），正在安装 libpulse0 libpulse-dev...${NC}"
    if dc_main exec -T -u root client-dev bash -c \
        'sed -i "s@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g" /etc/apt/sources.list && sed -i "s/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g" /etc/apt/sources.list && apt-get update -qq && apt-get install -y -qq --no-install-recommends libpulse0 libpulse-dev && rm -rf /var/lib/apt/lists/*' 2>/dev/null; then
        if dc_main exec -T client-dev bash -c '
            for f in /lib/x86_64-linux-gnu/libpulse.so.0 /usr/lib/x86_64-linux-gnu/libpulse.so.0 \
                     /lib/aarch64-linux-gnu/libpulse.so.0 /usr/lib/aarch64-linux-gnu/libpulse.so.0; do
                [ -f "$f" ] && exit 0
            done
            ldconfig -p 2>/dev/null | grep -q libpulse.so.0 && exit 0
            exit 1
        ' 2>/dev/null; then
            echo -e "${GREEN}✓${NC} libpulse0 / libpulse-dev 已安装"
            return 0
        fi
    fi
    echo -e "${RED}✗${NC} libpulse 安装失败，链接 RemoteDrivingClient 将出现 libpulse.so.0 not found / pa_* undefined reference"
    echo -e "${YELLOW}  可尝试: docker compose -f docker-compose.yml -f docker-compose.client-dev.yml build client-dev${NC}"
    echo ""
    return 1
}

# ---------- 强制重新编译客户端（确保使用最新代码）----------
ensure_client_built() {
    echo -e "${CYAN}========== 编译客户端（强制重新编译以确保使用最新代码）==========${NC}"
    echo -e "${CYAN}  TELEOP_CLIENT_MAKE_JOBS=${TELEOP_CLIENT_MAKE_JOBS} TELEOP_CLIENT_CMAKE_BUILD_TYPE=${TELEOP_CLIENT_CMAKE_BUILD_TYPE}${NC}"
    
    # 停止可能正在运行的客户端进程
    dc_main exec -T client-dev bash -c 'pkill -9 RemoteDrivingClient 2>/dev/null || true; sleep 1' 2>/dev/null || true
    
    # 清理旧的构建目录（如果 build 目录被占用，使用 /tmp/client-build）
    if ! dc_main exec -T client-dev bash -c 'cd /workspace/client && rm -rf build/.qt 2>/dev/null || true' 2>/dev/null; then
        echo -e "${YELLOW}⚠ build 目录被占用，将使用 /tmp/client-build 目录${NC}"
    fi
    
    # 强制重新编译（使用 /tmp/client-build 确保干净编译）
    if dc_main exec -T \
        -e "TELEOP_CLIENT_MAKE_JOBS=${TELEOP_CLIENT_MAKE_JOBS}" \
        -e "TELEOP_CLIENT_CMAKE_BUILD_TYPE=${TELEOP_CLIENT_CMAKE_BUILD_TYPE}" \
        client-dev bash -c '
        set -e
        JOBS="${TELEOP_CLIENT_MAKE_JOBS:-$(nproc)}"
        BT="${TELEOP_CLIENT_CMAKE_BUILD_TYPE:-Debug}"
        mkdir -p /tmp/client-build && cd /tmp/client-build
        echo "配置 CMake..."
        cmake /workspace/client \
            -DCMAKE_PREFIX_PATH="/opt/Qt/6.8.0/gcc_64;/opt/libdatachannel;/usr/local;/usr" \
            -DCMAKE_BUILD_TYPE="${BT}"
        echo "编译客户端 (make -j${JOBS})..."
        make -j"${JOBS}"
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
    local _manual_compose_files="-f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml"
    if [ "${TELEOP_CLIENT_NVIDIA_GL:-0}" = "1" ]; then
        _manual_compose_files="${_manual_compose_files} -f ${TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE}"
    fi
    # 检查当前终端是否有可用的 X11 socket（无则说明在 SSH/无图形，只给出手动命令）
    if [ ! -S "/tmp/.X11-unix/X${DISPLAY#*:}" ]; then
        echo -e "${YELLOW}当前环境无可用图形界面（无 X11 socket），无法在此终端直接打开客户端。${NC}"
        echo -e "${GREEN}所有节点已运行。请在本机有图形界面的终端执行以下命令启动远程驾驶客户端：${NC}"
        echo ""
        echo "  xhost +local:docker"
        echo "  # GL 栈由脚本强制指定（glx）；通过 CLIENT_SKIP_AUTO_GL_STACK=1 禁用 C++ 自动策略"
        echo "  docker compose ${_manual_compose_files} exec -it -e DISPLAY=${_manual_display} -e XAUTHORITY=/root/.Xauthority -e QT_QPA_PLATFORM=xcb -e CLIENT_SKIP_AUTO_GL_STACK=1 -e QT_XCB_GL_INTEGRATION=glx -e CLIENT_AUTO_CONNECT_VIDEO=0 -e CLIENT_LOG_FILE=${_client_log} -e ZLM_VIDEO_URL=http://zlmediakit:80 -e MQTT_BROKER_URL=mqtt://teleop-mosquitto:1883 client-dev bash -lc 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login'"
        echo ""
        return 0
    fi
    # 容器内使用服务名访问 ZLM 和 MQTT
    export ZLM_VIDEO_URL="${ZLM_VIDEO_URL:-http://zlmediakit:80}"
    export MQTT_BROKER_URL="${MQTT_BROKER_URL:-mqtt://teleop-mosquitto:1883}"
    export CLIENT_RESET_LOGIN=1
    # 2c 自动化曾用 CLIENT_AUTO_CONNECT_VIDEO=1 跳过登录；手动客户端强制关闭，避免宿主机 export 遗留进 exec
    export CLIENT_AUTO_CONNECT_VIDEO=0
    local _manual_display="${DISPLAY:-:0}"
    echo ""
    echo -e "${CYAN}若无法打开客户端窗口（如报 Authorization required），请在本机有图形界面的终端执行：${NC}"
    echo "  xhost +local:docker"
    echo "  # GL 栈由脚本强制指定（glx）；通过 CLIENT_SKIP_AUTO_GL_STACK=1 禁用 C++ 自动策略"
    echo "  docker compose ${_manual_compose_files} exec -it -e DISPLAY=${_manual_display} -e XAUTHORITY=/root/.Xauthority -e QT_QPA_PLATFORM=xcb -e CLIENT_SKIP_AUTO_GL_STACK=1 -e QT_XCB_GL_INTEGRATION=glx -e CLIENT_AUTO_CONNECT_VIDEO=0 -e CLIENT_LOG_FILE=${_client_log} -e ZLM_VIDEO_URL=$ZLM_VIDEO_URL -e MQTT_BROKER_URL=$MQTT_BROKER_URL client-dev bash -lc 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login'"
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
    # GL 栈由 RemoteDrivingClient 在 QGuiApplication 之前自动选择（glxinfo / nvidia-smi，见
    # client_display_runtime_policy.cpp），不在此传入 LIBGL_ALWAYS_SOFTWARE / QT_XCB_GL_INTEGRATION，避免与 C++ 策略冲突。
    if dc_main exec -T client-dev bash -c 'ls /dev/dri/renderD* >/dev/null 2>&1'; then
        echo -e "${GREEN}✓ 容器内可见 /dev/dri/renderD*（DRM 已挂载；实际硬件/软件 GL 以客户端 [Client][DisplayPolicy]、[Client][GLProbe] 为准）${NC}"
    else
        echo -e "${YELLOW}⚠ 容器内未检测到 /dev/dri/renderD*；客户端通常会选择软件光栅栈${NC}"
    fi
    _TELEOP_HW_GATE=()
    if [ "${TELEOP_GPU_OPTIONAL:-0}" = "1" ] || [ "${TELEOP_REQUIRE_HW_GL:-0}" = "0" ]; then
        _TELEOP_HW_GATE=(-e CLIENT_GPU_PRESENTATION_OPTIONAL=1)
        echo -e "${YELLOW}✓ 传入 CLIENT_GPU_PRESENTATION_OPTIONAL=1（关闭 Linux+xcb 默认硬件呈现门禁；TELEOP_GPU_OPTIONAL 或 TELEOP_REQUIRE_HW_GL=0）${NC}"
    elif [ "${TELEOP_REQUIRE_HW_GL:-0}" = "1" ]; then
        _TELEOP_HW_GATE=(-e CLIENT_TELOP_STATION=1)
        echo -e "${GREEN}✓ TELEOP_REQUIRE_HW_GL=1 → 传入 CLIENT_TELOP_STATION=1（显式远控台门禁，与进程内默认一致）${NC}"
    fi
    # 视频诊断 + 有 NVIDIA 全链路时优先硬件解码（客户端内仍有 software_raster / 能力门禁）
    _CLIENT_VIDEO_ENV_EXTRA=()
    if [ "${TELEOP_CLIENT_VIDEO_DIAG_MINIMAL:-0}" != "1" ]; then
        _CLIENT_VIDEO_ENV_EXTRA+=(
            -e CLIENT_H264_DECODE_FRAME_SUMMARY_EVERY=60
            -e CLIENT_VIDEO_EVIDENCE_CHAIN=1
            -e CLIENT_VIDEO_EVIDENCE_STRIPE_ROWS=1
        )
        echo -e "${GREEN}✓ 注入视频诊断 env：CLIENT_H264_DECODE_FRAME_SUMMARY_EVERY=60 CLIENT_VIDEO_EVIDENCE_CHAIN=1 STRIPE_ROWS=1${NC}"
    fi
    # 注入推荐的显卡环境变量
    if [ -n "${__GLX_VENDOR_LIBRARY_NAME:-}" ]; then
        _CLIENT_VIDEO_ENV_EXTRA+=( -e __GLX_VENDOR_LIBRARY_NAME="${__GLX_VENDOR_LIBRARY_NAME}" )
    fi
    if [ -n "${QT_XCB_GL_INTEGRATION:-}" ]; then
        _CLIENT_VIDEO_ENV_EXTRA+=( -e QT_XCB_GL_INTEGRATION="${QT_XCB_GL_INTEGRATION}" )
    fi
    if [ -n "${NVIDIA_DRIVER_CAPABILITIES:-}" ]; then
        _CLIENT_VIDEO_ENV_EXTRA+=( -e NVIDIA_DRIVER_CAPABILITIES="${NVIDIA_DRIVER_CAPABILITIES}" )
    fi
    if [ "${TELEOP_CLIENT_SKIP_HW_VIDEO_DECODE:-0}" = "1" ]; then
        _CLIENT_VIDEO_ENV_EXTRA+=(
            -e CLIENT_MEDIA_HARDWARE_DECODE=0
            -e CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0
        )
        echo -e "${YELLOW}✓ TELEOP_CLIENT_SKIP_HW_VIDEO_DECODE=1 → CLIENT_MEDIA_HARDWARE_DECODE=0（软解）${NC}"
    fi
    if [ "${TELEOP_CLIENT_NVIDIA_GL:-0}" = "1" ] && [ "${TELEOP_GPU_OPTIONAL:-0}" != "1" ] &&
        [ "${TELEOP_CLIENT_SKIP_HW_VIDEO_DECODE:-0}" != "1" ]; then
        _CLIENT_VIDEO_ENV_EXTRA+=(
            -e CLIENT_SKIP_AUTO_GL_STACK=1
            -e QT_XCB_GL_INTEGRATION=glx
            -e __GLX_VENDOR_LIBRARY_NAME=nvidia
            -e CLIENT_MEDIA_HARDWARE_DECODE=1
            -e CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=1
            -e CLIENT_WEBRTC_HW_EXPORT_DMABUF=1
        )
        echo -e "${GREEN}✓ 注入硬件视频路径：CLIENT_SKIP_AUTO_GL_STACK=1 CLIENT_MEDIA_*_HARDWARE_DECODE=1 ...${NC}"
    fi
    dc_main exec -it \
        -e DISPLAY="$DISPLAY" \
        -e XAUTHORITY=/root/.Xauthority \
        -e QT_QPA_PLATFORM=xcb \
        -e QT_LOGGING_RULES="qt.qpa.*=false" \
        -e ZLM_VIDEO_URL="$ZLM_VIDEO_URL" \
        -e MQTT_BROKER_URL="$MQTT_BROKER_URL" \
        -e CLIENT_RESET_LOGIN=1 \
        -e CLIENT_AUTO_CONNECT_VIDEO=0 \
        -e "CLIENT_LOG_FILE=${_client_log}" \
        -e "TELEOP_CLIENT_MAKE_JOBS=${TELEOP_CLIENT_MAKE_JOBS}" \
        -e "TELEOP_CLIENT_CMAKE_BUILD_TYPE=${TELEOP_CLIENT_CMAKE_BUILD_TYPE}" \
        "${_TELEOP_HW_GATE[@]}" \
        "${_CLIENT_VIDEO_ENV_EXTRA[@]}" \
        client-dev bash -lc '
        set -e  # 任何错误立即退出
        mkdir -p /tmp/client-build && cd /tmp/client-build
        JOBS="${TELEOP_CLIENT_MAKE_JOBS:-$(nproc)}"
        BT="${TELEOP_CLIENT_CMAKE_BUILD_TYPE:-Debug}"
        
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
                cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE="${BT}"
            fi
            echo "编译客户端 (make -j${JOBS})..."
            make -j"${JOBS}"
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
# DO_* / SKIP_CARLA 已在脚本开头解析（早于 NVIDIA 预检）
ensure_client_dev_image

# 确保宿主机环境已准备好（X11 权限、NVIDIA 驱动等）
if [ "$DO_CLIENT" -eq 1 ]; then
    bash "$SCRIPT_DIR/setup-host-for-client.sh" || echo -e "${YELLOW}警告: 宿主机环境设置脚本执行失败，将继续尝试启动${NC}"
fi

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
assert_client_dev_gpu_when_host_has_nvidia
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
    # 强制门禁：确保容器运行条件完整后，才允许继续编译/启动
    run_client_runtime_preflight_checklist

    # 仅检查，不安装（镜像应已预装）
    ensure_client_chinese_font
    ensure_client_mosquitto_pub
    ensure_client_libgl_mesa_dev
    ensure_client_mesa_utils
    ensure_client_libxkbcommon_dev
    ensure_client_libpulse
    ensure_client_hw_decode_deps
    ensure_client_flatbuffers
    ensure_client_paho_mqtt
    ensure_qt_multimedia_in_container
    ensure_qsb_in_container
    
    # 强制编译客户端（如果设置了 no-build，则跳过编译，客户端启动时自动编译）
    if [ "$DO_BUILD" -eq 1 ]; then
        # 在编译前运行 QML 静态检查，避免带病上线
        bash "$SCRIPT_DIR/verify-qml-lint.sh" || {
            echo -e "${RED}❌ QML 静态检查失败，请修复上述错误后再编译。${NC}"
            exit 1
        }
        ensure_client_built  # 编译失败会直接退出（set -e）
    else
        echo -e "${YELLOW}跳过客户端编译步骤（no-build 模式），客户端将在启动时自动编译${NC}"
        echo -e "${YELLOW}⚠ 警告: 如果客户端编译失败，脚本将停止执行${NC}"
        echo ""
    fi
fi

# X11 环境综合健康检查（仅在需要启动客户端时执行）
if [ "$DO_CLIENT" -eq 1 ]; then
    check_x11_environment || {
        echo -e "${RED}⚠ X11 环境检查未通过，但仍将尝试启动客户端${NC}"
        echo -e "${YELLOW}提示：如果客户端启动失败或崩溃，请尝试以下方法：${NC}"
        echo "  1. 更新 WSL：wsl --update && wsl --shutdown"
        echo "  2. 使用 Wayland 平台：export QT_QPA_PLATFORM=wayland"
        echo "  3. 使用 Offscreen 模式：export QT_QPA_PLATFORM=offscreen"
        echo "  4. 在独立终端中运行客户端"
        echo ""
    }
    
    # 环境变量完整性检查
    verify_environment_config
    
    # 渲染栈验证（Qt + OpenGL + Mesa）
    verify_client_rendering_stack
    
    # 客户端可执行文件验证
    verify_client_binary
    
    # 容器健康检查
    verify_client_container_health
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
    _manual_compose_files="-f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml"
    if [ "${TELEOP_CLIENT_NVIDIA_GL:-0}" = "1" ]; then
        _manual_compose_files="${_manual_compose_files} -f ${TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE}"
    fi
    echo -e "${CYAN}未启动客户端（no-client）。所有节点已运行。手动启动远程驾驶客户端请执行：${NC}"
    echo "  xhost +local:docker"
    echo "  # GL 栈由脚本强制指定（glx）；通过 CLIENT_SKIP_AUTO_GL_STACK=1 禁用 C++ 自动策略"
    echo "  docker compose ${_manual_compose_files} exec -it -e DISPLAY=:0 -e XAUTHORITY=/root/.Xauthority -e QT_QPA_PLATFORM=xcb -e CLIENT_SKIP_AUTO_GL_STACK=1 -e QT_XCB_GL_INTEGRATION=glx -e CLIENT_AUTO_CONNECT_VIDEO=0 -e CLIENT_LOG_FILE=$(teleop_client_log_container_path) -e ZLM_VIDEO_URL=http://zlmediakit:80 -e MQTT_BROKER_URL=mqtt://teleop-mosquitto:1883 client-dev bash -lc 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login'"
    wait_stream_e2e_background
fi
