#!/usr/bin/env bash
# 自动化验证：客户端四路视图均有视频数据（解码/呈现计数），依据日志
# [Client][VideoPresent][1Hz] 中 Fr / Re / Le / Ri 的 n>0 或 dE>0。
#
# 前置（缺一不可）：
#   1) Compose 栈已起：至少 zlmediakit + client-dev；client-dev 内需已编译 RemoteDrivingClient
#   2) ZLM app=teleop 上已有该 VIN 的四路流（cam_front/rear/left/right）
#      — 可先 ./scripts/verify-stream-e2e.sh 或 CARLA Bridge start_stream
#   3) 图形：默认 xcb + DISPLAY（与 show-driving-ui 一致）；无头可试 xvfb-run 或 export DISPLAY
#
# 用法（仓库根目录）：
#   ./scripts/verify-client-four-view-video.sh
#   CLIENT_AUTO_CONNECT_TEST_VIN=carla-sim-001 ./scripts/verify-client-four-view-video.sh
#   SKIP_ZLM_PREFLIGHT=1 RUN_TIMEOUT=120 ./scripts/verify-client-four-view-video.sh
#
# 环境变量：
#   CLIENT_AUTO_CONNECT_TEST_VIN  与 ZLM 流名前缀一致（默认 123456789）
#   RUN_TIMEOUT           客户端运行秒数（默认 90）
#   SKIP_ZLM_PREFLIGHT=1  跳过 ZLM getMediaList 四路检查（仅信客户端日志）
#   ZLM_URL / ZLM_SECRET  同 verify-stream-e2e.sh
#   VERIFY_CLIENT_LOG_IN_CONTAINER  容器内日志绝对路径（默认 /workspace/logs/verify-four-view-$$.log）
#   ZLM_VIDEO_URL / MQTT_BROKER_URL  与 start-full-chain 一致（默认 zlmediakit / teleop-mosquitto）
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE_FILES="${COMPOSE_FILES:--f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml}"
COMPOSE_CMD="docker compose ${COMPOSE_FILES}"

ZLM_URL="${ZLM_URL:-http://127.0.0.1:80}"
ZLM_SECRET="${ZLM_SECRET:-035c73f7-bb6b-4889-a715-d9eb2d1925cc}"
# 与 main.qml 自动连流测试车一致；CARLA 常为 carla-sim-001
VIN="${CLIENT_AUTO_CONNECT_TEST_VIN:-${VERIFY_CLIENT_FOUR_VIEW_VIN:-${VIN:-${VEHICLE_VIN:-123456789}}}}"
VIN_PREFIX="${VIN}_"
REQUIRED_STREAMS="${VIN_PREFIX}cam_front ${VIN_PREFIX}cam_rear ${VIN_PREFIX}cam_left ${VIN_PREFIX}cam_right"
RUN_TIMEOUT="${RUN_TIMEOUT:-90}"
SKIP_ZLM_PREFLIGHT="${SKIP_ZLM_PREFLIGHT:-0}"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_ok() { echo -e "  ${GREEN}[OK]${NC} $*"; }
log_fail() { echo -e "  ${RED}[FAIL]${NC} $*"; }
log_info() { echo -e "  ${CYAN}[..]${NC} $*"; }

echo ""
echo -e "${CYAN}========== verify-client-four-view-video（四路视频日志断言）==========${NC}"
echo "  VIN=$VIN → 流: $REQUIRED_STREAMS"
echo ""

if ! command -v python3 >/dev/null 2>&1; then
  log_fail "需要 python3"
  exit 2
fi

if ! command -v docker >/dev/null 2>&1; then
  log_fail "需要 docker"
  exit 2
fi

if ! $COMPOSE_CMD ps client-dev 2>/dev/null | grep -qE 'Up|running'; then
  log_fail "client-dev 未运行；请先 compose up client-dev"
  exit 1
fi

if ! $COMPOSE_CMD exec -T client-dev test -x /tmp/client-build/RemoteDrivingClient 2>/dev/null; then
  log_fail "容器内未找到 /tmp/client-build/RemoteDrivingClient；请先 cmake+build"
  exit 1
fi

if [[ "$SKIP_ZLM_PREFLIGHT" != "1" ]]; then
  log_info "ZLM 四路预检（getMediaList app=teleop）…"
  json="$(curl -sf "${ZLM_URL}/index/api/getMediaList?secret=${ZLM_SECRET}&app=teleop" 2>/dev/null || echo '')"
  if [[ -z "$json" ]] || ! echo "$json" | grep -q '"code":0'; then
    log_fail "ZLM getMediaList 失败或不可达: ZLM_URL=$ZLM_URL（可设 SKIP_ZLM_PREFLIGHT=1 跳过）"
    exit 1
  fi
  missing=""
  for s in $REQUIRED_STREAMS; do
    echo "$json" | grep -q "\"stream\":\"$s\"" || missing="$missing $s"
  done
  if [[ -n "$missing" ]]; then
    log_fail "ZLM 缺少流:$missing"
    echo "  当前 teleop 流: $(echo "$json" | grep -o '"stream":"[^"]*"' | tr '\n' ' ')"
    echo "  请先推流或设置正确的 CLIENT_AUTO_CONNECT_TEST_VIN / VIN"
    exit 1
  fi
  log_ok "ZLM 四路在册"
else
  log_info "已跳过 ZLM 预检（SKIP_ZLM_PREFLIGHT=1）"
fi

LOG_IN_CONTAINER="${VERIFY_CLIENT_LOG_IN_CONTAINER:-/workspace/logs/verify-four-view-$$.log}"
ZLM_VIDEO_URL="${ZLM_VIDEO_URL:-http://zlmediakit:80}"
MQTT_BROKER_URL="${MQTT_BROKER_URL:-mqtt://teleop-mosquitto:1883}"
log_info "运行客户端 ${RUN_TIMEOUT}s → 日志 $LOG_IN_CONTAINER"

DISP="${DISPLAY:-:0}"
_log_dir="$(dirname "$LOG_IN_CONTAINER")"
# 默认强制软件栈时须同时允许软件呈现（否则 Linux+xcb 默认硬件门禁会 exit 75）
_four_view_sw_gate=()
if [ "${CLIENT_ASSUME_SOFTWARE_GL:-1}" = "1" ]; then
  _four_view_sw_gate=(-e CLIENT_ALLOW_SOFTWARE_PRESENTATION=1)
fi
# 与 start-full-chain / verify-connect-feature：同一容器内 Qt 与 Broker/ZLM 地址
$COMPOSE_CMD exec -T \
  -e "DISPLAY=$DISP" \
  -e QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}" \
  -e QT_LOGGING_RULES="${QT_LOGGING_RULES:-qt.qpa.*=false}" \
  -e "ZLM_VIDEO_URL=$ZLM_VIDEO_URL" \
  -e "MQTT_BROKER_URL=$MQTT_BROKER_URL" \
  -e CLIENT_STARTUP_TCP_TARGETS=mqtt,zlm \
  -e "CLIENT_LOG_FILE=$LOG_IN_CONTAINER" \
  -e CLIENT_AUTO_CONNECT_VIDEO=1 \
  -e "CLIENT_AUTO_CONNECT_TEST_VIN=$VIN" \
  -e CLIENT_ASSUME_SOFTWARE_GL="${CLIENT_ASSUME_SOFTWARE_GL:-1}" \
  "${_four_view_sw_gate[@]}" \
  client-dev bash -c "mkdir -p '$_log_dir'; rm -f '$LOG_IN_CONTAINER'; cd /tmp/client-build && timeout '$RUN_TIMEOUT' ./RemoteDrivingClient 2>&1 || true; sleep 2" \
  | tail -15

if ! $COMPOSE_CMD exec -T client-dev test -f "$LOG_IN_CONTAINER" 2>/dev/null; then
  log_fail "日志文件未生成: $LOG_IN_CONTAINER"
  exit 1
fi

if ! $COMPOSE_CMD exec -T client-dev cat "$LOG_IN_CONTAINER" | python3 "$PROJECT_ROOT/scripts/lib/verify_four_view_client_log.py" -; then
  log_fail "四路视频日志断言未通过"
  echo ""
  echo -e "${YELLOW}排障：${NC}"
  echo "  grep -E 'VideoPresent\\]\\[1Hz\\]|WebRTC\\]\\[StreamManager\\]|ZLM_VIDEO' 日志"
  echo "  确认 VIN 与推流一致；确认容器内 echo \$ZLM_VIDEO_URL"
  exit 1
fi

echo ""
echo -e "${GREEN}========== verify-client-four-view-video 通过 ==========${NC}"
exit 0
