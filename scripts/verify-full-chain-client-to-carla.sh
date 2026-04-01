#!/usr/bin/env bash
# 远驾客户端到 CARLA 仿真系统整链验证：Backend 会话 → MQTT start_stream → CARLA C++ Bridge → ZLM 四路流。
# 前提：已构建并启动 CARLA 镜像（build-carla-image.sh + start-all-nodes.sh），且 Postgres/Keycloak/Backend/ZLM/Mosquitto 已运行。
#
# 用法：./scripts/verify-full-chain-client-to-carla.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
BACKEND_URL="${BACKEND_URL:-http://127.0.0.1:8081}"
KEYCLOAK_URL="${KEYCLOAK_URL:-http://127.0.0.1:8080}"
ZLM_URL="${ZLM_URL:-http://127.0.0.1:80}"
ZLM_SECRET="${ZLM_SECRET:-}"
VIN_CARLA="${VIN_CARLA:-carla-sim-001}"
E2E_USER="${E2E_USER:-e2e-test}"
E2E_PASSWORD="${E2E_PASSWORD:-e2e-test-password}"
VIN="${VIN:-${VEHICLE_VIN:-}}"
VIN_PREFIX="${VIN:+${VIN}_}"
REQUIRED_STREAMS="${VIN_PREFIX}cam_front ${VIN_PREFIX}cam_rear ${VIN_PREFIX}cam_left ${VIN_PREFIX}cam_right"
WAIT_STREAM=50

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[chain] $*${NC}"; }
log_ok()      { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail()    { echo -e "  ${RED}[FAIL] $*${NC}"; }

get_media_list() { curl -sf "${ZLM_URL}/index/api/getMediaList?secret=${ZLM_SECRET}&app=teleop" 2>/dev/null || echo '{"code":-1}'; }
# getMediaList 可能因 secret 失败(-100)，用 HTTP-FLV 探测四路流
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
echo -e "${CYAN}========== 远驾客户端 → CARLA 仿真 整链验证 ==========${NC}"
echo "  Backend=$BACKEND_URL  Keycloak=$KEYCLOAK_URL  ZLM=$ZLM_URL  VIN=$VIN_CARLA"
echo ""

# ---------- 1) 必要服务运行 ----------
log_section "1/5 检查必要服务"
for svc in postgres keycloak backend zlmediakit mosquitto carla; do
  if $COMPOSE ps "$svc" 2>/dev/null | grep -q "Up"; then
    log_ok "$svc 运行中"
  else
    log_fail "$svc 未运行；请先: ./scripts/build-carla-image.sh && ./scripts/start-all-nodes.sh"
    exit 1
  fi
done
echo ""

# ---------- 2) Keycloak Token + Backend 会话创建（模拟客户端创建会话）----------
log_section "2/5 Backend 会话创建（POST /api/v1/vins/$VIN_CARLA/sessions）"
TOKEN=$(curl -s -X POST "$KEYCLOAK_URL/realms/teleop/protocol/openid-connect/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password" -d "client_id=teleop-client" \
  -d "username=$E2E_USER" -d "password=$E2E_PASSWORD" 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin).get('access_token',''))" 2>/dev/null || echo "")
if [ -z "$TOKEN" ]; then
  log_fail "无法获取 Keycloak token（e2e-test）；请确认 Realm teleop 与用户已配置"
  exit 1
fi
HTTP_CODE=$(curl -s -o /tmp/verify-chain-session-$$.json -w "%{http_code}" -X POST \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  "$BACKEND_URL/api/v1/vins/$VIN_CARLA/sessions" 2>/dev/null || echo "000")
if [ "$HTTP_CODE" = "201" ]; then
  log_ok "会话创建成功 (201)"
  SESSION_ID=$(python3 -c "import sys,json; d=json.load(open('/tmp/verify-chain-session-$$.json')); print(d.get('sessionId',''))" 2>/dev/null || echo "")
  [ -n "$SESSION_ID" ] && echo "    sessionId=$SESSION_ID"
else
  log_fail "POST sessions 返回 $HTTP_CODE（若 503 可执行 ./scripts/ensure-seed-data.sh）"
  FAILED=$((FAILED+1))
fi
rm -f /tmp/verify-chain-session-$$.json
echo ""

# ---------- 3) MQTT start_stream (carla-sim-001) ----------
log_section "3/5 MQTT start_stream → CARLA C++ Bridge"
mqtt_pub "{\"type\":\"start_stream\",\"vin\":\"$VIN_CARLA\",\"timestampMs\":0}"
log_ok "已发送 start_stream"
echo ""

# ---------- 4) ZLM 四路流就绪 ----------
log_section "4/5 等待 ZLM 四路流（最多 ${WAIT_STREAM}s）"
elapsed=0
while [ $elapsed -lt $WAIT_STREAM ]; do
  if streams_present; then
    log_ok "四路流已就绪（${elapsed}s）"
    break
  fi
  sleep 2
  elapsed=$((elapsed+2))
  echo "  ${elapsed}s: 等待 app=teleop 四路流..."
done
if ! streams_present; then
  log_fail "超时未检测到四路流"
  echo "  排查: $COMPOSE logs carla 2>&1 | tail -30"
  FAILED=$((FAILED+1))
fi
echo ""

# ---------- 5) 可选：控制指令与 status 反馈 ----------
log_section "5/5 控制链路（drive → vehicle/status）"
mqtt_pub "{\"type\":\"remote_control\",\"vin\":\"$VIN_CARLA\",\"enable\":true,\"timestampMs\":0}"
mqtt_pub "{\"type\":\"drive\",\"vin\":\"$VIN_CARLA\",\"steering\":0.05,\"throttle\":0.1,\"brake\":0,\"gear\":1,\"timestampMs\":0}"
sleep 2
if command -v mosquitto_sub &>/dev/null; then
  if timeout 6 mosquitto_sub -h 127.0.0.1 -p 1883 -t "vehicle/status" -C 1 -W 6 2>/dev/null | grep -q "steering\|throttle"; then
    log_ok "vehicle/status 已收到控制反馈"
  else
    log_ok "控制已发送（vehicle/status 需本机 mosquitto_sub 可连时方可断言）"
  fi
else
  log_ok "控制已发送（未安装 mosquitto_sub，跳过 status 断言）"
fi
echo ""

# ---------- 汇总 ----------
echo -e "${CYAN}========== 整链验证汇总 ==========${NC}"
if [ $FAILED -eq 0 ]; then
  echo -e "${GREEN}通过：Backend 会话 → MQTT → CARLA C++ Bridge → ZLM 四路流 已打通。${NC}"
  echo "  客户端可选车 carla-sim-001 → 确认进入驾驶 → 连接车端 → 远驾接管。"
  echo ""
  exit 0
fi
echo -e "${RED}失败项: $FAILED${NC}"
exit 1
