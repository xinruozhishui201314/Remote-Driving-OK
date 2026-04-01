#!/usr/bin/env bash
# 并行编译验证脚本（v6：镜像缓存 tag，避免每次重建）
# 功能：并行执行 Backend、Vehicle-side 和 Client-Dev 的 CMake 和 Make 构建。
# 目标：仅验证代码编译是否通过（不启动服务），利用多核资源加速。
#
# 用法：
#   bash scripts/parallel-compile-verify.sh
#
# 镜像缓存：首次运行会构建 backend/vehicle 镜像并打 tag（*:compile-cache），
# 后续运行直接使用该镜像，不再重建。强制重建可设：
#   FORCE_REBUILD_BACKEND=1  bash scripts/parallel-compile-verify.sh
#   FORCE_REBUILD_VEHICLE=1  bash scripts/parallel-compile-verify.sh
#
# 若 Vehicle 报 spdlog 未找到：需先成功构建 Vehicle 镜像（含 libspdlog-dev）。
# 若构建时 registry 403，可切换镜像源或使用已构建好的 vehicle 镜像。
# SKIP_VEHICLE=1：跳过 Vehicle 编译/镜像（无 Vehicle-side 源码或不需要车端时使用）。

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# 强制项目名称
PROJECT_NAME="remote-driving"
# 编译验证用缓存镜像 tag（首次构建后打 tag，后续直接使用）
BACKEND_CACHE_IMAGE="${PROJECT_NAME}-backend:compile-cache"
VEHICLE_CACHE_IMAGE="${PROJECT_NAME}-vehicle:compile-cache"
COMPILE_CACHE_OVERRIDE="docker-compose.compile-cache.yml"

# 缓存管理函数
manage_cache_image() {
    local cache_name="$1"
    local dockerfile="$2"
    local context="$3"
    local force_rebuild="$4"
    
    if [ "${force_rebuild:-0}" = "1" ] || ! docker image inspect "$cache_name" >/dev/null 2>&1; then
        log_info "[Cache] 构建 $cache_name 缓存镜像..."
        if docker build -t "$cache_name" -f "$dockerfile" "$context" 2>"${RESULT_DIR}/${cache_name##:}.log"; then
            log_ok "[Cache] $cache_name 构建完成"
            return 0
        else
            log_err "[Cache] $cache_name 构建失败"
            return 1
        fi
    else
        log_info "[Cache] 使用现有缓存镜像: $cache_name"
        return 0
    fi
}

log_ok()     { echo -e "${GREEN}[OK] $*${NC}"; }
log_err()    { echo -e "${RED}[FAIL] $*${NC}"; }
log_warn()   { echo -e "${YELLOW}[WARN] $*${NC}"; }
log_info()   { echo -e "${CYAN}[INFO] $*${NC}"; }

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}并行编译验证 (结构化日志改造)${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# -------------------------
# 0. 网络清理与自愈机制
# -------------------------
log_info "[Pre-flight] 清理现有容器以释放端口..."
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml down --remove-orphans -v 2>/dev/null || true
sleep 2

# 强制清理所有 orphan 容器，避免状态干扰
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml down --remove-orphans -v 2>/dev/null || true

# Vehicle/Client-dev 使用 teleop-network (external)，编译前确保存在
docker network create teleop-network 2>/dev/null || true

# 存储编译结果的目录
RESULT_DIR="${TMPDIR:-/tmp}/compile-verify-$$"
mkdir -p "$RESULT_DIR"

# 辅助函数：执行单个服务的编译
# $1: 服务名称
# $2: docker compose 命令片段
# $3: 编译命令字符串
# $4: 可选，传 "no-entrypoint" 时 run 使用 --entrypoint ""（用于 backend 避免 dev 入口脚本接管）
compile_service() {
    local name=$1
    local compose_cmd=$2
    local build_cmd=$3
    local entrypoint_opt=${4:-}
    local rc=0

    log_info "[$name] 开始编译..."
    local run_name="parallel-compile-${name}-$$"
    # --no-deps：不启动依赖服务，避免与已有容器名冲突
    if [ "$entrypoint_opt" = "no-entrypoint" ]; then
        $compose_cmd run --no-deps --rm -T --name "$run_name" --entrypoint "" $name /bin/bash -c "$build_cmd" > "${RESULT_DIR}/${name}.log" 2>&1 || rc=1
    else
        $compose_cmd run --no-deps --rm -T --name "$run_name" $name /bin/bash -c "$build_cmd" > "${RESULT_DIR}/${name}.log" 2>&1 || rc=1
    fi
    if [ $rc -eq 0 ]; then
        echo "0" > "${RESULT_DIR}/${name}.status"
        log_ok "$name 编译成功"
    else
        echo "1" > "${RESULT_DIR}/${name}.status"
        log_err "$name 编译失败，请查看日志: ${RESULT_DIR}/${name}.log"
        # 打印最后 10 行日志用于快速排错
        echo "-------- $name 最后 10 行日志 --------"
        tail -10 "${RESULT_DIR}/${name}.log" | sed 's/^/    /'
        echo "----------------------------------------"
    fi
}

# ==========================
# 并行编译任务
# ==========================

# 1. Backend 缓存镜像管理
if [ -n "${FORCE_REBUILD_BACKEND:-}" ]; then
    log_info "[Pre-flight] 强制重建 Backend 缓存..."
    manage_cache_image "${BACKEND_CACHE_IMAGE}" "./backend/Dockerfile.dev" "./backend" 1
    if [ $? -ne 0 ]; then
        log_err "[Pre-flight] Backend 缓存构建失败"
        exit 1
    fi
elif ! manage_cache_image "${BACKEND_CACHE_IMAGE}" "./backend/Dockerfile.dev" "./backend" 0; then
    log_warn "[Pre-flight] Backend 缓存管理失败，尝试使用最新镜像"
    if docker image inspect "${PROJECT_NAME}-backend:latest" >/dev/null 2>&1; then
        docker tag "${PROJECT_NAME}-backend:latest" "${BACKEND_CACHE_IMAGE}" 2>/dev/null || true
        log_info "[Pre-flight] 已将 latest 打包为缓存镜像"
    else
        log_err "[Pre-flight] 缺少 Backend 镜像"
        exit 1
    fi
fi

# 2. Vehicle 缓存镜像管理
if [ "${SKIP_VEHICLE:-0}" = "1" ]; then
    log_info "[Pre-flight] Vehicle 已跳过（SKIP_VEHICLE=1）"
    echo "0" > "${RESULT_DIR}/vehicle.status"
else
    if [ -n "${FORCE_REBUILD_VEHICLE:-}" ]; then
        log_info "[Pre-flight] 强制重建 Vehicle 缓存..."
        manage_cache_image "${VEHICLE_CACHE_IMAGE}" "./Vehicle-side/Dockerfile" "./Vehicle-side" 1
        if [ $? -ne 0 ]; then
            log_err "[Pre-flight] Vehicle 缓存构建失败"
            echo "1" > "${RESULT_DIR}/vehicle.status"
        fi
    elif ! manage_cache_image "${VEHICLE_CACHE_IMAGE}" "./Vehicle-side/Dockerfile" "./Vehicle-side" 0; then
        log_warn "[Pre-flight] Vehicle 缓存管理失败，尝试使用最新镜像"
        if docker image inspect "${PROJECT_NAME}-vehicle:latest" >/dev/null 2>&1; then
            docker tag "${PROJECT_NAME}-vehicle:latest" "${VEHICLE_CACHE_IMAGE}" 2>/dev/null || true
            log_info "[Pre-flight] 已将 latest 打包为缓存镜像"
        else
            log_err "[Pre-flight] 缺少 Vehicle 镜像"
            echo "1" > "${RESULT_DIR}/vehicle.status"
        fi
    fi
    
    if [ ! -f "${RESULT_DIR}/vehicle.status" ] || [ "$(cat "${RESULT_DIR}/vehicle.status")" = "1" ]; then
        echo "1" > "${RESULT_DIR}/vehicle.status"
    else
        echo "0" > "${RESULT_DIR}/vehicle.status"
    fi
fi

backend_cmd="mkdir -p /app/build && cd /app/build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j\$(nproc)"
(
    COMPOSE_CMD="docker compose -f docker-compose.yml -f docker-compose.dev.yml -f ${COMPILE_CACHE_OVERRIDE} -p ${PROJECT_NAME}"
    compile_service "backend" "$COMPOSE_CMD" "$backend_cmd" "no-entrypoint"
) &

# 2. Vehicle-side “编译”任务（无本地源码时，仅校验镜像可构建 / 已存在）
# 策略：SKIP_VEHICLE=1 直接通过；否则优先使用缓存镜像；否则构建并打 tag。
(
    if [ "${SKIP_VEHICLE:-0}" = "1" ]; then
        echo "0" > "${RESULT_DIR}/vehicle.status"
        log_ok "vehicle 已跳过（SKIP_VEHICLE=1，无 Vehicle-side 源码或不需要车端）"
    elif docker image inspect "${VEHICLE_CACHE_IMAGE}" >/dev/null 2>&1 && [ -z "${FORCE_REBUILD_VEHICLE:-}" ]; then
        echo "0" > "${RESULT_DIR}/vehicle.status"
        log_ok "vehicle 使用缓存镜像 ${VEHICLE_CACHE_IMAGE}，跳过重新编译（可设 FORCE_REBUILD_VEHICLE=1 强制重建）。"
    else
        log_info "[vehicle] 构建 Vehicle 镜像并打 tag ${VEHICLE_CACHE_IMAGE}..."
        if docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.compile.yml -p ${PROJECT_NAME} build vehicle > "${RESULT_DIR}/vehicle.log" 2>&1; then
            docker tag "${PROJECT_NAME}-vehicle:latest" "${VEHICLE_CACHE_IMAGE}" 2>/dev/null || true
            echo "0" > "${RESULT_DIR}/vehicle.status"
            log_ok "vehicle 镜像构建成功并已打 tag（视为编译通过）"
        else
            echo "1" > "${RESULT_DIR}/vehicle.status"
            log_err "vehicle 镜像构建失败，请查看日志: ${RESULT_DIR}/vehicle.log"
            echo "-------- vehicle 最后 10 行日志 --------"
            tail -10 "${RESULT_DIR}/vehicle.log" | sed 's/^/    /'
            echo "----------------------------------------"
        fi
    fi
) &

# 3. Client-Dev 编译任务（同上，需 mosquitto 定义）
client_cmd="mkdir -p /tmp/client-build && cd /tmp/client-build && cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug && make -j4"
(
    COMPOSE_CMD="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.compile.yml -p ${PROJECT_NAME}"
    # 预先检查镜像是否存在
    if ! docker image inspect remote-driving-client-dev:full >/dev/null 2>&1; then
        echo "1" > "${RESULT_DIR}/client-dev.status"
        log_err "Client-Dev 镜像不存在，跳过编译。"
        log_info "提示：请先运行: bash scripts/build-client-dev-full-image.sh"
    else
        compile_service "client-dev" "$COMPOSE_CMD" "$client_cmd"
    fi
) &

# 等待所有后台任务完成
wait
echo ""

# ==========================
# 结果汇总
# ==========================
FAILED=0
for svc in backend vehicle client-dev; do
    STATUS_FILE="${RESULT_DIR}/${svc}.status"
    if [ -f "$STATUS_FILE" ]; then
        if [ "$(cat "$STATUS_FILE" 2>/dev/null)" = "0" ]; then
            log_ok "$svc: 编译通过"
        else
            log_err "$svc: 编译失败"
            FAILED=1
        fi
    else
        log_warn "$svc: 状态文件丢失 (可能未执行编译)"
        FAILED=1
    fi
done

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}所有服务编译验证通过！${NC}"
    echo -e "${GREEN}========================================${NC}"
    exit 0
else
    echo -e "${RED}========================================${NC}"
    echo -e "${RED}编译验证失败，部分服务未编译成功${NC}"
    echo -e "${RED}========================================${NC}"
    exit 1
fi

# 清理临时文件
rm -rf "$RESULT_DIR"
