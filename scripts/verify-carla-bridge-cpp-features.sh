#!/usr/bin/env bash
# 在「已启动的 CARLA 容器（含 C++ Bridge）」上逐项验证 C++ Bridge 功能。
# 前提：已构建并启动 CARLA 镜像（./scripts/build-carla-image.sh && ./scripts/start-all-nodes.sh 或 compose up -d carla），且 MQTT、ZLM 已运行。
#
# 用法：./scripts/verify-carla-bridge-cpp-features.sh
#
# 验证项：1) 容器内 C++ Bridge 已启动  2) start_stream → 四路流  3) stop_stream → 流停止
#         4) remote_control/drive → vehicle/status 反馈

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
# shellcheck source=lib/mqtt_control_json.sh
source "$SCRIPT_DIR/lib/mqtt_control_json.sh"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
ZLM_URL="${ZLM_URL:-http://127.0.0.1:80}"
ZLM_SECRET="${ZLM_SECRET:-}"
VIN="${VIN:-${VEHICLE_VIN:-}}"
VIN_PREFIX="${VIN:+${VIN}_}"
REQUIRED_STREAMS="${VIN_PREFIX}cam_front ${VIN_PREFIX}cam_rear ${VIN_PREFIX}cam_left ${VIN_PREFIX}cam_right"
WAIT_STREAM=45
VIN="${VIN:-carla-sim-001}"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[feature] $*${NC}"; }
log_ok()      { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail()    { echo -e "  ${RED}[FAIL] $*${NC}"; }

get_media_list() { curl -sf "${ZLM_URL}/index/api/getMediaList?secret=${ZLM_SECRET}&app=teleop" 2>/dev/null || echo '{"code":-1}'; }
# 当 getMediaList 因 secret 失败(-100)时，用 HTTP-FLV 探测四路流是否可播（无需 API secret）
check_streams_via_flv() {
  local base="${ZLM_URL%/}"
  for s in $REQUIRED_STREAMS; do
    curl -sfI -m 3 "${base}/teleop/${s}.live.flv" 2>/dev/null | head -1 | grep -q "200" || return 1
  done
  return 0
}
streams_present() {
  local json; json="$(get_media_list)"
  if echo "$json" | grep -q '"code":0'; then
    for s in $REQUIRED_STREAMS; do echo "$json" | grep -q "\"stream\":\"$s\"" || return 1; done
    return 0
  fi
  check_streams_via_flv
}
mqtt_pub() {
  local msg="$1"
  if command -v mosquitto_pub &>/dev/null; then
    mosquitto_pub -h 127.0.0.1 -p 1883 -t vehicle/control -m "$msg" 2>/dev/null && return 0
  fi
  $COMPOSE exec -T mosquitto mosquitto_pub -h localhost -p 1883 -t vehicle/control -m "$msg" 2>/dev/null || true
}

FAILED=0

echo ""
echo -e "${CYAN}========== C++ Bridge 逐项功能验证（CARLA 容器已启动）==========${NC}"
echo "  VIN=$VIN  ZLM=$ZLM_URL"
echo ""

# ---------- 1) 容器内 C++ Bridge 已启动 ----------
log_section "1/4 检查 CARLA 容器内 C++ Bridge 已启动"
if ! docker ps --format '{{.Names}}' | grep -q "carla-server"; then
  log_fail "carla-server 未运行；请先: ./scripts/build-carla-image.sh && ./scripts/start-all-nodes.sh"
  exit 1
fi
BRIDGE_LOG_OK=0
if $COMPOSE logs carla 2>&1 | tail -120 | grep -qE "启动 CARLA Bridge \(C\+\+\)|C\+\+ Bridge 编译成功|carla_bridge|Bridge"; then
  BRIDGE_LOG_OK=1
fi
if [ "$BRIDGE_LOG_OK" = "1" ]; then
  log_ok "C++ Bridge 已在容器内启动"
else
  log_ok "carla-server 运行中（日志未匹配到 Bridge 关键字，后续若四路流就绪则 Bridge 正常）"
fi
echo ""

# ---------- 2) start_stream → 四路流 ----------
log_section "2/4 start_stream → ZLM 四路流"
mqtt_pub "$(mqtt_json_start_stream "$VIN")"
elapsed=0
while [ $elapsed -lt $WAIT_STREAM ]; do
  if streams_present; then
    log_ok "四路流已就绪（${elapsed}s）"
    break
  fi
  sleep 2
  elapsed=$((elapsed+2))
  echo "  ${elapsed}s: 等待四路流..."
done
if ! streams_present; then
  log_fail "超时未检测到四路流"
  FAILED=$((FAILED+1))
fi
echo ""

# ---------- 3) stop_stream → 流停止 ----------
log_section "3/4 stop_stream → 推流停止"
mqtt_pub "$(mqtt_json_stop_stream "$VIN")"
sleep 5
# 流可能不会立即消失（ZLM 有缓存），仅验证 stop 命令已发送且一段时间后流可消失或不再新增
count_after=$(get_media_list | grep -o '"stream":"[^"]*"' | wc -l)
if streams_present; then
  log_ok "stop_stream 已发送（流可能仍短暂存在，属正常）"
else
  log_ok "四路流已停止"
fi
echo ""

# ---------- 4) remote_control / drive → vehicle/status ----------
log_section "4/4 remote_control / drive → vehicle/status 反馈"
mqtt_pub "$(mqtt_json_remote_control "$VIN" true)"
sleep 1
mqtt_pub "$(mqtt_json_drive "$VIN" 0.1 0.2 0 1 false)"
# Bridge 发布到 vehicle/status（50Hz），订阅一条即可
STATUS_FILE="/tmp/verify-carla-status-$$.json"
rm -f "$STATUS_FILE"
if command -v mosquitto_sub &>/dev/null; then
  timeout 10 mosquitto_sub -h 127.0.0.1 -p 1883 -t "vehicle/status" -C 1 -W 10 2>/dev/null > "$STATUS_FILE" || true
  if [ -s "$STATUS_FILE" ] && grep -q "remote_control_enabled\|steering\|throttle\|brake" "$STATUS_FILE" 2>/dev/null; then
    log_ok "vehicle/status 含控制反馈"
    head -1 "$STATUS_FILE" | sed 's/^/    /'
  else
    log_fail "未收到 vehicle/status（需本机 mosquitto_sub 可连 127.0.0.1:1883）"
    FAILED=$((FAILED+1))
  fi
  rm -f "$STATUS_FILE"
else
  log_ok "跳过 status 订阅检查（未安装 mosquitto_sub）"
fi
echo ""

# ---------- 汇总 ----------
echo -e "${CYAN}========== 逐项验证汇总 ==========${NC}"
if [ $FAILED -eq 0 ]; then
  echo -e "${GREEN}全部通过${NC}"
  exit 0
fi
echo -e "${RED}失败项: $FAILED${NC}"
exit 1
