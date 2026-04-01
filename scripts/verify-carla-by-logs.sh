#!/usr/bin/env bash
# 依据日志的自动化验证：发送 MQTT 指令后抓取 carla-server 日志，根据关键字判断功能是否正常。
# 不依赖客户端或 ZLM API（可选探测 ZLM 流），适合 CI/排障时快速判断 Bridge 行为。
#
# 用法：./scripts/verify-carla-by-logs.sh
# 前提：carla-server 已运行（./scripts/start-all-nodes.sh）。
#
# 验证项（均依据 docker logs carla-server）：
#   1) Bridge 已启动（Python 或 C++）
#   2) 发送 start_stream 后，日志出现「已置 streaming」且「推流已启动」
#   3) 发送 remote_control/drive 后，日志出现「收到 type=remote_control」「收到 type=drive」
#   4) vehicle/status 有反馈（可选：mosquitto_sub 收一条）
#   5) 可选：ZLM 上存在四路流

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
VIN="${VIN:-carla-sim-001}"
LOG_TAIL=400
SLEEP_AFTER_START=8
SLEEP_AFTER_CTRL=2
ZLM_URL="${ZLM_URL:-http://127.0.0.1:80}"
ZLM_SECRET="${ZLM_SECRET:-}"
VIN="${VIN:-${VEHICLE_VIN:-}}"
VIN_PREFIX="${VIN:+${VIN}_}"
REQUIRED_STREAMS="${VIN_PREFIX}cam_front ${VIN_PREFIX}cam_rear ${VIN_PREFIX}cam_left ${VIN_PREFIX}cam_right"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[验证] $*${NC}"; }
log_ok()      { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail()    { echo -e "  ${RED}[FAIL] $*${NC}"; }
log_evidence() { echo -e "  ${BOLD}(日志) $*${NC}"; }

get_carla_logs() {
  docker logs carla-server 2>&1 | tail -"${LOG_TAIL}"
}

mqtt_pub() {
  local msg="$1"
  if command -v mosquitto_pub &>/dev/null; then
    mosquitto_pub -h 127.0.0.1 -p 1883 -t vehicle/control -m "$msg" 2>/dev/null && return 0
  fi
  $COMPOSE exec -T mosquitto mosquitto_pub -h localhost -p 1883 -t vehicle/control -m "$msg" 2>/dev/null || true
}

streams_present() {
  local json
  json="$(curl -sf "${ZLM_URL}/index/api/getMediaList?secret=${ZLM_SECRET}&app=teleop" 2>/dev/null || echo '{"code":-1}')"
  if echo "$json" | grep -q '"code":0'; then
    for s in $REQUIRED_STREAMS; do echo "$json" | grep -q "\"stream\":\"$s\"" || return 1; done
    return 0
  fi
  local base="${ZLM_URL%/}"
  for s in $REQUIRED_STREAMS; do
    curl -sfI -m 2 "${base}/teleop/${s}.live.flv" 2>/dev/null | head -1 | grep -q "200" || return 1
  done
  return 0
}

FAILED=0

echo ""
echo -e "${BOLD}========== CARLA Bridge 功能验证（依据日志判断）==========${NC}"
echo "  VIN=$VIN  日志: docker logs carla-server (tail $LOG_TAIL)"
echo ""

# ---------------------------------------------------------------------------
# 0) 前置：carla-server 运行
# ---------------------------------------------------------------------------
log_section "0/6 前置：carla-server 运行"
if ! docker ps --format '{{.Names}}' | grep -q "carla-server"; then
  log_fail "carla-server 未运行；请先: ./scripts/start-all-nodes.sh"
  exit 1
fi
log_ok "carla-server 运行中"
echo ""

# ---------------------------------------------------------------------------
# 1) 日志判断：Bridge 已启动（Python 或 C++）
# ---------------------------------------------------------------------------
log_section "1/6 Bridge 已启动（依据日志）"
LOGS=$(get_carla_logs)
BRIDGE_OK=0
if echo "$LOGS" | grep -qE "启动 CARLA Bridge \(Python\)|Python Bridge（CARLA 相机推流）|USE_PYTHON_BRIDGE=1"; then
  log_ok "日志确认为 Python Bridge"
  BRIDGE_OK=1
fi
if [ "$BRIDGE_OK" = "0" ] && echo "$LOGS" | grep -qE "启动 CARLA Bridge \(C\+\+\)|C\+\+ Bridge 编译成功|\[CARLA\].*未链接 LibCarla"; then
  log_ok "日志确认为 C++ Bridge"
  BRIDGE_OK=1
fi
if [ "$BRIDGE_OK" = "0" ]; then
  log_fail "日志中未找到 Bridge 启动标识"
  log_evidence "可 grep: 启动 CARLA Bridge|Python Bridge|C++ Bridge"
  FAILED=$((FAILED+1))
fi
echo ""

# ---------------------------------------------------------------------------
# 2) 发送 start_stream，等待后依据日志判断「已收 stream + 推流已启动」
# ---------------------------------------------------------------------------
log_section "2/6 start_stream → 日志中可见「已置 streaming」与「推流已启动」"
mqtt_pub "{\"type\":\"start_stream\",\"vin\":\"$VIN\",\"timestampMs\":0}"
sleep "$SLEEP_AFTER_START"
LOGS=$(get_carla_logs)

STREAMING_RECV=0
if echo "$LOGS" | grep -qE "已置 streaming=true（VIN 匹配）|已置 streaming=True|收到 start_stream.*vin_ok=True"; then
  log_ok "日志中已出现「已置 streaming」或「收到 start_stream」"
  STREAMING_RECV=1
else
  log_fail "日志中未出现 Bridge 收到 start_stream/streaming"
  log_evidence "$(echo "$LOGS" | grep -E "Control|start_stream|streaming" | tail -3 | sed 's/^/    /')"
  FAILED=$((FAILED+1))
fi

PUSH_STARTED=0
if echo "$LOGS" | grep -qE "\[ZLM\] 四路推流已启动|\[ZLM\].*推流已启动|推流已启动|四路相机已 spawn.*推流已启动"; then
  log_ok "日志中已出现「推流已启动」"
  PUSH_STARTED=1
else
  log_fail "日志中未出现推流已启动（Bridge 可能未真正推流）"
  log_evidence "$(echo "$LOGS" | grep -E "ZLM|推流|spawn" | tail -3 | sed 's/^/    /')"
  FAILED=$((FAILED+1))
fi
echo ""

# ---------------------------------------------------------------------------
# 3) 可选：ZLM 上四路流存在
# ---------------------------------------------------------------------------
log_section "3/6 ZLM 四路流（可选）"
if streams_present 2>/dev/null; then
  log_ok "ZLM 上四路流已就绪"
else
  log_evidence "ZLM 未检测到四路流或 API 不可用（若 2 已通过则 Bridge 已推流，可能延迟或网络不同）"
fi
echo ""

# ---------------------------------------------------------------------------
# 4) 发送 remote_control enable=true，依据日志判断
# ---------------------------------------------------------------------------
log_section "4/6 remote_control enable=true → 日志中可见"
mqtt_pub "{\"type\":\"remote_control\",\"vin\":\"$VIN\",\"enable\":true,\"timestampMs\":0}"
sleep "$SLEEP_AFTER_CTRL"
LOGS=$(get_carla_logs)
if echo "$LOGS" | grep -qE "\[Control\].*remote_control enable=true|收到 type=remote_control"; then
  log_ok "日志中已出现「remote_control enable=true」或「收到 type=remote_control」"
else
  log_fail "日志中未出现 remote_control 接收"
  log_evidence "$(echo "$LOGS" | grep -E "Control|remote" | tail -3 | sed 's/^/    /')"
  FAILED=$((FAILED+1))
fi
echo ""

# ---------------------------------------------------------------------------
# 5) 发送 drive，依据日志判断
# ---------------------------------------------------------------------------
log_section "5/6 drive → 日志中可见「收到 type=drive」"
mqtt_pub "{\"type\":\"drive\",\"vin\":\"$VIN\",\"steering\":0.1,\"throttle\":0.2,\"brake\":0,\"gear\":1,\"timestampMs\":0}"
sleep "$SLEEP_AFTER_CTRL"
LOGS=$(get_carla_logs)
if echo "$LOGS" | grep -q "\[Control\] 收到 type=drive"; then
  log_ok "日志中已出现「收到 type=drive」"
else
  log_fail "日志中未出现「收到 type=drive」"
  log_evidence "$(echo "$LOGS" | grep "Control" | tail -3 | sed 's/^/    /')"
  FAILED=$((FAILED+1))
fi
echo ""

# ---------------------------------------------------------------------------
# 6) vehicle/status 反馈（若有 mosquitto_sub 则订阅一条）
# ---------------------------------------------------------------------------
log_section "6/6 vehicle/status 反馈"
if command -v mosquitto_sub &>/dev/null; then
  STATUS_MSG=$(timeout 5 mosquitto_sub -h 127.0.0.1 -p 1883 -t "vehicle/status" -C 1 -W 4 2>/dev/null || true)
  if [ -n "$STATUS_MSG" ] && (echo "$STATUS_MSG" | grep -q "steering\|throttle\|remote_control_ack\|speed"); then
    log_ok "已收到 vehicle/status，内容含 steering/throttle/ack/speed"
  else
    log_fail "未在 4s 内收到 vehicle/status 或内容不符"
    FAILED=$((FAILED+1))
  fi
else
  log_evidence "未安装 mosquitto_sub，跳过订阅检查（Bridge 会周期性发布 vehicle/status）"
fi
echo ""

# ---------------------------------------------------------------------------
# 汇总
# ---------------------------------------------------------------------------
echo -e "${BOLD}========== 依据日志验证汇总 ==========${NC}"
if [ $FAILED -eq 0 ]; then
  echo -e "${GREEN}全部通过。依据 carla-server 日志判断：Bridge 已启动、已收 start_stream 并推流、已收 remote_control/drive、vehicle/status 有反馈。${NC}"
  echo ""
  echo "  结论：功能正常（日志证据充分）。"
  echo ""
  exit 0
fi
echo -e "${RED}失败项: $FAILED${NC}"
echo "  请根据上述 [FAIL] 与 (日志) 片段排查；完整日志: docker logs carla-server 2>&1 | tail -$LOG_TAIL"
echo ""
exit 1
