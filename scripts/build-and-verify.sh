#!/usr/bin/env bash
# 修改代码后：编译（backend + client）并运行自动化验证
# 用法：./scripts/build-and-verify.sh
# 可选：SKIP_BACKEND_BUILD=1 跳过 backend 构建；SKIP_CLIENT_BUILD=1 跳过客户端构建
#       SKIP_CARLA_BRIDGE_CPP=1 跳过 C++ Bridge（默认 1：无显示/GPU 时 CARLA 验证易失败；需验证时设 0）
#       SKIP_STACK_UP=1 跳过本脚本内的 compose up（已手动启动栈时）
#       SKIP_VERIFY_LOGIN=1 / SKIP_VERIFY_ADMIN=1 / SKIP_VERIFY_ADD_VEHICLE=1 跳过对应验证
#       SKIP_VERIFY_CLIENT_SOURCE_MAP=1 跳过 client 单测名与 CLIENT_UNIT_TEST_SOURCE_MAP.md 一致性校验
#       SKIP_VERIFY_CONTRACT_ARTIFACTS=0 时运行 ./scripts/verify-contract-artifacts.sh（默认 1：由 contract-ci 负责，避免本脚本强依赖 pip 拉包）
#       SKIP_VERIFY_CARLA_WITH_CLIENT=0 启用客户端同步启动 CARLA 验证（默认 1 跳过，需 CARLA 镜像）
# 与 verify-add-vehicle-e2e 一致：三文件 compose（含 dev，Backend 宿主机 8081）。
# Backend dev 首次/重建后需在容器内 CMake 编译，默认可等待较久：
#   BACKEND_HEALTH_MAX_WAIT_SEC=1200 BACKEND_HEALTH_POLL_SEC=3

set -e
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# shellcheck disable=SC1091
source "$SCRIPT_DIR/lib/teleop-logs.sh"
teleop_logs_init

COMPOSE_BASE=( -f docker-compose.yml -f docker-compose.vehicle.dev.yml )
COMPOSE_DEV=( -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml )
# 子脚本通过 COMPOSE_FILES 调用 docker compose
export COMPOSE_FILES="-f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml"
COMPOSE_CMD="docker compose ${COMPOSE_FILES}"

COMPOSE_CMD_BASE() { docker compose "${COMPOSE_BASE[@]}" "$@"; }
COMPOSE_CMD_DEV() { docker compose "${COMPOSE_DEV[@]}" "$@"; }

export BACKEND_URL="${BACKEND_URL:-http://127.0.0.1:8081}"
export KEYCLOAK_URL="${KEYCLOAK_URL:-http://127.0.0.1:8080}"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_section() { echo -e "\n${CYAN}========== $* ==========${NC}"; }
log_ok()     { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail()   { echo -e "  ${RED}[FAIL] $*${NC}"; }
log_skip()   { echo -e "  ${YELLOW}[SKIP] $*${NC}"; }
log_warn()   { echo -e "  ${YELLOW}[WARN] $*${NC}"; }

FAILED=0

BACKEND_HEALTH_MAX_WAIT_SEC="${BACKEND_HEALTH_MAX_WAIT_SEC:-1200}"
BACKEND_HEALTH_POLL_SEC="${BACKEND_HEALTH_POLL_SEC:-3}"

# ---------- 等待 Backend HTTP /health（dev 容器内编译可能需数分钟）----------
wait_backend_health() {
  local reason="${1:-Backend 就绪}"
  local max_iter=$((BACKEND_HEALTH_MAX_WAIT_SEC / BACKEND_HEALTH_POLL_SEC))
  [ "$max_iter" -lt 1 ] && max_iter=60
  log_ok "${reason}：轮询 ${BACKEND_URL}/health（最长 ${BACKEND_HEALTH_MAX_WAIT_SEC}s，间隔 ${BACKEND_HEALTH_POLL_SEC}s）"
  local be=0
  while [ "$be" -lt "$max_iter" ]; do
    if curl -sf "${BACKEND_URL}/health" >/dev/null 2>&1; then
      log_ok "Backend 已响应 /health"
      return 0
    fi
    sleep "$BACKEND_HEALTH_POLL_SEC"
    be=$((be + 1))
    if [ $((be % 30)) -eq 0 ]; then
      echo -e "  ${CYAN}... 仍等待 Backend（约 $((be * BACKEND_HEALTH_POLL_SEC))s / ${BACKEND_HEALTH_MAX_WAIT_SEC}s）${NC}"
    fi
  done
  log_warn "Backend 在 ${BACKEND_HEALTH_MAX_WAIT_SEC}s 内未响应 ${BACKEND_URL}/health"
  return 1
}

# ---------- 拉起验证所需 Compose 栈（Postgres / Keycloak / Backend@8081 等）----------
ensure_teleop_stack() {
  log_section "0/6 确保 Compose 栈就绪（teleop-network + 基础服务 + backend dev）"
  docker network create teleop-network 2>/dev/null || true

  log_ok "启动 teleop-postgres / zlmediakit / teleop-mosquitto"
  COMPOSE_CMD_BASE up -d --remove-orphans teleop-postgres zlmediakit teleop-mosquitto
  sleep 4

  log_ok "启动 keycloak"
  COMPOSE_CMD_BASE up -d --remove-orphans keycloak

  log_ok "等待 Keycloak 就绪（最多约 120s）"
  local kc=0
  while [ "$kc" -lt 60 ]; do
    if curl -sf "${KEYCLOAK_URL}/health/ready" >/dev/null 2>&1 || \
       curl -sf "${KEYCLOAK_URL}/realms/teleop/.well-known/openid-configuration" >/dev/null 2>&1; then
      break
    fi
    sleep 2
    kc=$((kc + 1))
  done

  log_ok "启动 backend（docker-compose.dev.yml → 8081:8080，依赖 db-init）"
  COMPOSE_CMD_DEV up -d --remove-orphans backend

  wait_backend_health "首次拉起 stack 后" || log_warn "Backend 尚未就绪；构建步骤后将再次等待。日志: docker compose ${COMPOSE_FILES} logs backend --tail=80"
}

# 可选：启动车端与 client-dev（供容器内编译 Client；无镜像时静默失败）
optional_client_dev_stack() {
  if [ "${SKIP_CLIENT_STACK:-0}" = "1" ]; then
    return 0
  fi
  if COMPOSE_CMD_DEV up -d --no-build --remove-orphans vehicle client-dev 2>/dev/null; then
    log_ok "vehicle / client-dev 已尝试启动（--no-build）"
  else
    log_skip "vehicle/client-dev 未启动（需镜像或构建）；容器内 Client 编译将跳过"
  fi
}

echo ""
echo -e "${CYAN}========== 修改代码后：编译 + 自动化验证 ==========${NC}"
echo "  COMPOSE_FILES=$COMPOSE_FILES"
echo "  BACKEND_URL=$BACKEND_URL"
echo ""

if [ "${SKIP_STACK_UP:-0}" != "1" ]; then
  ensure_teleop_stack
else
  log_section "0/6 Compose 栈"
  log_skip "SKIP_STACK_UP=1"
fi

optional_client_dev_stack

# 0b. 客户端 CTest 名 ↔ 映射表（MVP；无依赖）
if [ "${SKIP_VERIFY_CLIENT_SOURCE_MAP:-0}" != "1" ]; then
  log_section "0b/6 校验 CLIENT_UNIT_TEST_SOURCE_MAP.md 与 CMake 一致"
  if bash "${SCRIPT_DIR}/verify-client-source-map-sync.sh" 2>&1; then
    log_ok "单测映射表与 CMake 注册一致"
  else
    log_fail "verify-client-source-map-sync.sh 失败：新增 CTest 请同步 docs/CLIENT_UNIT_TEST_SOURCE_MAP.md"
    FAILED=1
  fi
else
  log_section "0b/6 单测映射表校验"
  log_skip "SKIP_VERIFY_CLIENT_SOURCE_MAP=1"
fi

if [ "${SKIP_VERIFY_CONTRACT_ARTIFACTS:-1}" != "1" ]; then
  log_section "0c/6 契约真源静态校验（verify-contract-artifacts.sh）"
  if bash "${SCRIPT_DIR}/verify-contract-artifacts.sh" 2>&1; then
    log_ok "OpenAPI + MQTT schema + golden 通过"
  else
    log_fail "verify-contract-artifacts.sh 失败（或设 SKIP_VERIFY_CONTRACT_ARTIFACTS=1 跳过，由 contract-ci 兜底）"
    FAILED=1
  fi
fi

# 1. 构建 Backend 并重启（使后续 E2E 使用新镜像，含 [Backend][AddVehicle] 等日志）
if [ "${SKIP_BACKEND_BUILD:-0}" != "1" ]; then
  log_section "1/6 构建 Backend"
  _blog="$(mktemp)"
  if COMPOSE_CMD_DEV build backend >"$_blog" 2>&1; then
    tail -25 "$_blog"
    rm -f "$_blog"
    log_ok "Backend 构建完成"
    if COMPOSE_CMD_DEV ps backend 2>/dev/null | grep -q "Up"; then
      COMPOSE_CMD_DEV up -d backend 2>&1 | tail -8 || true
      log_ok "Backend 已重启（新镜像生效，供增加车辆 E2E 日志断言）"
    fi
  else
    tail -50 "$_blog"
    rm -f "$_blog"
    log_fail "Backend 构建失败（若镜像拉取 403，请检查 Docker 镜像源或代理）"
    FAILED=1
  fi
else
  log_section "1/6 构建 Backend"
  log_skip "SKIP_BACKEND_BUILD=1"
fi

# 构建/重启后 dev 容器常需重新编译，必须等 /health 再跑 HTTP 验证
if [ "${SKIP_VERIFY_LOGIN:-0}" != "1" ] || [ "${SKIP_VERIFY_ADMIN:-0}" != "1" ] || [ "${SKIP_VERIFY_ADD_VEHICLE:-0}" != "1" ]; then
  log_section "1b/6 等待 Backend HTTP 就绪（验证前）"
  if ! wait_backend_health "验证登录/管理页/E2E 前"; then
    log_fail "Backend 未就绪，后续 HTTP 验证预计失败"
    FAILED=1
  fi
fi

# 2. 构建 Client（在 client-dev 容器内）
if [ "${SKIP_CLIENT_BUILD:-0}" != "1" ]; then
  log_section "2/6 构建 Client (client-dev 容器内)"
  if COMPOSE_CMD_DEV ps client-dev 2>/dev/null | grep -q "Up"; then
    BUILD_CMD='mkdir -p /tmp/client-build && cd /tmp/client-build && (test -f CMakeCache.txt || cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug) && make -j4'
    _clog="$(mktemp)"
    if COMPOSE_CMD_DEV exec -T client-dev bash -c "$BUILD_CMD" >"$_clog" 2>&1; then
      tail -30 "$_clog"
      cp -f "$_clog" "$(teleop_log_path_session client-container-build)" 2>/dev/null || true
      rm -f "$_clog"
      log_ok "Client 构建完成（完整日志已复制到 logs/）"
    else
      tail -40 "$_clog"
      cp -f "$_clog" "$(teleop_log_path_session client-container-build-fail)" 2>/dev/null || true
      rm -f "$_clog"
      log_fail "Client 构建失败（完整日志已复制到 logs/）"
      FAILED=1
    fi
  else
    log_skip "client-dev 未运行，请先: COMPOSE_CMD_DEV up -d vehicle client-dev（或设 SKIP_CLIENT_BUILD=1）"
  fi
else
  log_section "2/6 构建 Client"
  log_skip "SKIP_CLIENT_BUILD=1"
fi

# 2a. 验证操作界面布局（右视图、高精地图）
log_section "2a/6 验证操作界面布局 (verify-driving-layout.sh)"
if bash "${SCRIPT_DIR}/verify-driving-layout.sh" 2>&1; then
  log_ok "UI 布局验证通过"
else
  log_fail "UI 布局验证失败"
  FAILED=1
fi

# 2a2. 本机 client/build 存在时：视频解码/显示策略单测（DMA-BUF 与软栈线程契约）
if [ "${SKIP_VERIFY_CLIENT_VIDEO_DECODE_SANITY:-0}" != "1" ]; then
  log_section "2a2/6 视频解码显示策略 (verify-client-video-decode-sanity.sh)"
  if bash "${SCRIPT_DIR}/verify-client-video-decode-sanity.sh" 2>&1; then
    log_ok "test_h264decoder / ClientVideoStreamHealth 契约通过（或已 SKIP 无 build）"
  else
    log_fail "verify-client-video-decode-sanity.sh 失败（修复后重试；仅 CI 无 client/build 可设 SKIP_VERIFY_CLIENT_VIDEO_DECODE_SANITY=1）"
    FAILED=1
  fi
else
  log_section "2a2/6 视频解码显示策略"
  log_skip "SKIP_VERIFY_CLIENT_VIDEO_DECODE_SANITY=1"
fi

log_section "2a3/6 CPU 视频 RGBA8888 契约 (verify-client-video-rgba-contract.sh)"
if bash "${SCRIPT_DIR}/verify-client-video-rgba-contract.sh" 2>&1; then
  log_ok "RGBA8888 热路径静态门禁通过"
else
  log_fail "verify-client-video-rgba-contract.sh 失败"
  FAILED=1
fi

# 2b. C++ Bridge（CARLA 容器内；默认跳过）
SKIP_CARLA_BRIDGE_CPP="${SKIP_CARLA_BRIDGE_CPP:-1}"
if [ "${SKIP_CARLA_BRIDGE_CPP}" != "1" ] && [ -f "${PROJECT_ROOT}/carla-bridge/cpp/CMakeLists.txt" ]; then
  log_section "2b/6 验证 C++ Bridge（CARLA 容器内构建）"
  _carlalog="$(mktemp)"
  if bash "${SCRIPT_DIR}/verify-carla-bridge-cpp.sh" --build-only >"$_carlalog" 2>&1; then
    tail -25 "$_carlalog"
    rm -f "$_carlalog"
    log_ok "C++ Bridge 构建验证通过"
  else
    tail -35 "$_carlalog"
    rm -f "$_carlalog"
    log_fail "C++ Bridge 构建验证失败"
    FAILED=1
  fi
else
  log_section "2b/6 C++ Bridge"
  if [ "${SKIP_CARLA_BRIDGE_CPP}" = "1" ]; then
    log_skip "SKIP_CARLA_BRIDGE_CPP=1（默认；需 CARLA/GPU 验证时: SKIP_CARLA_BRIDGE_CPP=0 ./scripts/build-and-verify.sh）"
  else
    log_skip "无 carla-bridge/cpp"
  fi
fi

# 3. 验证客户端登录链路（Keycloak → JWT → GET /api/v1/vins）
if [ "${SKIP_VERIFY_LOGIN:-0}" != "1" ]; then
  log_section "3/6 验证客户端登录链路 (verify-client-login.sh)"
  if ./scripts/verify-client-login.sh 2>&1; then
    log_ok "登录链路验证通过"
  else
    log_fail "登录链路验证失败"
    FAILED=1
  fi
else
  log_section "3/6 验证客户端登录链路"
  log_skip "SKIP_VERIFY_LOGIN=1"
fi

# 4. 验证管理页可访问
if [ "${SKIP_VERIFY_ADMIN:-0}" != "1" ]; then
  log_section "4/6 验证管理页 (verify-admin-add-vehicle-page.sh)"
  if ./scripts/verify-admin-add-vehicle-page.sh 2>&1; then
    log_ok "管理页验证通过"
  else
    log_fail "管理页验证失败"
    FAILED=1
  fi
else
  log_section "4/6 验证管理页"
  log_skip "SKIP_VERIFY_ADMIN=1"
fi

# 5. 验证增加/删除车辆 E2E
if [ "${SKIP_VERIFY_ADD_VEHICLE:-0}" != "1" ]; then
  log_section "5/6 验证增加/删除车辆 E2E (verify-add-vehicle-e2e.sh)"
  if ./scripts/verify-add-vehicle-e2e.sh 2>&1; then
    log_ok "增加车辆 E2E 验证通过"
  else
    log_fail "增加车辆 E2E 验证失败"
    FAILED=1
  fi
else
  log_section "5/6 验证增加/删除车辆 E2E"
  log_skip "SKIP_VERIFY_ADD_VEHICLE=1"
fi

# 6. 验证客户端启动时同步启动 CARLA（ensure-carla-running 集成）
if [ "${SKIP_VERIFY_CARLA_WITH_CLIENT:-1}" != "1" ]; then
  log_section "6/6 验证客户端启动时同步启动 CARLA (verify-ensure-carla-with-client.sh)"
  if bash ./scripts/verify-ensure-carla-with-client.sh 2>&1; then
    log_ok "客户端同步启动 CARLA 验证通过"
  else
    log_fail "客户端同步启动 CARLA 验证失败"
    FAILED=1
  fi
else
  log_section "6/6 客户端同步启动 CARLA"
  log_skip "SKIP_VERIFY_CARLA_WITH_CLIENT=1（默认跳过；需 CARLA 镜像时设 0）"
fi

echo ""
log_section "客户端契约静态检查 (verify-client-contract.sh)"
if bash "$SCRIPT_DIR/verify-client-contract.sh" 2>&1; then
  log_ok "客户端 QML 控制路径契约检查通过"
else
  log_fail "客户端契约检查失败"
  FAILED=1
fi

echo ""
log_section "QML AppContext 子目录 import 静态检查 (verify-qml-appcontext-imports.sh)"
if bash "$SCRIPT_DIR/verify-qml-appcontext-imports.sh" 2>&1; then
  log_ok "子目录 QML AppContext import 约定检查通过"
else
  log_fail "QML AppContext import 检查失败（须 import \"..\" 或 \"../..\"）"
  FAILED=1
fi

echo ""
if [ $FAILED -eq 0 ]; then
  echo -e "${GREEN}========== 编译与自动化验证全部通过 ==========${NC}"
  echo "  修改代码后请始终执行: ./scripts/build-and-verify.sh"
  exit 0
else
  echo -e "${RED}========== 存在失败项，请根据上方输出修复后重跑 ==========${NC}"
  echo "  重跑: ./scripts/build-and-verify.sh"
  exit 1
fi
