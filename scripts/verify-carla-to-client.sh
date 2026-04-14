#!/usr/bin/env bash
# 从 CARLA 到客户端的逐环节验证：CARLA 是否正常启动 → 地图/车辆是否加载 → Bridge 是否收 start_stream → ZLM 是否有四路流 → 客户端能否拉流/接管。
# 用法：./scripts/verify-carla-to-client.sh
# 前提：docker compose 已启动（postgres/keycloak/backend/zlmediakit/mosquitto），且已启动 CARLA（含 Bridge）。
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
# shellcheck source=lib/mqtt_control_json.sh
source "$SCRIPT_DIR/lib/mqtt_control_json.sh"

# 可选：带 carla 的 compose
COMPOSE_CARLA="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
COMPOSE_BASE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml"
ZLM_URL="${ZLM_URL:-http://127.0.0.1:80}"
ZLM_SECRET="${ZLM_SECRET:-}"
VIN_CARLA="carla-sim-001"
VIN="${VIN:-${VEHICLE_VIN:-}}"
VIN_PREFIX="${VIN:+${VIN}_}"
REQUIRED_STREAMS="${VIN_PREFIX}cam_front ${VIN_PREFIX}cam_rear ${VIN_PREFIX}cam_left ${VIN_PREFIX}cam_right"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[环节] $*${NC}"; }
log_ok()     { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_warn()   { echo -e "  ${YELLOW}[WARN] $*${NC}"; }
log_fail()   { echo -e "  ${RED}[FAIL] $*${NC}"; }

# 1) CARLA 容器是否在运行
check_carla_container() {
  log_section "1/6 检查 CARLA 容器是否运行"
  if docker ps --format '{{.Names}}' | grep -q 'carla-server'; then
    log_ok "容器 carla-server 运行中"
    return 0
  fi
  log_fail "未发现 carla-server 容器"
  echo "  启动: docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml up -d carla"
  echo "  或: ./scripts/start-all-nodes.sh（会启动 CARLA）"
  return 1
}

# 2) CARLA 日志：是否已加载地图、spawn 车辆、Bridge 已连 MQTT
check_carla_logs() {
  log_section "2/6 检查 CARLA/Bridge 日志（地图、车辆、MQTT）"
  local log
  log=$(docker logs carla-server 2>&1 | tail -150)
  if echo "$log" | grep -q "已连接，地图:\|get_map\|地图"; then
    log_ok "CARLA 已连接并加载地图"
  else
    log_warn "未在日志中看到「已连接，地图」；可能仍在启动或未加载地图"
  fi
  if echo "$log" | grep -q "车辆已 spawn\|spawn_actor"; then
    log_ok "车辆已 spawn"
  else
    log_fail "未看到车辆 spawn；检查 CARLA 是否报错"
  fi
  if echo "$log" | grep -q "已连接 broker\|已订阅 topic=vehicle/control"; then
    log_ok "Bridge 已连 MQTT 并订阅 vehicle/control"
  else
    log_fail "Bridge 未连 MQTT 或未订阅 vehicle/control"
    echo "  检查: docker logs carla-server 2>&1 | grep -E 'MQTT|broker|subscribe'"
    return 1
  fi
  return 0
}

# 3) 发送 start_stream (carla-sim-001)，等待 Bridge 开始推流
send_start_stream_and_wait() {
  log_section "3/6 发送 MQTT start_stream vin=$VIN_CARLA 并等待推流"
  local msg
  msg="$(mqtt_json_start_stream "$VIN_CARLA")"
  if command -v mosquitto_pub &>/dev/null; then
    mosquitto_pub -h 127.0.0.1 -p 1883 -t vehicle/control -m "$msg" 2>/dev/null && log_ok "已发送 start_stream (宿主机 mosquitto_pub)"
  else
    $COMPOSE_BASE exec -T mosquitto mosquitto_pub -h localhost -p 1883 -t vehicle/control -m "$msg" 2>/dev/null && log_ok "已发送 start_stream (容器内 mosquitto_pub)"
  fi
  log_section "等待 Bridge 启动四路推流（最多 25s）"
  local elapsed=0
  while [ $elapsed -lt 25 ]; do
    if docker logs carla-server 2>&1 | tail -80 | grep -q "四路相机已 spawn\|四路推流已启动\|ffmpeg 推流已启动"; then
      log_ok "Bridge 已开始推流（约 ${elapsed}s）"
      return 0
    fi
    sleep 2
    elapsed=$((elapsed + 2))
    echo "  ${elapsed}s: 等待 CARLA Bridge 推流..."
  done
  log_fail "超时未看到 Bridge 推流日志"
  echo "  查看: docker logs carla-server 2>&1 | tail -50"
  return 1
}

# 4) ZLM 上是否存在 app=teleop 的 cam_front/cam_rear/cam_left/cam_right
check_zlm_streams() {
  log_section "4/6 检查 ZLM 上是否存在四路流 (app=teleop)"
  local json
  json=$(curl -sf "${ZLM_URL}/index/api/getMediaList?secret=${ZLM_SECRET}&app=teleop" 2>/dev/null || \
         curl -sf "${ZLM_URL}/index/api/getMediaList?app=teleop" 2>/dev/null || echo '{"code":-1}')
  if ! echo "$json" | grep -q '"code":0'; then
    log_fail "ZLM getMediaList 失败或未返回 code:0"
    echo "  响应: $json"
    echo "  若为 Incorrect secret，可设置 ZLM_SECRET 与 deploy/zlm/config.ini 中 secret 一致"
    return 1
  fi
  local missing=""
  for s in $REQUIRED_STREAMS; do
    echo "$json" | grep -q "\"stream\":\"$s\"" || missing="$missing $s"
  done
  if [ -z "$missing" ]; then
    log_ok "四路流已在 ZLM: $REQUIRED_STREAMS"
    return 0
  fi
  log_fail "ZLM 缺少流:$missing"
  echo "  当前 teleop 流: $(echo "$json" | grep -o '"stream":"[^"]*"' | tr '\n' ' ')"
  return 1
}

# 5) 简要检查 Backend 会话创建（可选）
check_session_creation() {
  log_section "5/6 检查 Backend 会话创建 (POST sessions)"
  local url="${BACKEND_URL:-http://localhost:8081}"
  local kc="${KEYCLOAK_URL:-http://localhost:8080}"
  local token
  token=$(curl -s -X POST "$kc/realms/teleop/protocol/openid-connect/token" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "grant_type=password" -d "client_id=teleop-client" \
    -d "username=e2e-test" -d "password=e2e-test-password" | python3 -c "import sys,json; print(json.load(sys.stdin).get('access_token',''))" 2>/dev/null || echo "")
  if [ -z "$token" ]; then
    log_warn "无法获取 Keycloak token，跳过会话检查"
    return 0
  fi
  local code
  code=$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Authorization: Bearer $token" -H "Content-Type: application/json" "$url/api/v1/vins/$VIN_CARLA/sessions")
  if [ "$code" = "201" ]; then
    log_ok "POST sessions 返回 201"
  else
    log_warn "POST sessions 返回 $code（若为 503 可执行 ./scripts/ensure-seed-data.sh）"
  fi
  return 0
}

# 6) 汇总与客户端操作提示
summary() {
  log_section "6/6 汇总与客户端操作提示"
  echo "  若 1–4 均通过，则："
  echo "    1) 客户端选车 carla-sim-001 → 确认并进入驾驶"
  echo "    2) 点击「连接车端」（会再发 start_stream 并约 6s 后拉流）"
  echo "    3) 四路画面出现后点击「远驾接管」即可控制 CARLA 车辆"
  echo "  若「stream not found」：多为 Bridge 未推流或未收到 start_stream，请按 2、3 步查 CARLA 日志与 MQTT。"
}

# 执行各环节
FAIL=0
check_carla_container || FAIL=1
check_carla_logs || FAIL=1
if [ $FAIL -eq 0 ]; then
  send_start_stream_and_wait || FAIL=1
fi
check_zlm_streams || FAIL=1
check_session_creation || true
summary

if [ $FAIL -eq 0 ]; then
  echo ""
  log_ok "CARLA→ZLM 环节验证通过；可在客户端选 carla-sim-001 并点击「连接车端」进行接管验证。"
  exit 0
fi
echo ""
log_fail "部分环节未通过，请按上述提示排查。"
exit 1
