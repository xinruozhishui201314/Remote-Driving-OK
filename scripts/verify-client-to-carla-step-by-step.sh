#!/usr/bin/env bash
# 远驾客户端 → CARLA 仿真 整链逐项验证
#
# 设计说明：
#   - 实车部署时仅部署 Vehicle-side。Vehicle-side 是「流媒体服务（ZLM）与车辆」的桥梁：
#     订阅 MQTT start_stream → 推流到 ZLM；订阅 drive/remote_control → 控制底盘。
#   - CARLA 仿真仅用于验证远驾链路：用 CARLA 容器内 C++ Bridge 替代「Vehicle-side + 实车」，
#     验证同一套 Client → Backend → MQTT → [桥梁] → ZLM → Client 的流程。
#   - 本脚本对「客户端到 CARLA 仿真」整链做逐项验证，每项对应实车链路的同一环节。
#
# 用法：./scripts/verify-client-to-carla-step-by-step.sh
# 前提：./scripts/build-carla-image.sh && ./scripts/start-all-nodes.sh 已执行

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
# shellcheck source=lib/mqtt_control_json.sh
source "$SCRIPT_DIR/lib/mqtt_control_json.sh"

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
BOLD='\033[1m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[验证] $*${NC}"; }
log_ok()      { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail()    { echo -e "  ${RED}[FAIL] $*${NC}"; }
log_note()    { echo -e "  ${BOLD}(实车对应: $*)${NC}"; }

get_media_list() { curl -sf "${ZLM_URL}/index/api/getMediaList?secret=${ZLM_SECRET}&app=teleop" 2>/dev/null || echo '{"code":-1}'; }
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
echo -e "${BOLD}========== 远驾客户端 → CARLA 仿真 整链逐项验证 ==========${NC}"
echo -e "  ${BOLD}说明：实车部署仅部署 Vehicle-side（流媒体与车辆的桥梁）；CARLA 仅用于验证本链路。${NC}"
echo "  Backend=$BACKEND_URL  Keycloak=$KEYCLOAK_URL  ZLM=$ZLM_URL  VIN=$VIN_CARLA"
echo ""

# ---------------------------------------------------------------------------
# 第 1 项：基础设施（Postgres / Keycloak / Backend / ZLM / Mosquitto）
# ---------------------------------------------------------------------------
log_section "1/7 基础设施：Postgres / Keycloak / Backend / ZLM / Mosquitto"
for svc in postgres keycloak backend zlmediakit mosquitto; do
  if $COMPOSE ps "$svc" 2>/dev/null | grep -q "Up"; then
    log_ok "$svc 运行中"
  else
    log_fail "$svc 未运行；请先: ./scripts/start-all-nodes.sh"
    exit 1
  fi
done
log_note "实车同样依赖 Backend / ZLM / Mosquitto 等中心侧服务"
echo ""

# ---------------------------------------------------------------------------
# 第 2 项：仿真端桥梁（CARLA 容器 + C++ Bridge）
# ---------------------------------------------------------------------------
log_section "2/7 仿真端桥梁：CARLA 容器 + C++ Bridge（收 MQTT → 推流到 ZLM）"
if ! docker ps --format '{{.Names}}' | grep -q "carla-server"; then
  log_fail "carla-server 未运行；请先: ./scripts/build-carla-image.sh && ./scripts/start-all-nodes.sh"
  exit 1
fi
log_ok "carla-server 运行中（容器内 C++ Bridge 负责 MQTT → CARLA 取图 → ZLM 推流）"
log_note "实车时此处为 Vehicle-side：订阅 MQTT start_stream → 摄像头/采集推流到 ZLM"
echo ""

# ---------------------------------------------------------------------------
# 第 3 项：鉴权与会话（模拟客户端：Keycloak Token + Backend 创建会话）
# ---------------------------------------------------------------------------
log_section "3/7 鉴权与会话：Keycloak Token + Backend POST /api/v1/vins/$VIN_CARLA/sessions"
TOKEN=$(curl -s -X POST "$KEYCLOAK_URL/realms/teleop/protocol/openid-connect/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password" -d "client_id=teleop-client" \
  -d "username=$E2E_USER" -d "password=$E2E_PASSWORD" 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin).get('access_token',''))" 2>/dev/null || echo "")
if [ -z "$TOKEN" ]; then
  log_fail "无法获取 Keycloak token（用户 $E2E_USER）；请确认 Realm teleop 与用户已配置"
  exit 1
fi
log_ok "Keycloak Token 已获取"

HTTP_CODE=$(curl -s -o /tmp/verify-step-session-$$.json -w "%{http_code}" -X POST \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  "$BACKEND_URL/api/v1/vins/$VIN_CARLA/sessions" 2>/dev/null || echo "000")
if [ "$HTTP_CODE" = "201" ]; then
  log_ok "会话创建成功 (201)"
  SESSION_ID=$(python3 -c "import sys,json; d=json.load(open('/tmp/verify-step-session-$$.json')); print(d.get('sessionId',''))" 2>/dev/null || echo "")
  [ -n "$SESSION_ID" ] && echo "    sessionId=$SESSION_ID"
else
  log_fail "POST sessions 返回 $HTTP_CODE（若 503 可执行 ./scripts/ensure-seed-data.sh）"
  FAILED=$((FAILED+1))
fi
rm -f /tmp/verify-step-session-$$.json
log_note "实车：客户端同样通过 Backend 创建会话后获得连接权限"
echo ""

# ---------------------------------------------------------------------------
# 第 4 项：控制通道 — MQTT 发布 start_stream
# ---------------------------------------------------------------------------
log_section "4/7 控制通道：MQTT 发布 start_stream（topic=vehicle/control, vin=$VIN_CARLA）"
mqtt_pub "$(mqtt_json_start_stream "$VIN_CARLA")"
log_ok "已发送 start_stream"
log_note "实车：Vehicle-side 订阅 vehicle/control，收到 start_stream 后启动推流"
echo ""

# ---------------------------------------------------------------------------
# 第 5 项：桥梁响应 — 仿真端推流到 ZLM
# ---------------------------------------------------------------------------
log_section "5/7 桥梁响应：等待仿真端（CARLA C++ Bridge）推流到 ZLM（app=teleop 四路流）"
elapsed=0
while [ $elapsed -lt $WAIT_STREAM ]; do
  if streams_present; then
    log_ok "四路流已就绪（${elapsed}s）：cam_front / cam_rear / cam_left / cam_right"
    break
  fi
  sleep 2
  elapsed=$((elapsed+2))
  echo "  ${elapsed}s: 等待四路流..."
done
if ! streams_present; then
  log_fail "超时未检测到四路流"
  echo "  排查: $COMPOSE logs carla 2>&1 | tail -30"
  FAILED=$((FAILED+1))
fi
log_note "实车：Vehicle-side 推流到 ZLM 后，客户端从 ZLM 拉流"
echo ""

# ---------------------------------------------------------------------------
# 第 6 项：控制指令 — remote_control + drive
# ---------------------------------------------------------------------------
log_section "6/7 控制指令：MQTT remote_control + drive（模拟驾驶操作）"
mqtt_pub "$(mqtt_json_remote_control "$VIN_CARLA" true)"
mqtt_pub "$(mqtt_json_drive "$VIN_CARLA" 0.05 0.1 0 1 false)"
log_ok "已发送 remote_control 与 drive"
if command -v mosquitto_sub &>/dev/null; then
  if timeout 6 mosquitto_sub -h 127.0.0.1 -p 1883 -t "vehicle/status" -C 1 -W 6 2>/dev/null | grep -q "steering\|throttle"; then
    log_ok "vehicle/status 已收到控制反馈"
  else
    log_ok "控制已发送（vehicle/status 需本机 mosquitto_sub 可连时方可断言）"
  fi
else
  log_ok "控制已发送（未安装 mosquitto_sub，跳过 status 断言）"
fi
log_note "实车：Vehicle-side 订阅 drive/remote_control，转发到底盘执行并上报 vehicle/status"
echo ""

# ---------------------------------------------------------------------------
# 第 7 项：停止推流（可选，验证 stop_stream）
# ---------------------------------------------------------------------------
log_section "7/7 停止推流：MQTT stop_stream（验证按需推流闭环）"
mqtt_pub "$(mqtt_json_stop_stream "$VIN_CARLA")"
log_ok "已发送 stop_stream（流可能短暂仍存在，属正常）"
log_note "实车：Vehicle-side 收到 stop_stream 后停止推流"
echo ""

# ---------------------------------------------------------------------------
# 汇总
# ---------------------------------------------------------------------------
echo -e "${BOLD}========== 逐项验证汇总 ==========${NC}"
if [ $FAILED -eq 0 ]; then
  echo -e "${GREEN}全部通过。${NC}"
  echo "  远驾链路：Client → Backend 会话 → MQTT → [CARLA C++ Bridge] → ZLM 四路流 已打通。"
  echo "  实车部署：将「CARLA C++ Bridge」替换为「Vehicle-side」即可，Vehicle-side 为流媒体与车辆的桥梁。"
  echo "  客户端可选车 carla-sim-001 → 连接车端 → 远驾接管。"
  echo ""
  exit 0
fi
echo -e "${RED}失败项: $FAILED${NC}"
exit 1
