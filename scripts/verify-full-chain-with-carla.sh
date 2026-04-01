#!/usr/bin/env bash
# 全链路 + 仿真验证：在「所有节点已启动」前提下，先验证车端路径（E2E VIN），再验证 CARLA 仿真路径（carla-sim-001）。
# 用法：
#   ./scripts/verify-full-chain-with-carla.sh
# 前提：
#   - 已启动全链路（含 vehicle）：如 bash scripts/start-full-chain.sh no-client 或 manual
#   - 验证 CARLA 路径前需在宿主机启动 CARLA 与 carla-bridge（见输出提示）

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml"
ZLM_URL="${ZLM_URL:-http://127.0.0.1:80}"
ZLM_SECRET="${ZLM_SECRET:-}"
VIN="${VIN:-${VEHICLE_VIN:-}}"
VIN_PREFIX="${VIN:+${VIN}_}"
REQUIRED_STREAMS="${VIN_PREFIX}cam_front ${VIN_PREFIX}cam_rear ${VIN_PREFIX}cam_left ${VIN_PREFIX}cam_right"
WAIT_STREAM_MAX=45
SEND_INTERVAL=5

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[LOG] $*${NC}"; }
log_ok()     { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_warn()   { echo -e "  ${YELLOW}[WARN] $*${NC}"; }
log_fail()   { echo -e "  ${YELLOW}[FAIL] $*${NC}"; }

get_media_list() {
  curl -sf "${ZLM_URL}/index/api/getMediaList?secret=${ZLM_SECRET}&app=teleop" 2>/dev/null || echo '{"code":-1}'
}

check_streams_present() {
  local json
  json="$(get_media_list)"
  if ! echo "$json" | grep -q '"code":0'; then
    return 1
  fi
  for s in $REQUIRED_STREAMS; do
    echo "$json" | grep -q "\"stream\":\"$s\"" || return 1
  done
  return 0
}

mqtt_pub_vin() {
  local vin="$1"
  local type="${2:-start_stream}"
  local msg="{\"type\":\"$type\",\"vin\":\"$vin\",\"timestampMs\":0}"
  log_section "MQTT 发布 topic=vehicle/control payload=$msg"
  if command -v mosquitto_pub &>/dev/null; then
    mosquitto_pub -h 127.0.0.1 -p 1883 -t vehicle/control -m "$msg" 2>/dev/null && return 0
  fi
  $COMPOSE exec -T mosquitto mosquitto_pub -h localhost -p 1883 -t vehicle/control -m "$msg" 2>/dev/null || true
}

wait_for_streams() {
  local vin_label="$1"
  local vin_value="$2"
  local elapsed=0
  while [ $elapsed -lt $WAIT_STREAM_MAX ]; do
    if check_streams_present; then
      log_ok "${vin_label} 推流就绪（四路流已出现在 ZLM）"
      return 0
    fi
    if [ $((elapsed % SEND_INTERVAL)) -eq 0 ] && [ $elapsed -gt 0 ]; then
      log_section "重发 start_stream vin=$vin_value (${elapsed}s)"
      mqtt_pub_vin "$vin_value" "start_stream" 2>/dev/null || true
    fi
    if [ $((elapsed % 10)) -eq 0 ] && [ $elapsed -gt 0 ]; then
      local json
      json="$(get_media_list)"
      local code=$(echo "$json" | grep -o '"code":[^,}]*' | head -1)
      log_section "ZLM getMediaList app=teleop $code (当前流: $(echo "$json" | grep -o '"stream":"[^"]*"' | tr '\n' ' '))"
    fi
    sleep 2
    elapsed=$((elapsed + 2))
    echo "  ${elapsed}s: 等待 ${vin_label} 四路流..."
  done
  return 1
}

echo -e "${CYAN}========== 全链路 + 仿真验证（所有节点已启动）==========${NC}"
echo ""

# 1. 检查必要服务
echo "[1/4] 检查服务..."
log_section "检查 Compose 服务: postgres backend zlmediakit mosquitto vehicle"
for svc in postgres backend zlmediakit mosquitto vehicle; do
  if ! $COMPOSE ps "$svc" 2>/dev/null | grep -q "Up"; then
    echo -e "${RED}请先启动全链路（含 vehicle）: bash scripts/start-full-chain.sh no-client 或 ./scripts/start-all-nodes.sh${NC}"
    log_fail "当前 $svc 未运行；可执行: $COMPOSE ps"
    exit 1
  fi
  log_ok "$svc 运行中"
done
echo ""

# 2. 等待车端订阅
echo "[2/4] 等待车端 MQTT 订阅（最多 60s）..."
VEHICLE_READY=0
for i in $(seq 1 20); do
  if $COMPOSE logs vehicle 2>&1 | tail -100 | grep -q "已订阅主题\|Vehicle-side Controller 运行中"; then
    VEHICLE_READY=1
    log_ok "车端已订阅 vehicle/control（第 ${i} 次检查）"
    break
  fi
  sleep 3
done
if [ $VEHICLE_READY -eq 0 ]; then
  log_warn "未检测到车端订阅（车端可能仍在编译）；将继续发 start_stream 尝试"
  log_section "车端日志末尾: $COMPOSE logs vehicle 2>&1 | tail -15"
  $COMPOSE logs vehicle 2>&1 | tail -15 | sed 's/^/    /' || true
fi
echo ""

# 3. 验证车端路径（E2ETESTVIN0000001）
echo "[3/4] 验证车端路径（VIN=E2ETESTVIN0000001）..."
mqtt_pub_vin "E2ETESTVIN0000001" "start_stream"
if wait_for_streams "车端（E2E）" "E2ETESTVIN0000001"; then
  E2E_OK=1
else
  E2E_OK=0
  log_fail "车端路径超时"
  log_section "排查车端: $COMPOSE logs vehicle 2>&1 | grep -E 'start_stream|VEHICLE_VIN|推流|PUSH|control' | tail -20"
  $COMPOSE logs vehicle 2>&1 | grep -E "start_stream|VEHICLE_VIN|推流|PUSH|control|已订阅" | tail -20 | sed 's/^/    /' || true
  log_section "确认车端 VEHICLE_VIN 与消息 vin 一致（默认 E2ETESTVIN0000001）；推流脚本: docker-compose.vehicle.dev.yml 中 VEHICLE_PUSH_SCRIPT"
fi
echo ""

# 4. 验证 CARLA 仿真路径（carla-sim-001）
echo "[4/4] 验证 CARLA 仿真路径（VIN=carla-sim-001）..."
log_section "若未启动 CARLA 与 carla-bridge，此步将超时；启动后重跑本脚本可单独验证 CARLA 路径"
mqtt_pub_vin "E2ETESTVIN0000001" "stop_stream" 2>/dev/null || true
sleep 3
mqtt_pub_vin "carla-sim-001" "start_stream"
if wait_for_streams "CARLA 仿真（carla-sim-001）" "carla-sim-001"; then
  CARLA_OK=1
else
  CARLA_OK=0
  log_fail "CARLA 路径超时"
  log_section "排查: 1) CARLA 容器: docker ps | grep carla; docker logs carla-server 2>&1 | tail -20"
  log_section "       2) 若 Bridge 在容器内: docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml logs carla 2>&1 | tail -30"
  log_section "       3) 若 Bridge 在宿主机: tail -30 $PROJECT_ROOT/carla-bridge.log"
  [ -f "$PROJECT_ROOT/carla-bridge.log" ] && tail -15 "$PROJECT_ROOT/carla-bridge.log" 2>/dev/null | sed 's/^/    /' || true
  echo "  启动方式: 推荐 ./scripts/start-all-nodes.sh（Bridge 在容器内）"
fi
echo ""

# 汇总
echo -e "${CYAN}========== 验证汇总 ==========${NC}"
if [ "${E2E_OK:-0}" -eq 1 ]; then
  echo -e "  车端路径（E2ETESTVIN0000001）: ${GREEN}通过${NC}"
else
  echo -e "  车端路径（E2ETESTVIN0000001）: ${YELLOW}未通过或超时${NC}"
fi
if [ "${CARLA_OK:-0}" -eq 1 ]; then
  echo -e "  CARLA 仿真路径（carla-sim-001）: ${GREEN}通过${NC}"
else
  echo -e "  CARLA 仿真路径（carla-sim-001）: ${YELLOW}未通过或未启动 bridge${NC}"
fi
echo ""

if [ "${E2E_OK:-0}" -eq 1 ] || [ "${CARLA_OK:-0}" -eq 1 ]; then
  echo -e "${GREEN}至少一条链路验证通过；可启动客户端选车并点击「连接车端」进行人工验证。${NC}"
  exit 0
fi
echo -e "${RED}两条链路均未在限定时间内就绪。${NC}"
log_section "排查建议:"
echo "  车端: $COMPOSE logs vehicle 2>&1 | tail -50"
echo "  ZLM 流: curl -s '${ZLM_URL}/index/api/getMediaList?secret=xxx&app=teleop'"
echo "  MQTT: docker exec \$(docker ps -q -f name=mosquitto) mosquitto_sub -h localhost -t vehicle/control -v -C 1"
exit 1
