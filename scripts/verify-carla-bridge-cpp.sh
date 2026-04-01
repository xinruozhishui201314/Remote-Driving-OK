#!/usr/bin/env bash
# C++ Bridge 自动化验证：仅在新构建的 CARLA 容器（deploy/carla/Dockerfile）内完成构建与可选集成验证。
# 不在宿主机或其它容器中验证。
#
# 用法：
#   ./scripts/verify-carla-bridge-cpp.sh              # 构建 CARLA 镜像（若无）并在其内编译 C++ Bridge
#   ./scripts/verify-carla-bridge-cpp.sh --build-only # 同上
#   ./scripts/verify-carla-bridge-cpp.sh --integration  # 构建验证 + 发 start_stream 检查 ZLM 四路流（需 CARLA 容器+MQTT+ZLM 已启动）
#
# 环境变量：
#   CARLA_VERIFY_IMAGE   用于验证的 CARLA 镜像名（默认 carla-bridge-verify:latest），需由 deploy/carla/Dockerfile 构建
#   SKIP_CARLA_IMAGE_BUILD=1  若已存在 CARLA_VERIFY_IMAGE 则跳过镜像构建（默认会先 build 再验证）
#   VERIFY_STREAM_TIMEOUT    集成验证等待流超时秒数（默认 45）

set -e
set -o pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BRIDGE_CPP="${PROJECT_ROOT}/carla-bridge/cpp"
CARLA_DOCKERFILE="${PROJECT_ROOT}/deploy/carla/Dockerfile"
CARLA_BUILD_CTX="${PROJECT_ROOT}/deploy/carla"
# 与 docker-compose.carla.yml 中 image 一致，便于先 build 后直接启动同一镜像并做验证
CARLA_VERIFY_IMAGE="${CARLA_VERIFY_IMAGE:-remote-driving/carla-with-bridge:latest}"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[verify-cpp] $*${NC}"; }
log_ok()      { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail()    { echo -e "  ${RED}[FAIL] $*${NC}"; }
log_skip()    { echo -e "  ${YELLOW}[SKIP] $*${NC}"; }

MODE="build"
[[ "${1:-}" = "--integration" ]] && MODE="integration"
[[ "${1:-}" = "--build-only" ]] && MODE="build"

# ---------- 1. 确保 CARLA 镜像存在（由 deploy/carla/Dockerfile 构建）-----------
ensure_carla_image() {
  if docker image inspect "$CARLA_VERIFY_IMAGE" >/dev/null 2>&1; then
    if [ "${SKIP_CARLA_IMAGE_BUILD:-0}" = "1" ]; then
      log_ok "使用已有 CARLA 验证镜像: $CARLA_VERIFY_IMAGE"
      return 0
    fi
    log_section "CARLA 验证镜像已存在: $CARLA_VERIFY_IMAGE（跳过构建，若需重建请先 docker rmi $CARLA_VERIFY_IMAGE）"
    return 0
  fi
  log_section "构建 CARLA 验证镜像: $CARLA_VERIFY_IMAGE（deploy/carla/Dockerfile）"
  if [ ! -f "$CARLA_DOCKERFILE" ]; then
    log_fail "不存在 $CARLA_DOCKERFILE"
    return 1
  fi
  local _bld
  _bld="$(mktemp)"
  if docker build -t "$CARLA_VERIFY_IMAGE" -f "$CARLA_DOCKERFILE" "$CARLA_BUILD_CTX" >"$_bld" 2>&1; then
    tail -30 "$_bld"
    rm -f "$_bld"
  else
    tail -40 "$_bld"
    rm -f "$_bld"
    log_fail "CARLA 镜像构建失败"
    return 1
  fi
  log_ok "CARLA 验证镜像构建完成: $CARLA_VERIFY_IMAGE"
  return 0
}

# ---------- 2. 在 CARLA 容器内构建 C++ Bridge（与 entrypoint 一致）-----------
build_inside_carla_container() {
  log_section "在 CARLA 容器内构建 C++ Bridge（挂载 carla-bridge 源码）"
  if [ ! -f "$BRIDGE_CPP/CMakeLists.txt" ]; then
    log_fail "不存在 $BRIDGE_CPP/CMakeLists.txt"
    return 1
  fi
  if ! docker image inspect "$CARLA_VERIFY_IMAGE" >/dev/null 2>&1; then
    log_fail "CARLA 验证镜像不存在: $CARLA_VERIFY_IMAGE；请先运行本脚本以触发构建"
    return 1
  fi
  
  # 检查容器内所需的依赖项
  local check_deps_cmd='
    set -e
    echo "检查编译依赖项..."
    command -v cmake >/dev/null 2>&1 || { echo "错误: 未找到 cmake"; exit 1; }
    command -v make >/dev/null 2>&1 || { echo "错误: 未找到 make"; exit 1; }
    command -v g++ >/dev/null 2>&1 || { echo "错误: 未找到 g++"; exit 1; }
    
    # CARLA 官方 egg 常与 python3.7 绑定；python3 在部分镜像中会 segfault
    if command -v python3.7 >/dev/null 2>&1; then
      python3.7 -c "import carla" 2>/dev/null || { echo "错误: python3.7 无法 import carla"; exit 1; }
    else
      python3 -c "import carla" 2>/dev/null || { echo "错误: CARLA Python API 不可用"; exit 1; }
    fi
    
    # 检查 spdlog
    if [ -f "/usr/local/include/spdlog/spdlog.h" ]; then
        echo "✓ spdlog 已安装"
    else
        echo "✗ spdlog 未安装，尝试安装..."
        apt-get update -qq && apt-get install -y -qq --no-install-recommends libspdlog-dev >/dev/null 2>&1 || {
            echo "错误: 无法安装 spdlog"
            exit 1
        }
        echo "✓ spdlog 已安装"
    fi
    
    echo "DEPENDENCIES_OK"
  '
  
  local deps_out
  deps_out=$(docker run --rm \
    -v "$PROJECT_ROOT/carla-bridge:/workspace/carla-bridge:ro" \
    "$CARLA_VERIFY_IMAGE" \
    bash -c "$check_deps_cmd" 2>&1) || true
    
  if echo "$deps_out" | grep -q "DEPENDENCIES_OK"; then
    log_ok "编译依赖项检查通过"
  else
    log_fail "编译依赖项检查失败"
    echo "$deps_out" | tail -10
    return 1
  fi
  
  local build_cmd='set -e
    echo "开始编译 C++ Bridge..."
    BUILD_DIR=/tmp/carla-bridge-cpp-build
    mkdir -p "$BUILD_DIR"
    echo "配置 CMake..."
    cmake -S /workspace/carla-bridge/cpp -B "$BUILD_DIR" \
      -DCMAKE_PREFIX_PATH=/usr/local \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O2 -march=native"
    echo "编译项目..."
    make -C "$BUILD_DIR" -j$(nproc 2>/dev/null || echo 4)
    test -x "$BUILD_DIR/carla_bridge"
    echo "编译成功! 可执行文件: $BUILD_DIR/carla_bridge"
    echo "BUILD_OK"
  '
  local out
  out=$(docker run --rm \
    -v "$PROJECT_ROOT/carla-bridge:/workspace/carla-bridge:ro" \
    "$CARLA_VERIFY_IMAGE" \
    bash -c "$build_cmd" 2>&1) || true
  if echo "$out" | grep -q "BUILD_OK"; then
    log_ok "C++ Bridge 在 CARLA 容器内构建成功"
    return 0
  fi
  log_fail "C++ Bridge 在 CARLA 容器内构建失败"
  echo "$out" | tail -20 | sed 's/^/    /'
  return 1
}

# ---------- 3. 集成验证（start_stream -> ZLM 四路流）-----------
do_integration_verify() {
  log_section "集成验证：MQTT start_stream (carla-sim-001) -> ZLM 四路流"
  COMPOSE_CMD="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml"
  if [ -f "$PROJECT_ROOT/docker-compose.carla.yml" ]; then
    COMPOSE_CMD="$COMPOSE_CMD -f docker-compose.carla.yml"
  fi
  MQTT_SVC="${MQTT_SERVICE:-teleop-mosquitto}"
  ZLM_URL="${ZLM_URL:-http://127.0.0.1:80}"
  ZLM_SECRET="${ZLM_SECRET:-}"
  VIN="${VIN:-${VEHICLE_VIN:-}}"
VIN_PREFIX="${VIN:+${VIN}_}"
REQUIRED_STREAMS="${VIN_PREFIX}cam_front ${VIN_PREFIX}cam_rear ${VIN_PREFIX}cam_left ${VIN_PREFIX}cam_right"
  WAIT_MAX="${VERIFY_STREAM_TIMEOUT:-45}"

  get_media_list() {
    curl -sf "${ZLM_URL}/index/api/getMediaList?secret=${ZLM_SECRET}&app=teleop" 2>/dev/null || echo '{"code":-1}'
  }
  check_streams() {
    local json
    json="$(get_media_list)"
    echo "$json" | grep -q '"code":0' || return 1
    for s in $REQUIRED_STREAMS; do
      echo "$json" | grep -q "\"stream\":\"$s\"" || return 1
    done
    return 0
  }

  if ! $COMPOSE_CMD ps "$MQTT_SVC" 2>/dev/null | grep -q "Up"; then
    log_fail "${MQTT_SVC} 未运行；请先启动: $COMPOSE_CMD up -d teleop-mosquitto zlmediakit"
    return 1
  fi
  if ! $COMPOSE_CMD ps zlmediakit 2>/dev/null | grep -q "Up"; then
    log_fail "zlmediakit 未运行"
    return 1
  fi
  log_ok "${MQTT_SVC}、zlmediakit 已运行"

  local msg='{"type":"start_stream","vin":"carla-sim-001","timestampMs":0}'
  log_section "发布 MQTT vehicle/control: $msg"
  if command -v mosquitto_pub &>/dev/null; then
    mosquitto_pub -h 127.0.0.1 -p 1883 -t vehicle/control -m "$msg" 2>/dev/null || true
  else
    $COMPOSE_CMD exec -T "$MQTT_SVC" mosquitto_pub -h localhost -p 1883 -t vehicle/control -m "$msg" 2>/dev/null || true
  fi

  local elapsed=0
  while [ $elapsed -lt $WAIT_MAX ]; do
    if check_streams; then
      log_ok "四路流已就绪（${elapsed}s）"
      return 0
    fi
    sleep 2
    elapsed=$((elapsed + 2))
    echo "  ${elapsed}s: 等待 ZLM app=teleop 四路流..."
  done
  log_fail "超时 ${WAIT_MAX}s 未检测到四路流；请确认 CARLA 容器内 C++ Bridge 已启动并收到 start_stream"
  echo "  排查: $COMPOSE_CMD logs carla 2>&1 | tail -40"
  return 1
}

# ---------- 执行 ----------
main() {
  echo ""
  echo -e "${CYAN}========== C++ Bridge 自动化验证（仅在 CARLA 容器内）==========${NC}"
  echo "  模式: $MODE  镜像: $CARLA_VERIFY_IMAGE"
  echo ""

  if ! ensure_carla_image; then
    exit 1
  fi
  if ! build_inside_carla_container; then
    echo ""
    log_fail "构建验证未通过"
    exit 1
  fi

  if [ "$MODE" = "integration" ]; then
    if ! do_integration_verify; then
      echo ""
      log_fail "集成验证未通过"
      exit 1
    fi
  fi

  echo ""
  echo -e "${GREEN}========== C++ Bridge 验证通过 ==========${NC}"
  echo ""
  exit 0
}

main
