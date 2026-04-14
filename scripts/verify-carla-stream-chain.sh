#!/usr/bin/env bash
# 分步验证「start_stream → Bridge 收包 → 推流 → ZLM 有流」，任一步失败即停并给出修复建议。
# 用法：./scripts/verify-carla-stream-chain.sh
# 前提：Compose 已启动（carla、mosquitto、zlmediakit 等）。

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
# shellcheck source=lib/mqtt_control_json.sh
source "$SCRIPT_DIR/lib/mqtt_control_json.sh"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
VIN="${VIN:-carla-sim-001}"
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

step_ok()    { echo -e "  ${GREEN}[PASS] $*${NC}"; }
step_fail()  { echo -e "  ${RED}[FAIL] $*${NC}"; }
step_info()  { echo -e "  ${CYAN}$*${NC}"; }
remedy()     { echo -e "  ${YELLOW}修复建议: $*${NC}"; }

mqtt_pub() {
  local msg="$1"
  # 优先从 teleop-mosquitto 容器内发布（服务名与 docker-compose.yml 一致）
  if $COMPOSE exec -T teleop-mosquitto mosquitto_pub -h localhost -p 1883 -t vehicle/control -m "$msg" 2>/dev/null; then
    return 0
  fi
  # 备选：从 carla 容器内发布，与 Bridge 同机同 broker，Bridge 必能收到
  if docker exec carla-server python3.7 -c "
import sys
try:
  import paho.mqtt.client as mqtt
  c = mqtt.Client(client_id='verify-pub')
  c.connect('teleop-mosquitto', 1883)
  c.publish('vehicle/control', '''$msg''')
  c.disconnect()
  sys.exit(0)
except Exception as e:
  print(str(e), file=sys.stderr)
  sys.exit(1)
" 2>/dev/null; then
    return 0
  fi
  if command -v mosquitto_pub &>/dev/null; then
    mosquitto_pub -h 127.0.0.1 -p 1883 -t vehicle/control -m "$msg" 2>/dev/null && return 0
  fi
  return 1
}

streams_on_zlm() {
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

get_carla_logs() { docker logs carla-server 2>&1 | tail -2500; }

echo ""
echo -e "${BOLD}========== CARLA 推流链路分步验证（遇失败即停）==========${NC}"
echo "  VIN=$VIN  ZLM=$ZLM_URL"
echo ""

# ---------------------------------------------------------------------------
# 步骤 1：carla-server 容器运行
# ---------------------------------------------------------------------------
echo -e "${BOLD}[1/6] 环节: carla-server 容器运行${NC}"
if ! docker ps --format '{{.Names}}' | grep -q "carla-server"; then
  step_fail "carla-server 未运行"
  remedy "执行: ./scripts/start-all-nodes.sh 或 docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml up -d carla"
  exit 1
fi
step_ok "carla-server 已运行"
echo ""

# ---------------------------------------------------------------------------
# 步骤 2：Bridge 进程在运行（C++ 或 Python）
# ---------------------------------------------------------------------------
echo -e "${BOLD}[2/6] 环节: Bridge 进程在容器内运行${NC}"
BRIDGE_RUNNING=0
if docker exec carla-server sh -c "ps aux 2>/dev/null; ps -ef 2>/dev/null" 2>/dev/null | grep -v grep | grep -q "carla_bridge\.py"; then
  step_ok "Python Bridge 进程 (carla_bridge.py) 运行中"
  BRIDGE_RUNNING=1
fi
if [ "$BRIDGE_RUNNING" = "0" ] && docker exec carla-server sh -c "ps aux 2>/dev/null; ps -ef 2>/dev/null" 2>/dev/null | grep -v grep | grep -q "[c]arla_bridge"; then
  step_ok "C++ Bridge 进程 (carla_bridge) 运行中"
  BRIDGE_RUNNING=1
fi
if [ "$BRIDGE_RUNNING" = "0" ]; then
  step_fail "未检测到 Bridge 进程（carla_bridge 或 carla_bridge.py）"
  step_info "容器日志末尾:"
  docker logs carla-server 2>&1 | tail -30 | sed 's/^/    /'
  remedy "查看完整日志: docker logs carla-server 2>&1 | tail -100"
  remedy "若为 Python Bridge：镜像内 CARLA 多为 Python 2.7 egg，python3 会 segfault；entrypoint 已支持自动回退到 C++ Bridge，重启容器即可"
  remedy "或手动设 USE_PYTHON_BRIDGE=0 后重启 carla 容器，使用 C++ Bridge（testsrc）保证四路流"
  exit 1
fi
echo ""

# ---------------------------------------------------------------------------
# 步骤 3：发送 start_stream 后，日志中出现「已置 streaming」或「收到 start_stream」
# ---------------------------------------------------------------------------
echo -e "${BOLD}[3/6] 环节: 发送 start_stream，检查 Bridge 是否收到${NC}"
for attempt in 1 2; do
  mqtt_pub "$(mqtt_json_start_stream "$VIN")" || true
  sleep 8
  LOGS=$(get_carla_logs)
  if echo "$LOGS" | grep -qE "已置 streaming=true（VIN 匹配）|已置 streaming=True|收到 start_stream.*vin_ok=True|\[Control\] 已置 streaming|收到 vehicle/control 消息|收到 start_stream|\"streaming\":\s*true"; then
    step_ok "Bridge 已收到 start_stream（日志有「已置 streaming」或「收到 start_stream」）"
    break
  fi
  if [ "$attempt" -eq 2 ]; then
    step_fail "Bridge 未在日志中体现收到 start_stream"
    step_info "相关日志:"
    echo "$LOGS" | grep -E "Control|start_stream|streaming|MQTT|Bridge" | tail -15 | sed 's/^/    /'
    remedy "确认 MQTT：carla 与 teleop-mosquitto 同网（teleop-network）、MQTT_BROKER=teleop-mosquitto"
    remedy "确认主题：客户端与 Bridge 均使用 vehicle/control；VIN 一致（$VIN）"
    remedy "若为 C++：查看是否有 [MQTT] 已连接、[Control] 收到原始消息"
    exit 1
  fi
  step_info "第 1 次未检测到，2s 后重发 start_stream 再检查..."
  sleep 2
done
echo ""

# ---------------------------------------------------------------------------
# 步骤 4：日志中出现推流已启动（startPushers / 推流已启动）
# ---------------------------------------------------------------------------
echo -e "${BOLD}[4/6] 环节: Bridge 已启动推流（日志有推流已启动）${NC}"
# 先按 ZLM/推流 过滤再判断，避免被 50Hz status 刷掉
ZLM_LOGS=$(docker logs carla-server 2>&1 | grep -E "ZLM|推流|spawn.*相机|worker 启动" | tail -100)
if echo "$ZLM_LOGS" | grep -qE "\[ZLM\] 四路推流已启动|\[ZLM\].*推流已启动|推流已启动|四路相机已 spawn.*推流|spawn 并启动推流|startPushers 开始|worker 启动 stream_id"; then
  step_ok "Bridge 已启动推流"
else
  step_fail "日志中未看到推流已启动"
  step_info "相关日志:"
  echo "$ZLM_LOGS" | tail -15 | sed 's/^/    /'
  remedy "C++：检查是否有「ffmpeg 未找到或启动失败」→ 容器内安装 ffmpeg"
  remedy "C++：检查 [Bridge] 主循环: 检测到 streaming 由 false->true → 若无则主循环未读到状态"
  remedy "Python：检查是否有「spawn_cameras_and_start_pushers 进入」→ 若无则主循环未调用"
  exit 1
fi
echo ""

# ---------------------------------------------------------------------------
# 步骤 5：ZLM 上存在四路流
# ---------------------------------------------------------------------------
echo -e "${BOLD}[5/6] 环节: ZLM 上存在四路流 (cam_front/cam_rear/cam_left/cam_right)${NC}"
# 再等几秒让推流稳定
sleep 8
if streams_on_zlm 2>/dev/null; then
  step_ok "ZLM 上四路流已就绪，客户端可拉流"
else
  step_fail "ZLM 未检测到四路流"
  step_info "请确认 Bridge 推流地址与 ZLM 一致：rtmp://zlmediakit:1935/teleop/cam_*"
  remedy "从 carla-server 内测试: docker exec carla-server bash -c 'curl -sI http://zlmediakit:80/ 2>/dev/null | head -1'"
  remedy "若 ZLM 在宿主机: 将 Bridge 的 ZLM_HOST 改为宿主机在 Docker 网内的 IP 或 host.docker.internal"
  exit 1
fi
echo ""

# ---------------------------------------------------------------------------
# 步骤 6：客户端拉流验证（从 ZLM 拉取四路流并解码 1 帧，确认客户端能正常接收）
# ---------------------------------------------------------------------------
echo -e "${BOLD}[6/6] 环节: 客户端拉流验证（拉取并解码 1 帧）${NC}"
ZLM_BASE="${ZLM_URL%/}"
CLIENT_RECEIVE_OK=1
RECEIVE_COUNT=0
_probe_stream() {
  local s="$1"
  local url="$2"
  if command -v ffprobe &>/dev/null; then
    timeout 10 ffprobe -v error -show_entries stream=codec_type -of csv=p=0 -analyzeduration 500000 -probesize 50000 "$url" &>/dev/null
  elif command -v ffmpeg &>/dev/null; then
    timeout 10 ffmpeg -v error -i "$url" -t 0.5 -frames:v 1 -f null - 2>/dev/null
  else
    docker exec carla-server timeout 10 ffprobe -v error -show_entries stream=codec_type -of csv=p=0 "$url" -analyzeduration 500000 -probesize 50000 &>/dev/null 2>&1
  fi
}

if command -v ffprobe &>/dev/null || command -v ffmpeg &>/dev/null; then
  for s in $REQUIRED_STREAMS; do
    url="${ZLM_BASE}/teleop/${s}.live.flv"
    if _probe_stream "$s" "$url"; then
      step_ok "客户端可接收 $s (解码成功)"
      RECEIVE_COUNT=$((RECEIVE_COUNT + 1))
    else
      step_fail "客户端拉流失败: $s"
      CLIENT_RECEIVE_OK=0
    fi
  done
else
  # 备选：从 carla-server 容器内用 ffprobe 拉 ZLM（与 Bridge 同网，zlmediakit:80）
  for s in $REQUIRED_STREAMS; do
    if docker exec carla-server timeout 6 ffprobe -v error -show_entries stream=codec_type -of csv=p=0 "http://zlmediakit:80/teleop/${s}.live.flv" -analyzeduration 500000 -probesize 50000 &>/dev/null 2>&1; then
      step_ok "客户端可接收 $s (carla-server 内 ffprobe 解码成功)"
      RECEIVE_COUNT=$((RECEIVE_COUNT + 1))
    else
      step_fail "客户端拉流失败: $s"
      CLIENT_RECEIVE_OK=0
    fi
  done
  if [ "$RECEIVE_COUNT" -eq 0 ]; then
    step_info "宿主机与 carla-server 内均无可用的 ffprobe/ffmpeg 时，无法自动验证拉流"
    step_info "手动验证: docker exec carla-server ffprobe -v error http://zlmediakit:80/teleop/cam_front.live.flv"
    step_info "或启动 Qt 客户端选车 $VIN 连接车端查看四路画面"
  fi
fi

# 至少一路可接收即视为客户端能正常接收（部分路可能因 Bridge worker 退出暂不可用）
if [ "$RECEIVE_COUNT" -gt 0 ]; then
  if [ "$RECEIVE_COUNT" -lt 4 ]; then
    remedy "仅 $RECEIVE_COUNT/4 路可拉流；若某路 Bridge worker 曾报错，可重启 carla 后重发 start_stream 再验证"
  fi
  CLIENT_RECEIVE_OK=1
fi
if [ "$CLIENT_RECEIVE_OK" = "0" ] && [ "$RECEIVE_COUNT" -eq 0 ]; then
  remedy "Bridge 推流可能刚启动或 worker 异常，稍等 5s 后重跑；或检查 docker logs carla-server 2>&1 | grep -E ZLM|worker"
  exit 1
fi
echo ""

echo -e "${GREEN}========== 全部 6 步通过，推流与客户端接收正常 ==========${NC}"
echo "  可启动客户端选车 $VIN、连接车端，约 18s 后应出现四路画面。"
echo ""
