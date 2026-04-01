#!/bin/bash
# 自动化验证「start_stream → 车端推流 → ZLM 四路流就绪」全链路
# 用法：bash scripts/verify-stream-e2e.sh
# 依赖：全链路已启动（或本脚本先 start-no-client），且 vehicle 容器可收到 MQTT 并执行推流脚本

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml"
ZLM_URL="${ZLM_URL:-http://127.0.0.1:80}"
# ZLM secret 与 deploy/zlm/config.ini 保持一致；若通过环境变量传入则优先使用
ZLM_SECRET="${ZLM_SECRET:-035c73f7-bb6b-4889-a715-d9eb2d1925cc}"
MQTT_PORT="${MQTT_PORT:-1883}"
# VIN：用于构造 VIN-prefixed 流名（{VIN}_cam_front 等）；为空时回退使用无前缀流名
VIN="${VIN:-${VEHICLE_VIN:-}}"
VIN_PREFIX="${VIN:+${VIN}_}"
REQUIRED_STREAMS="${VIN_PREFIX}cam_front ${VIN_PREFIX}cam_rear ${VIN_PREFIX}cam_left ${VIN_PREFIX}cam_right"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'
log_section() { echo -e "${CYAN}[LOG] $*${NC}"; }
log_ok()     { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_warn()   { echo -e "  ${YELLOW}[WARN] $*${NC}"; }
log_fail()   { echo -e "  ${YELLOW}[FAIL] $*${NC}"; }

# 从 ZLM 拉取 getMediaList：宿主机/容器内 secret 可能被拒(-100)，改用 HTTP-FLV 探测
get_media_list() {
  local json
  json="$(curl -sf "${ZLM_URL}/index/api/getMediaList?secret=${ZLM_SECRET}&app=teleop" 2>/dev/null || echo '')"
  if [ -n "$json" ] && echo "$json" | grep -q '"code":0' && echo "$json" | grep -q '"data":'; then
    echo "$json"
    return
  fi
  $COMPOSE run --rm --no-deps -e ZLM_SECRET="$ZLM_SECRET" backend curl -sf "http://zlmediakit:80/index/api/getMediaList?secret=${ZLM_SECRET}&app=teleop" 2>/dev/null || echo '{"code":-1}'
}

# 当 getMediaList 因 secret 失败时，通过 HTTP-FLV 探测四路流是否可播（无需 API secret）
check_streams_via_flv() {
  local base="${ZLM_URL%/}"
  local ok=1
  for s in $REQUIRED_STREAMS; do
    if ! curl -sfI -m 3 "${base}/teleop/${s}.live.flv" 2>/dev/null | head -1 | grep -q "200"; then
      ok=0
      break
    fi
  done
  [ "$ok" -eq 1 ]
}

CARLA_SERVER_UP=0
if docker ps --format '{{.Names}}' | grep -q '^carla-server$'; then
  CARLA_SERVER_UP=1
  VIN_E2E="${CARLA_VIN:-carla-sim-001}"
else
  VIN_E2E="${VEHICLE_VIN:-E2ETESTVIN0000001}"
fi

echo "=== 四路流 E2E 自动化验证（start_stream → 车端推流 → ZLM）==="
log_section "ZLM_URL=$ZLM_URL VIN_E2E=$VIN_E2E"
echo ""

# 1. 检查服务
echo "[1/5] 检查服务状态..."
log_section "检查 Compose 服务: teleop-postgres backend zlmediakit teleop-mosquitto vehicle"
for svc in teleop-postgres backend zlmediakit teleop-mosquitto vehicle; do
  if [ "$svc" = "vehicle" ] && [ "$CARLA_SERVER_UP" -eq 1 ]; then
    if $COMPOSE ps "$svc" 2>/dev/null | grep -q "Up"; then
      log_ok "$svc 运行中"
    else
      log_warn "carla-server 已运行：跳过强制 vehicle（CARLA 仿真推流可不依赖 teleop-vehicle）"
    fi
    continue
  fi
  if ! $COMPOSE ps "$svc" 2>/dev/null | grep -q "Up"; then
    log_fail "当前 $svc 未运行"
    echo "  请先启动全链路: ./scripts/start-full-chain.sh 或 ./scripts/start-all-nodes.sh"
    exit 1
  fi
  log_ok "$svc 运行中"
done
echo ""

# 2. 等待车端 MQTT 已订阅（首次启动需编译，最多等 90s），再发 start_stream 才有效
echo "[2/5] 等待车端 MQTT 已订阅（最多 90s）..."
VEHICLE_READY_MAX=90
VEHICLE_READY_INTERVAL=3
VEHICLE_ELAPSED=0
VEHICLE_READY=0
while [ $VEHICLE_ELAPSED -lt $VEHICLE_READY_MAX ]; do
  if $COMPOSE logs vehicle 2>&1 | grep -q "已订阅主题"; then
    VEHICLE_READY=1
    log_ok "车端已订阅 vehicle/control（${VEHICLE_ELAPSED}s）"
    break
  fi
  if docker ps --format '{{.Names}}' | grep -q '^carla-server$' && docker logs carla-server 2>&1 | tail -80 | grep -qE "vehicle/control|已订阅|subscribe"; then
    VEHICLE_READY=1
    log_ok "CARLA Bridge 已关联 MQTT（${VEHICLE_ELAPSED}s）"
    break
  fi
  sleep $VEHICLE_READY_INTERVAL
  VEHICLE_ELAPSED=$((VEHICLE_ELAPSED + VEHICLE_READY_INTERVAL))
  echo "  ${VEHICLE_ELAPSED}s: 等待车端编译并订阅..."
done
if [ $VEHICLE_READY -eq 0 ]; then
  log_warn "超时未检测到车端订阅，将继续发送 start_stream 并等待流"
  log_section "车端日志末尾: $COMPOSE logs vehicle 2>&1 | tail -20"
  $COMPOSE logs vehicle 2>&1 | tail -20 | sed 's/^/    /' || true
fi
echo ""

# 3. 发 MQTT start_stream（轮询期间每 5s 重发）；VIN_E2E 已在脚本开头按 CARLA/车端选定
MSG='{"type":"start_stream","vin":"'"$VIN_E2E"'","timestampMs":0}'
mqtt_pub() {
  if command -v mosquitto_pub &>/dev/null && mosquitto_pub -h 127.0.0.1 -p "${MQTT_PORT:-1883}" -t vehicle/control -m "$MSG" 2>/dev/null; then
    return 0
  fi
  $COMPOSE exec -T teleop-mosquitto mosquitto_pub -h localhost -p 1883 -t vehicle/control -m '{"type":"start_stream","vin":"'"$VIN_E2E"'","timestampMs":0}' 2>/dev/null || true
}
log_section "发送 MQTT topic=vehicle/control payload=$MSG"
echo "[3/5] 发送 MQTT start_stream（等待流期间每 5s 重发）..."
mqtt_pub

# 4. 轮询 ZLM getMediaList 直到四路流就绪或超时（默认 60s，可用 STREAM_E2E_WAIT_MAX 覆盖）
WAIT_MAX="${STREAM_E2E_WAIT_MAX:-60}"
echo "[4/5] 等待 ZLM 上 teleop 四路流就绪（最多 ${WAIT_MAX}s）..."
INTERVAL=2
SEND_INTERVAL=5
ELAPSED=0
FOUND=0
LAST_SENT=0

while [ $ELAPSED -lt $WAIT_MAX ]; do
  JSON="$(get_media_list)"
  # 每 10s 打印一次 ZLM 状态，便于排查
  if [ $((ELAPSED % 10)) -eq 0 ] && [ $ELAPSED -gt 0 ]; then
    CODE=$(echo "$JSON" | grep -o '"code":[^,}]*' | head -1)
    STREAMS=$(echo "$JSON" | grep -o '"stream":"[^"]*"' | sed 's/"stream":"//;s/"//g' | tr '\n' ' ')
    log_section "ZLM 状态 ${ELAPSED}s: $CODE streams=[${STREAMS:-无}]"
  fi
  if echo "$JSON" | grep -q '"code":0' && echo "$JSON" | grep -q '"data":'; then
    MISSING=""
    for s in $REQUIRED_STREAMS; do
      if ! echo "$JSON" | grep -q "\"stream\":\"$s\""; then
        MISSING="$MISSING $s"
      fi
    done
    if [ -z "$MISSING" ]; then
      FOUND=1
      break
    fi
  fi
  # getMediaList 可能因 secret 返回 -100，改用 HTTP-FLV 探测
  if [ $FOUND -eq 0 ] && check_streams_via_flv; then
    FOUND=1
    break
  fi
  if [ $ELAPSED -ge $LAST_SENT ] && [ $((ELAPSED - LAST_SENT)) -ge $SEND_INTERVAL ]; then
    log_section "重发 MQTT start_stream (${ELAPSED}s)"
    mqtt_pub
    LAST_SENT=$ELAPSED
  fi
  sleep $INTERVAL
  ELAPSED=$((ELAPSED + INTERVAL))
  echo "  ${ELAPSED}s: 等待流就绪..."
done

if [ $FOUND -eq 0 ]; then
  echo "[5/5] VERIFY_FAIL: 超时未在 ZLM 上发现四路流 (app=teleop: $REQUIRED_STREAMS)"
  log_fail "排查: 1) 车端是否收到 start_stream 且 VIN 匹配 2) 推流脚本是否执行 3) ZLM 是否收到 RTMP"
  log_section "车端日志（start_stream/VEHICLE_VIN/推流）: $COMPOSE logs vehicle 2>&1 | grep -E 'start_stream|VEHICLE_VIN|Control|推流|已订阅' | tail -25"
  $COMPOSE logs vehicle 2>&1 | grep -E "start_stream|VEHICLE_VIN|Control|推流|已订阅|MQTT" | tail -25 | sed 's/^/    /' || true
  log_section "ZLM getMediaList 当前返回:"
  JSON="$(get_media_list)"
  echo "$JSON" | head -c 500
  echo ""
  echo "  或手动探测: curl -sI '${ZLM_URL%/}/teleop/cam_front.live.flv'"
  exit 1
fi

log_ok "ZLM 上已存在四路流 (${VIN_PREFIX}cam_front, ..., ${VIN_PREFIX}cam_right)"
echo "[5/5] VERIFY_OK"
exit 0
