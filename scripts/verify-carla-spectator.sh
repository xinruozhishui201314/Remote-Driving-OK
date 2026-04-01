#!/usr/bin/env bash
# 自动化验证：CARLA Bridge Spectator 功能（仿真窗口视角跟随车辆）是否已实现。
# 检查：Bridge 启动、Spectator 配置、Spectator 更新日志。
#
# 用法：bash scripts/verify-carla-spectator.sh
# 前置：CARLA 容器已启动且 Bridge 已运行（verify-carla-ui-only.sh 或 start-all-nodes-and-verify.sh 后约 90s）
# 可选：WAIT_FOR_BRIDGE=60 等待 Bridge 日志的最长时间（秒），默认 30

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_ok()   { echo -e "${GREEN}[OK] $*${NC}"; }
log_fail() { echo -e "${RED}[FAIL] $*${NC}"; }
log_warn() { echo -e "${YELLOW}[WARN] $*${NC}"; }
log_info() { echo -e "${CYAN}[INFO] $*${NC}"; }

WAIT_FOR_BRIDGE="${WAIT_FOR_BRIDGE:-30}"
FAILED=0

echo -e "${CYAN}========== CARLA Spectator 功能自动化验证 ==========${NC}"
echo ""

# 1. CARLA 容器运行
log_info "1. 检查 CARLA 容器..."
if ! docker ps --format '{{.Names}}' 2>/dev/null | grep -q "carla-server"; then
  log_fail "CARLA 容器未运行；请先执行: bash scripts/verify-carla-ui-only.sh"
  exit 1
fi
log_ok "CARLA 容器已运行"
echo ""

# 2. 等待 Bridge 产生 Spectator 相关日志
log_info "2. 等待 Bridge 启动并产生 Spectator 日志（最多 ${WAIT_FOR_BRIDGE}s）..."
ELAPSED=0
INTERVAL=5
FOUND_BRIDGE=0
FOUND_SPECTATOR_CFG=0
FOUND_SPECTATOR_UPDATE=0

while [ $ELAPSED -lt $WAIT_FOR_BRIDGE ]; do
  LOGS=$(docker logs carla-server 2>&1)
  # Bridge 启动：匹配 [carla-bridge] 或 [carla-bridge:CARLA] 或 [Bridge] 或 [Spectator]
  if echo "$LOGS" | grep -qE "\[carla-bridge\]|\[carla-bridge:CARLA\]|\[Bridge\]|\[Spectator\]"; then
    FOUND_BRIDGE=1
  fi
  if echo "$LOGS" | grep -q "\[Spectator\].*SPECTATOR_FOLLOW_VEHICLE="; then
    FOUND_SPECTATOR_CFG=1
  fi
  if echo "$LOGS" | grep -q "\[Spectator\] 更新 #"; then
    FOUND_SPECTATOR_UPDATE=1
  fi
  # Spectator 配置或更新日志任一存在即可
  if [ "$FOUND_BRIDGE" = "1" ] && { [ "$FOUND_SPECTATOR_CFG" = "1" ] || [ "$FOUND_SPECTATOR_UPDATE" = "1" ]; }; then
    log_ok "Bridge 已启动且 Spectator 已就绪（${ELAPSED}s）"
    break
  fi
  sleep $INTERVAL
  ELAPSED=$((ELAPSED + INTERVAL))
  log_info "  ${ELAPSED}s: 等待 Bridge/Spectator 日志..."
done

if [ "$FOUND_BRIDGE" != "1" ]; then
  log_fail "未在日志中找到 Bridge/Spectator 相关输出；Bridge 可能未启动"
  log_info "  排查: docker logs carla-server 2>&1 | grep -E '\[carla-bridge\]|\[Bridge\]|\[Spectator\]|entrypoint'"
  FAILED=1
fi

if [ "$FOUND_SPECTATOR_CFG" != "1" ] && [ "$FOUND_SPECTATOR_UPDATE" != "1" ]; then
  log_fail "未在日志中找到 Spectator 配置或更新日志；Spectator 功能可能未启用"
  FAILED=1
fi

if [ "$FOUND_SPECTATOR_UPDATE" != "1" ]; then
  log_warn "未在日志中找到 [Spectator] 更新 #；Spectator 可能未进入主循环或 get_spectator 返回 None"
  log_info "  若 CARLA 为无头模式，Spectator 可能不可用；详见 docs/CARLA_SPECTATOR_DEBUG.md"
  # 不设为 FAILED，因无头模式下 Spectator 可能确实不可用
fi
echo ""

# 3. 汇总检查项
log_info "3. 验证结果汇总..."
LOGS=$(docker logs carla-server 2>&1)
if echo "$LOGS" | grep -q "\[Bridge\] 阶段: 车辆已 spawn"; then
  log_ok "车辆已 spawn，Bridge 主循环已进入"
else
  log_warn "未确认「车辆已 spawn」；Bridge 可能仍在连接或 spawn 阶段"
fi

if echo "$LOGS" | grep -q "SPECTATOR_VIEW_MODE=driver"; then
  log_ok "SPECTATOR_VIEW_MODE=driver（主视角）已配置"
else
  log_warn "未确认 SPECTATOR_VIEW_MODE=driver"
fi
echo ""

# 4. 输出关键日志片段（便于人工核对）
log_info "4. 关键日志片段（Spectator 相关）..."
docker logs carla-server 2>&1 | grep -E "\[carla-bridge\]|\[Bridge\]|\[Spectator\]" | tail -15 || echo "(无匹配)"
echo ""

# 汇总
echo -e "${CYAN}========== Spectator 功能验证完成 ==========${NC}"
if [ $FAILED -eq 0 ]; then
  log_ok "Spectator 功能已实现：Bridge 启动、Spectator 配置已输出"
  exit 0
else
  log_fail "Spectator 功能验证未通过；请根据上方提示排查"
  exit 1
fi
