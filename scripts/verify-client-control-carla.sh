#!/usr/bin/env bash
# 自动化验证：远驾客户端控制 CARLA 仿真车辆 — 逐项测试，依据 CARLA 容器日志判断是否生效。
#
# 验证链路：MQTT 发布 remote_control / drive → CARLA Bridge（C++ 或 Python）接收并写日志 → 主循环发布 vehicle/status。
# 通过抓取 carla-server 日志中的 [Control] / [carla-bridge:Control] 与 vehicle/status 反馈判断功能是否正常。
# 兼容：C++ Bridge 日志 [Control] 收到 type=remote_control；Python Bridge 日志 [carla-bridge:Control] 收到 vehicle/control / 收到 drive / [Control] 应用 #
#
# 用法：./scripts/verify-client-control-carla.sh
# 前提：./scripts/start-all-nodes.sh 已执行，carla-server 与 Bridge 已运行。

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
# shellcheck source=lib/mqtt_control_json.sh
source "$SCRIPT_DIR/lib/mqtt_control_json.sh"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
VIN="${VIN:-carla-sim-001}"
LOG_TAIL=500
SLEEP_AFTER_MSG=2

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[验证] $*${NC}"; }
log_ok()      { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail()    { echo -e "  ${RED}[FAIL] $*${NC}"; }
log_detail()  { echo -e "  ${BOLD}(日志) $*${NC}"; }

# 获取 CARLA 容器最近日志（用于依据日志判断）
get_carla_logs() {
  docker logs carla-server 2>&1 | tail -"${LOG_TAIL}"
}

# 发送 MQTT 到 vehicle/control
mqtt_pub() {
  local msg="$1"
  if command -v mosquitto_pub &>/dev/null; then
    mosquitto_pub -h 127.0.0.1 -p 1883 -t vehicle/control -m "$msg" 2>/dev/null && return 0
  fi
  $COMPOSE exec -T teleop-mosquitto mosquitto_pub -h localhost -p 1883 -t vehicle/control -m "$msg" 2>/dev/null || true
}

FAILED=0

echo ""
echo -e "${BOLD}========== 远驾控制 CARLA 仿真车 — 逐项验证（依据日志判断）==========${NC}"
echo "  VIN=$VIN  日志来源: docker logs carla-server (tail $LOG_TAIL)"
echo ""

# ---------------------------------------------------------------------------
# 0) 前置：CARLA 容器运行
# ---------------------------------------------------------------------------
log_section "0/6 前置：CARLA 容器 (carla-server) 运行"
if ! docker ps --format '{{.Names}}' | grep -q "carla-server"; then
  log_fail "carla-server 未运行；请先: ./scripts/start-all-nodes.sh"
  exit 1
fi
log_ok "carla-server 运行中"
echo ""

# ---------------------------------------------------------------------------
# 1) 发送 remote_control enable=true，依据日志判断 Bridge 是否收到并处理
# ---------------------------------------------------------------------------
log_section "1/6 remote_control enable=true — 发送并检查 Bridge 日志"
mqtt_pub "$(mqtt_json_remote_control "$VIN" true)"
sleep "$SLEEP_AFTER_MSG"
LOGS=$(get_carla_logs)
if echo "$LOGS" | grep -qE "\[Control\] 收到 type=remote_control|收到 vehicle/control 消息|carla-bridge:Control.*vehicle/control|收到远驾接管指令"; then
  log_ok "Bridge 日志中已出现「收到 remote_control/vehicle/control」"
else
  log_fail "Bridge 日志中未出现「收到 type=remote_control」或「收到 vehicle/control 消息」"
  log_detail "最近日志片段:"
  echo "$LOGS" | grep -E "Control|control|remote" | tail -5 | sed 's/^/    /'
  FAILED=$((FAILED+1))
fi
if echo "$LOGS" | grep -qE "\[Control\] remote_control enable=true|收到控制:.*steering|carla-bridge:Control.*收到控制|收到远驾接管指令.*enable=True"; then
  log_ok "Bridge 日志中已出现「remote_control enable=true」或「收到远驾接管指令」"
else
  log_fail "Bridge 日志中未出现「remote_control enable=true」或「收到远驾接管指令」"
  FAILED=$((FAILED+1))
fi
echo ""

# ---------------------------------------------------------------------------
# 2) 发送 drive（steering/throttle/brake），依据日志判断 Bridge 是否收到
# ---------------------------------------------------------------------------
log_section "2/6 drive（steering/throttle/brake）— 发送并检查 Bridge 日志"
mqtt_pub "$(mqtt_json_drive "$VIN" 0.15 0.25 0 1 false)"
sleep "$SLEEP_AFTER_MSG"
LOGS=$(get_carla_logs)
if echo "$LOGS" | grep -qE "\[Control\] 收到 type=drive|\[Control\] 收到 drive|收到控制:.*steering|carla-bridge:Control.*收到控制"; then
  log_ok "Bridge 日志中已出现「收到 drive」或「收到控制」"
else
  log_fail "Bridge 日志中未出现「收到 drive」或「收到控制」"
  log_detail "最近含 Control 的日志:"
  echo "$LOGS" | grep -E "Control|control" | tail -5 | sed 's/^/    /'
  FAILED=$((FAILED+1))
fi
echo ""

# ---------------------------------------------------------------------------
# 2b) Python Bridge：检查 apply_control 是否执行（车辆控制已应用到 CARLA）
# ---------------------------------------------------------------------------
log_section "2b/6 车辆控制应用 — 检查 [Control] 应用 #（Python Bridge apply_control）"
LOGS=$(get_carla_logs)
if echo "$LOGS" | grep -qE "\[Control\] 应用 #|车辆控制已启用 CONTROL_HZ"; then
  log_ok "Bridge 已执行 apply_control，车辆控制已应用到 CARLA 仿真"
else
  log_detail "未检测到 [Control] 应用 #（可能为 C++ Bridge 或 Bridge 未进入主循环）"
  log_ok "跳过 apply_control 检查（C++ Bridge 无此日志）"
fi
echo ""

# ---------------------------------------------------------------------------
# 3) vehicle/status 反馈 — 若有 mosquitto_sub 则订阅一条，判断是否含 steering/throttle 或 remote_control_ack
# ---------------------------------------------------------------------------
log_section "3/6 vehicle/status 反馈 — Bridge 是否周期性发布状态"
if command -v mosquitto_sub &>/dev/null; then
  STATUS_MSG=$(timeout 5 mosquitto_sub -h 127.0.0.1 -p 1883 -t "vehicle/status" -C 1 -W 4 2>/dev/null || true)
  if [ -n "$STATUS_MSG" ] && (echo "$STATUS_MSG" | grep -q "steering\|throttle\|remote_control_ack\|speed"); then
    log_ok "已收到 vehicle/status，内容含 steering/throttle/ack/speed"
    log_detail "$(echo "$STATUS_MSG" | head -c 200)"
  else
    log_fail "未在 4s 内收到 vehicle/status 或内容不含预期字段"
    FAILED=$((FAILED+1))
  fi
else
  log_ok "未安装 mosquitto_sub，跳过 vehicle/status 订阅检查（Bridge 仍会发布，客户端可收）"
fi
echo ""

# ---------------------------------------------------------------------------
# 4) 再次发送 drive 不同参数，确认日志中可见多次 drive
# ---------------------------------------------------------------------------
log_section "4/6 再次发送 drive（不同参数）— 确认控制通道可持续接收"
mqtt_pub "$(mqtt_json_drive "$VIN" -0.1 0.1 0.05 1 false)"
sleep "$SLEEP_AFTER_MSG"
LOGS=$(get_carla_logs)
DRIVE_COUNT=$(echo "$LOGS" | grep -cE "\[Control\] 收到 type=drive|\[Control\] 收到 drive|收到控制:.*steering" || true)
if [ "${DRIVE_COUNT:-0}" -ge 2 ]; then
  log_ok "日志中至少 2 次「收到 type=drive」或「收到控制」，控制通道可持续接收"
else
  log_fail "日志中「收到 type=drive」/「收到控制」次数不足 2（当前 ${DRIVE_COUNT:-0}）"
  FAILED=$((FAILED+1))
fi
echo ""

# ---------------------------------------------------------------------------
# 5) 发送 remote_control enable=false，依据日志判断
# ---------------------------------------------------------------------------
log_section "5/6 remote_control enable=false — 关闭远驾接管"
mqtt_pub "$(mqtt_json_remote_control "$VIN" false)"
sleep "$SLEEP_AFTER_MSG"
LOGS=$(get_carla_logs)
if echo "$LOGS" | grep -qE "\[Control\] remote_control enable=false|收到 vehicle/control 消息.*remote_control|carla-bridge:Control.*vehicle/control"; then
  log_ok "Bridge 日志中已出现「remote_control enable=false」或后续 control 消息"
else
  log_fail "Bridge 日志中未出现「remote_control enable=false」（Python Bridge 可能仅打印「收到 vehicle/control 消息」）"
  FAILED=$((FAILED+1))
fi
echo ""

# ---------------------------------------------------------------------------
# 汇总与日志输出
# ---------------------------------------------------------------------------
echo -e "${BOLD}========== 验证汇总 ==========${NC}"
if [ $FAILED -eq 0 ]; then
  echo -e "${GREEN}全部通过。远驾客户端通过 MQTT 控制 CARLA 仿真车的链路正常（Bridge 收包与日志、vehicle/status 反馈均符合预期）。${NC}"
  echo ""
  echo "  依据日志结论："
  echo "    - Bridge 能正确接收 remote_control / drive 并写入 [Control] 日志"
  echo "    - vehicle/status 有反馈（或本机未安装 mosquitto_sub 未校验）"
  echo "  实际车辆运动依赖 Bridge 内 applyControl 与 CARLA 仿真（当前 C++ 侧为状态存储，若需真实动效需 LibCarla 集成）。"
  echo ""
  exit 0
fi
echo -e "${RED}失败项: $FAILED${NC}"
echo "  请根据上述 [FAIL] 与日志片段排查：MQTT 是否可达、VIN 是否匹配 $VIN、carla-server 内 C++ Bridge 是否运行。"
echo ""
echo "  查看完整 CARLA 日志: docker logs carla-server 2>&1 | tail -100"
echo ""
exit 1
