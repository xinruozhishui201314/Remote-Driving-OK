#!/usr/bin/env bash
# =============================================================================
# 客户端一键全量测试（对齐 start-full-chain.sh 的 compose / client-dev 启动方式）
# =============================================================================
#
# 与 start-full-chain.sh 一致：
#   - docker compose: docker-compose.yml + docker-compose.vehicle.dev.yml + docker-compose.dev.yml
#   - 客户端在 teleop-client-dev（remote-driving-client-dev:full）内运行，依赖镜像内 Qt/libdatachannel/系统库
#   - 容器内构建目录: /tmp/client-build，源码挂载 /workspace/client
#   - 客户端环境: ZLM_VIDEO_URL=http://zlmediakit:80, MQTT_BROKER_URL=mqtt://teleop-mosquitto:1883
#   - 日志: ./logs → /workspace/logs（本会话子目录见 TELEOP_LOGS_RUN_DIR）
#
# 默认执行（ENSURE_STACK=1）：
#   1) 拉起基础栈（postgres / zlm / mosquitto / keycloak / backend / client-dev），不启 CARLA
#   2) 宿主机 L1：verify-client-ui-and-video-coverage.sh
#   3) 容器内编译（若需）+ CTest 全量
#   4) 连接/拉流冒烟：verify-connect-feature.sh（需 DISPLAY+xcb 或自行改 QT_QPA_PLATFORM）
#   5) 四路视频：默认跳过；WITH_CARLA=1 时启 CARLA 并 start_stream 后跑 verify-client-four-view-video.sh
#
# 用法：
#   bash scripts/run-client-oneclick-all-tests.sh
#   ENSURE_STACK=0 bash scripts/run-client-oneclick-all-tests.sh          # 栈已起，只跑测试
#   WITH_CARLA=1 bash scripts/run-client-oneclick-all-tests.sh            # 含 CARLA + 四路视频断言（需 NVIDIA）
#   NO_L1=1 NO_CONNECT=1 bash scripts/run-client-oneclick-all-tests.sh   # 仅 CTest
#   bash scripts/run-client-oneclick-all-tests.sh --help
#
# 环境变量（摘录）：
#   CLIENT_DEV_IMAGE   默认 remote-driving-client-dev:full
#   CARLA_VIN          默认 carla-sim-001（WITH_CARLA=1 时）
#   CARLA_NO_GPU=1     叠加 docker-compose.carla.nogpu.yml
#   BACKEND_URL        默认 http://127.0.0.1:8081
#   ZLM_URL            宿主机探测 ZLM，默认 http://127.0.0.1:80
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# shellcheck disable=SC1091
source "$SCRIPT_DIR/lib/teleop-logs.sh"
teleop_logs_init
teleop_logs_init_run_subdir

export PROJECT_ROOT TELEOP_RUN_ID TELEOP_LOGS_RUN_DIR TELEOP_LOG_DATE

dc_main() {
  docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml "$@"
}

dc_carla() {
  local a=( -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml -f docker-compose.carla.yml )
  [[ "${CARLA_NO_GPU:-0}" == "1" ]] && a+=( -f docker-compose.carla.nogpu.yml )
  docker compose "${a[@]}" "$@"
}

CLIENT_DEV_IMAGE="${CLIENT_DEV_IMAGE:-remote-driving-client-dev:full}"
BACKEND_URL="${BACKEND_URL:-http://127.0.0.1:8081}"
ZLM_URL="${ZLM_URL:-http://127.0.0.1:80}"
CARLA_VIN="${CARLA_VIN:-carla-sim-001}"

ENSURE_STACK="${ENSURE_STACK:-1}"
NO_L1="${NO_L1:-0}"
NO_CTEST="${NO_CTEST:-0}"
NO_CONNECT="${NO_CONNECT:-0}"
WITH_CARLA="${WITH_CARLA:-0}"
# 未显式设置 RUN_FOUR_VIEW 时：与 WITH_CARLA 同步（CARLA 场景默认验四路）
if [[ -z "${RUN_FOUR_VIEW+x}" ]]; then
  RUN_FOUR_VIEW="$WITH_CARLA"
fi

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

PASS=0
FAIL=0
pass() { echo -e "  ${GREEN}[PASS]${NC} $*"; PASS=$((PASS + 1)); }
fail() { echo -e "  ${RED}[FAIL]${NC} $*"; FAIL=$((FAIL + 1)); }

usage() {
  sed -n '2,45p' "$0" | sed 's/^# \{0,1\}//'
}

for arg in "$@"; do
  case "$arg" in
    -h|--help) usage; exit 0 ;;
  esac
done

ensure_client_dev_image() {
  if docker image inspect "$CLIENT_DEV_IMAGE" >/dev/null 2>&1; then
    echo -e "${GREEN}镜像已存在: $CLIENT_DEV_IMAGE${NC}"
    return 0
  fi
  echo -e "${YELLOW}构建镜像 $CLIENT_DEV_IMAGE …${NC}"
  bash "$SCRIPT_DIR/build-client-dev-full-image.sh" "$CLIENT_DEV_IMAGE"
}

# 与 start-full-chain.sh start_all_nodes 对齐（不含 CARLA / vehicle）
ensure_stack_base() {
  echo -e "${CYAN}========== 拉起基础 Compose 栈（与 start-full-chain 一致）==========${NC}"
  docker network create teleop-network 2>/dev/null || true
  dc_main up -d --remove-orphans teleop-postgres zlmediakit teleop-mosquitto
  sleep 4
  dc_main up -d --remove-orphans keycloak
  sleep 3
  docker rm -f remote-driving-backend-1 2>/dev/null || true
  dc_main up -d --remove-orphans backend
  echo -e "${YELLOW}等待 Keycloak / Backend / ZLM …${NC}"
  local w=0
  while [[ "$w" -lt 240 ]]; do
    if curl -sf "http://127.0.0.1:8080/realms/teleop/.well-known/openid-configuration" >/dev/null 2>&1 &&
      curl -sf "${BACKEND_URL}/health" >/dev/null 2>&1 &&
      curl -sf "${ZLM_URL}/index/api/getServerConfig" >/dev/null 2>&1; then
      echo -e "${GREEN}基础服务就绪（约 $((w * 3))s）${NC}"
      break
    fi
    sleep 3
    w=$((w + 1))
    if [[ $((w % 20)) -eq 0 ]]; then
      echo -e "  ${CYAN}… 仍等待 $((w * 3))s / 720s${NC}"
    fi
  done
  if ! docker image inspect "${CLIENT_DEV_IMAGE}" >/dev/null 2>&1; then
    echo -e "${YELLOW}尝试构建 ${CLIENT_DEV_IMAGE} …${NC}"
    bash "$SCRIPT_DIR/build-client-dev-full-image.sh" "${CLIENT_DEV_IMAGE}" 2>/dev/null || true
  fi
  dc_main up -d --no-build --remove-orphans client-dev 2>/dev/null || dc_main up -d --remove-orphans client-dev
  dc_main up -d --remove-orphans client-dev 2>/dev/null || true
  echo -e "${GREEN}client-dev 已请求启动${NC}"
}

start_carla_and_stream() {
  echo -e "${CYAN}========== 启动 CARLA（docker-compose.carla.yml）==========${NC}"
  if ! dc_carla up -d carla 2>&1; then
    echo -e "${RED}CARLA 启动失败（无 GPU 时可试 CARLA_NO_GPU=1）${NC}"
    return 1
  fi
  echo -e "${YELLOW}等待 Bridge 推流（最多 40s）…${NC}"
  local elapsed=0
  while [[ "$elapsed" -lt 40 ]]; do
    if docker logs carla-server 2>&1 | tail -120 | grep -qE "四路相机已 spawn|四路推流已启动|ffmpeg 推流已启动"; then
      echo -e "${GREEN}检测到推流相关日志${NC}"
      break
    fi
    sleep 2
    elapsed=$((elapsed + 2))
  done
  local msg
  msg=$(printf '{"type":"start_stream","vin":"%s","timestamp":0}' "$CARLA_VIN")
  echo -e "${CYAN}MQTT start_stream vin=$CARLA_VIN${NC}"
  if command -v mosquitto_pub >/dev/null 2>&1; then
    mosquitto_pub -h 127.0.0.1 -p 1883 -t vehicle/control -m "$msg" 2>/dev/null || true
  else
    dc_main exec -T teleop-mosquitto mosquitto_pub -h localhost -p 1883 -t vehicle/control -m "$msg" 2>/dev/null || true
  fi
  sleep 8
  return 0
}

# 与 start-full-chain ensure_client_built 相同 CMake 参数；再跑 run-unit-tests（含 CTest）
ensure_client_built_and_ctest_in_container() {
  echo -e "${CYAN}========== 容器内编译 RemoteDrivingClient + CTest（run-unit-tests）==========${NC}"
  dc_main exec -T client-dev bash -euo pipefail -c '
    mkdir -p /tmp/client-build && cd /tmp/client-build
    if [[ ! -f CMakeCache.txt ]]; then
      cmake /workspace/client \
        -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel \
        -DCMAKE_BUILD_TYPE=Debug
    fi
    cmake --build . -j"$(nproc 2>/dev/null || echo 4)"
    test -x ./RemoteDrivingClient
    cmake --build . --target run-unit-tests
  '
}

prepare_display() {
  export DISPLAY="${DISPLAY:-:0}"
  if command -v xhost >/dev/null 2>&1; then
    xhost +local:docker 2>/dev/null || true
  fi
}

have_x11_socket() {
  local dnum="${DISPLAY#*:}"
  dnum="${dnum%%.*}"
  [[ -S "/tmp/.X11-unix/X${dnum}" ]]
}

echo ""
echo -e "${BOLD}${CYAN}========== 客户端一键全量测试（run-client-oneclick-all-tests）==========${NC}"
echo -e "  会话日志目录: ${TELEOP_LOGS_RUN_DIR}"
echo -e "  ENSURE_STACK=$ENSURE_STACK  WITH_CARLA=$WITH_CARLA  RUN_FOUR_VIEW=$RUN_FOUR_VIEW"
echo ""

ensure_client_dev_image

if [[ "$ENSURE_STACK" == "1" ]]; then
  ensure_stack_base
else
  echo -e "${YELLOW}已跳过 ENSURE_STACK=0（请自行保证栈与 client-dev 已运行）${NC}"
fi

if [[ "$WITH_CARLA" == "1" ]]; then
  start_carla_and_stream || fail "CARLA/推流环节异常（四路测试可能失败）"
  export CLIENT_AUTO_CONNECT_TEST_VIN="$CARLA_VIN"
fi

if ! dc_main ps client-dev 2>/dev/null | grep -qE 'Up|running'; then
  echo -e "${RED}client-dev 未运行${NC}"
  exit 1
fi

if [[ "$NO_L1" != "1" ]]; then
  echo -e "${CYAN}========== [L1] UI + 视频结构（宿主机，先于重编译）==========${NC}"
  if bash "$SCRIPT_DIR/verify-client-ui-and-video-coverage.sh"; then
    pass "verify-client-ui-and-video-coverage.sh"
  else
    fail "verify-client-ui-and-video-coverage.sh"
  fi
else
  echo -e "${YELLOW}跳过 L1（NO_L1=1）${NC}"
fi

if [[ "$NO_CTEST" != "1" ]]; then
  ensure_client_built_and_ctest_in_container || { fail "容器内编译或 CTest 失败"; exit 1; }
  pass "CTest run-unit-tests"
else
  echo -e "${CYAN}========== 仅编译主程序（跳过 CTest）==========${NC}"
  dc_main exec -T client-dev bash -euo pipefail -c '
    mkdir -p /tmp/client-build && cd /tmp/client-build
    if [[ ! -f CMakeCache.txt ]]; then
      cmake /workspace/client \
        -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel \
        -DCMAKE_BUILD_TYPE=Debug
    fi
    cmake --build . -j"$(nproc 2>/dev/null || echo 4)"
    test -x ./RemoteDrivingClient
  ' || { fail "客户端编译失败"; exit 1; }
fi

prepare_display
if [[ "$NO_CONNECT" != "1" ]]; then
  echo -e "${CYAN}========== [连接] verify-connect-feature（容器内客户端 ~18s）==========${NC}"
  if have_x11_socket; then
    if bash "$SCRIPT_DIR/verify-connect-feature.sh"; then
      pass "verify-connect-feature.sh"
    else
      fail "verify-connect-feature.sh"
    fi
  else
    echo -e "${YELLOW}无 X11 socket（DISPLAY=$DISPLAY），跳过 verify-connect-feature；可在图形桌面执行或 xvfb-run${NC}"
  fi
else
  echo -e "${YELLOW}跳过连接冒烟（NO_CONNECT=1）${NC}"
fi

if [[ "$RUN_FOUR_VIEW" == "1" ]]; then
  echo -e "${CYAN}========== [四路视频] verify-client-four-view-video ==========${NC}"
  export CLIENT_AUTO_CONNECT_TEST_VIN="${CLIENT_AUTO_CONNECT_TEST_VIN:-${CARLA_VIN:-carla-sim-001}}"
  if [[ "$WITH_CARLA" != "1" ]] && [[ "${CLIENT_AUTO_CONNECT_TEST_VIN}" == "carla-sim-001" ]]; then
    echo -e "${YELLOW}提示: RUN_FOUR_VIEW=1 但未 WITH_CARLA=1，若 ZLM 无 carla-sim-001 四路流将失败${NC}"
  fi
  _fv_log="/workspace/logs/${TELEOP_RUN_ID}/four-view-verify.log"
  mkdir -p "${TELEOP_LOGS_RUN_DIR}"
  export VERIFY_CLIENT_LOG_IN_CONTAINER="$_fv_log"
  if have_x11_socket; then
    if bash "$SCRIPT_DIR/verify-client-four-view-video.sh"; then
      pass "verify-client-four-view-video.sh"
    else
      fail "verify-client-four-view-video.sh"
    fi
  else
    echo -e "${YELLOW}无 X11，尝试 QT_QPA_PLATFORM=offscreen 跑四路断言（可能因 GL 策略失败）${NC}"
    export QT_QPA_PLATFORM=offscreen
    if bash "$SCRIPT_DIR/verify-client-four-view-video.sh"; then
      pass "verify-client-four-view-video.sh (offscreen)"
    else
      fail "verify-client-four-view-video.sh (offscreen)"
    fi
  fi
else
  echo -e "${YELLOW}跳过四路视频（RUN_FOUR_VIEW=0；完整含 CARLA 请: WITH_CARLA=1）${NC}"
fi

echo ""
echo -e "${BOLD}========== 汇总: PASS=$PASS  FAIL=$FAIL ==========${NC}"
if [[ "$FAIL" -gt 0 ]]; then
  echo -e "${RED}一键测试未全部通过，见上文 [FAIL]${NC}"
  exit 1
fi
echo -e "${GREEN}一键测试全部通过${NC}"
echo "  归档目录: $TELEOP_LOGS_RUN_DIR"
exit 0
