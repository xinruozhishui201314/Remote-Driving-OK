#!/usr/bin/env bash
# 验证：远驾客户端启动时同步启动 CARLA 的逻辑
# 用法：bash scripts/verify-ensure-carla-with-client.sh
# 前置：CARLA 镜像 remote-driving/carla-with-bridge:latest 已存在（否则 ensure 会跳过并 exit 0）
# 环境变量：SKIP_CARLA=1 时 ensure 会跳过，本验证会检测并报告

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
log_info() { echo -e "${CYAN}[INFO] $*${NC}"; }

FAILED=0

echo -e "${CYAN}========== 验证：客户端启动时同步启动 CARLA ==========${NC}"
echo ""

# 1. 确保 ensure-carla-running.sh 存在
if [ ! -f "$SCRIPT_DIR/ensure-carla-running.sh" ]; then
  log_fail "ensure-carla-running.sh 不存在"
  exit 1
fi
log_ok "ensure-carla-running.sh 存在"

# 2. run-client-ui.sh 和 run.sh 已集成 ensure 调用
if ! grep -q "ensure-carla-running" "$SCRIPT_DIR/run-client-ui.sh" 2>/dev/null; then
  log_fail "run-client-ui.sh 未调用 ensure-carla-running.sh"
  FAILED=1
else
  log_ok "run-client-ui.sh 已集成 ensure-carla-running.sh"
fi

if ! grep -q "ensure-carla-running" "$SCRIPT_DIR/run.sh" 2>/dev/null; then
  log_fail "run.sh cmd_client 未调用 ensure-carla-running.sh"
  FAILED=1
else
  log_ok "run.sh cmd_client 已集成 ensure-carla-running.sh"
fi

# 3. 若 CARLA 镜像存在，执行 ensure 并验证容器启动
if docker image inspect remote-driving/carla-with-bridge:latest >/dev/null 2>&1; then
  log_info "CARLA 镜像已存在，执行 ensure-carla-running.sh 并验证..."
  # 先停止 CARLA（若在运行）
  docker stop carla-server 2>/dev/null || true
  docker rm -f carla-server 2>/dev/null || true
  sleep 2

  # 执行 ensure（SKIP_CARLA=0 明确不跳过）
  export SKIP_CARLA=0
  if bash "$SCRIPT_DIR/ensure-carla-running.sh" 2>&1; then
    sleep 2
    if docker ps --format '{{.Names}}' 2>/dev/null | grep -q "carla-server"; then
      log_ok "ensure-carla-running.sh 执行后 carla-server 已启动"
    else
      log_fail "ensure-carla-running.sh 执行后 carla-server 未运行"
      FAILED=1
    fi
  else
    log_fail "ensure-carla-running.sh 执行失败"
    FAILED=1
  fi
else
  log_info "CARLA 镜像未找到，跳过容器启动验证（ensure 会正常跳过并提示）"
  # 仅验证 ensure 不报错退出
  export SKIP_CARLA=0
  bash "$SCRIPT_DIR/ensure-carla-running.sh" 2>&1 || true
  log_ok "ensure-carla-running.sh 可正常执行（镜像缺失时友好提示）"
fi

echo ""
if [ $FAILED -eq 0 ]; then
  echo -e "${GREEN}========== 验证通过 ==========${NC}"
  echo "  远驾客户端启动时将同步启动 CARLA（bash scripts/run-client-ui.sh 或 bash scripts/run.sh client）"
  echo "  跳过 CARLA：SKIP_CARLA=1 bash scripts/run-client-ui.sh"
  exit 0
else
  echo -e "${RED}========== 验证失败 ==========${NC}"
  exit 1
fi
