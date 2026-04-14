#!/usr/bin/env bash
# 远驾客户端 → CARLA 仿真 全面功能验证
# 按顺序执行多类验证并汇总结果，覆盖：镜像能力、基础设施、MQTT、Backend、CARLA Bridge、整链逐项。
#
# 用法：
#   ./scripts/verify-all-client-to-carla.sh           # 遇失败继续执行后续项，最后汇总
#   CONTINUE_ON_FAIL=0 ./scripts/verify-all-client-to-carla.sh  # 遇失败即退出（默认 1=继续）
#
# 前提：已构建 CARLA 镜像且已启动全部节点时，可全部通过；仅镜像未启动时部分项会失败。

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
# shellcheck source=lib/mqtt_control_json.sh
source "$SCRIPT_DIR/lib/mqtt_control_json.sh"

CONTINUE_ON_FAIL="${CONTINUE_ON_FAIL:-1}"
PASSED=0
FAILED=0
SKIPPED=0
STEP_FAILED=0

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

run_step() {
  local name="$1"
  local cmd="$2"
  STEP_FAILED=0
  echo ""
  echo -e "${BOLD}---------- $name ----------${NC}"
  if eval "$cmd" 2>&1; then
    echo -e "${GREEN}[PASS] $name${NC}"
    PASSED=$((PASSED+1))
    return 0
  else
    echo -e "${RED}[FAIL] $name${NC}"
    FAILED=$((FAILED+1))
    STEP_FAILED=1
    [ "$CONTINUE_ON_FAIL" = "1" ] && return 1 || exit 1
  fi
}

run_step_optional() {
  local name="$1"
  local cmd="$2"
  echo ""
  echo -e "${BOLD}---------- $name (可选) ----------${NC}"
  if eval "$cmd" 2>&1; then
    echo -e "${GREEN}[PASS] $name${NC}"
    PASSED=$((PASSED+1))
    return 0
  else
    echo -e "${YELLOW}[SKIP] $name（未满足前提或可选失败）${NC}"
    SKIPPED=$((SKIPPED+1))
    return 0
  fi
}

echo ""
echo -e "${BOLD}============================================${NC}"
echo -e "${BOLD}  远驾客户端 → CARLA 仿真 全面功能验证${NC}"
echo -e "${BOLD}============================================${NC}"
echo "  CONTINUE_ON_FAIL=$CONTINUE_ON_FAIL（1=遇失败继续，0=遇失败即停）"
echo ""

# ── 1. 镜像能力（静态，--entrypoint 已由 verify-carla-image-capabilities 覆盖）──
run_step "1/10 CARLA 镜像能力（Paho/ffmpeg/entrypoint/cmake/CARLA）" \
  "bash $SCRIPT_DIR/verify-carla-image-capabilities.sh" || true
[ "$STEP_FAILED" = "1" ] && [ "$CONTINUE_ON_FAIL" = "0" ] && exit 1

# ── 2. 基础设施：各服务运行状态 ──
run_step "2/10 基础设施（Postgres/Keycloak/Backend/ZLM/Mosquitto/CARLA 容器）" \
  "bash -c '
    COMPOSE=\"docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml\"
    for svc in postgres keycloak backend zlmediakit mosquitto carla; do
      \$COMPOSE ps \$svc 2>/dev/null | grep -q Up || { echo \"  \$svc 未运行\"; exit 1; }
    done
    echo \"  所有服务运行中\"
  '" || true
[ "$STEP_FAILED" = "1" ] && [ "$CONTINUE_ON_FAIL" = "0" ] && exit 1

# ── 3. Backend 健康 ──
run_step "3/10 Backend 健康检查（/health）" \
  "curl -sf http://127.0.0.1:8081/health && echo '' && echo '  Backend 健康'" || true
[ "$STEP_FAILED" = "1" ] && [ "$CONTINUE_ON_FAIL" = "0" ] && exit 1

# ── 4. ZLM 可访问 ──
run_step "4/10 ZLM 可访问（HTTP 80）" \
  "curl -sfI -m 3 http://127.0.0.1:80/ 2>/dev/null | head -1 && echo '  ZLM 可访问'" || true
[ "$STEP_FAILED" = "1" ] && [ "$CONTINUE_ON_FAIL" = "0" ] && exit 1

# ── 5. MQTT 发布（vehicle/control）──
run_step "5/10 MQTT 发布（vehicle/control 可发）" \
  'p=$(mqtt_json_start_stream carla-sim-001) && if command -v mosquitto_pub &>/dev/null; then mosquitto_pub -h 127.0.0.1 -p 1883 -t vehicle/control -m "$p" -u client_user -P client_password_change_in_prod 2>/dev/null || mosquitto_pub -h 127.0.0.1 -p 1883 -t vehicle/control -m "$p" 2>/dev/null; else docker exec teleop-mosquitto mosquitto_pub -h localhost -p 1883 -t vehicle/control -m "$p" 2>/dev/null; fi && echo "  MQTT 发布成功"' || true
[ "$STEP_FAILED" = "1" ] && [ "$CONTINUE_ON_FAIL" = "0" ] && exit 1

# ── 6. CARLA Bridge 功能（start_stream / 四路流 / stop_stream / control）──
run_step "6/10 CARLA Bridge 功能（四路流/stop/control）" \
  "bash $SCRIPT_DIR/verify-carla-bridge-cpp-features.sh" || true
[ "$STEP_FAILED" = "1" ] && [ "$CONTINUE_ON_FAIL" = "0" ] && exit 1

# ── 6a. 视频源是否为 CARLA（Python Bridge / CARLA 相机，非 testsrc）──
run_step "7/10 CARLA 视频源（Python Bridge / CARLA 相机）" \
  "bash $SCRIPT_DIR/verify-carla-video-source.sh" || true
[ "$STEP_FAILED" = "1" ] && [ "$CONTINUE_ON_FAIL" = "0" ] && exit 1

# ── 6b. 控制 CARLA 仿真车（remote_control + drive，依据 carla-server 日志判断）──
run_step "8/10 控制 CARLA 仿真车（remote_control/drive，依据日志）" \
  "bash $SCRIPT_DIR/verify-client-control-carla.sh" || true
[ "$STEP_FAILED" = "1" ] && [ "$CONTINUE_ON_FAIL" = "0" ] && exit 1

# ── 7. 远驾整链逐项（会话/start_stream/四路流/drive/stop_stream）──
run_step "9/10 远驾整链逐项（鉴权/会话/MQTT/桥梁/ZLM/控制/stop）" \
  "bash $SCRIPT_DIR/verify-client-to-carla-step-by-step.sh" || true
[ "$STEP_FAILED" = "1" ] && [ "$CONTINUE_ON_FAIL" = "0" ] && exit 1

# ── 8. 远驾整链一次性（与 7 重叠，可选）──
run_step_optional "10/10 远驾整链一次性（verify-full-chain-client-to-carla）" \
  "bash $SCRIPT_DIR/verify-full-chain-client-to-carla.sh"

# ── 汇总 ──
echo ""
echo -e "${BOLD}============================================${NC}"
echo -e "${BOLD}  全面功能验证汇总${NC}"
echo -e "${BOLD}============================================${NC}"
echo -e "  ${GREEN}通过: $PASSED${NC}  ${RED}失败: $FAILED${NC}  ${YELLOW}跳过: $SKIPPED${NC}"
echo ""

if [ $FAILED -eq 0 ]; then
  echo -e "${GREEN}全部必检项通过。远驾客户端 → CARLA 仿真链路功能验证完成。${NC}"
  echo "  可选：启动客户端选车 carla-sim-001 进行人工驾驶验证。"
  echo ""
  exit 0
fi

echo -e "${RED}存在失败项，请根据上述 [FAIL] 逐项排查。${NC}"
echo "  常见原因：未执行 ./scripts/start-all-nodes.sh、CARLA 未就绪、网络/端口不可达。"
echo ""
exit 1
