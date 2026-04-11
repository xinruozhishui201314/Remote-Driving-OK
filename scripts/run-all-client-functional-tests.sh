#!/usr/bin/env bash
# =============================================================================
# 客户端「当前可自动化」功能验证 — 单入口（一键尽可能多）
# =============================================================================
#
# 5 Whys 结论（为何仍不等于「所有模块 × 所有函数」）：
#   1) 为什么无法一条命令证完「所有功能」？→ 功能分布在 C++/QML/进程外服务/硬件/GPU。
#   2) 为什么单测不够？→ UI/WebRTC/解码/显示依赖图形栈、网络与计时，单测无法复现真实时空行为。
#   3) 为什么集成脚本也不够？→ CARLA/实车/证书/多机部署组合爆炸，需分级与可选门控。
#   4) 为什么还要跑静态检查？→ 契约与 QML 结构错误可在无显示环境下秒级检出（如控制 topic、布局关键字）。
#   5) 根因是什么？→ 「全覆盖」是分层 SLO：L1 静态 + L2 单测 + L3 API/登录栈 + L4 视频 + L5 整链/CARLA；
#      本脚本默认执行 L1+L2，可选 L3；L4/L5 见下方命令。
#
# 用法（修改客户端后推荐每次执行）：
#   ./scripts/run-all-client-functional-tests.sh
# 与 start-full-chain 同 compose 的完整一键（含栈、镜像内编译、CTest、连接、可选 CARLA 四路）：
#   ./scripts/run-client-oneclick-all-tests.sh
#   WITH_STACK_CHECKS=1 ./scripts/run-all-client-functional-tests.sh   # 含 Keycloak→JWT→/vins
#   SKIP_CLIENT_UNIT_TESTS=1 ./scripts/run-all-client-functional-tests.sh  # 仅静态（无 Docker 时临时；建议同时 SKIP_HEADLESS_LIFECYCLE=1，避免 L2b 找不到刚编的二进制）
#   SKIP_HEADLESS_LIFECYCLE=1 ./scripts/run-all-client-functional-tests.sh  # 跳过 L2b 无头进程退出链
#   IN_DOCKER=0 CLIENT_BUILD_DIR=./client/build ./scripts/run-all-client-functional-tests.sh
#
# 与 ./scripts/build-and-verify.sh 关系：后者含后端镜像/compose/增删车等全栈；本脚本聚焦客户端可自动化子集。
# 全仓库门禁仍建议：改代码后 ./scripts/build-and-verify.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

WITH_STACK_CHECKS="${WITH_STACK_CHECKS:-0}"
SKIP_CLIENT_UNIT_TESTS="${SKIP_CLIENT_UNIT_TESTS:-0}"
SKIP_HEADLESS_LIFECYCLE="${SKIP_HEADLESS_LIFECYCLE:-0}"
COMPOSE_FILES="${COMPOSE_FILES:--f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml}"
COMPOSE_CMD="docker compose ${COMPOSE_FILES}"
CLIENT_BUILD_DIR="${CLIENT_BUILD_DIR:-/tmp/client-build}"
HEADLESS_SMOKE_MS="${HEADLESS_SMOKE_MS:-8000}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

echo ""
echo -e "${BOLD}${CYAN}========== 客户端功能自动化验证（L1 静态 + L2 单测 + 可选 L3 栈）==========${NC}"
echo ""

# --- L1：契约 + 驾驶布局 + 视频管线结构 + 壳层/四路视频清点 ---
echo -e "${CYAN}[L1] UI/视频 L1（verify-client-ui-and-video-coverage.sh）${NC}"
bash "$SCRIPT_DIR/verify-client-ui-and-video-coverage.sh"
echo -e "  ${GREEN}[OK]${NC} L1 完成（矩阵见 docs/CLIENT_UI_FEATURE_COVERAGE_MATRIX.md）"
echo ""

# --- L2：C++ 全量 CTest ---
if [[ "$SKIP_CLIENT_UNIT_TESTS" == "1" ]]; then
  echo -e "${YELLOW}[L2] 跳过单元测试（SKIP_CLIENT_UNIT_TESTS=1）${NC}"
else
  echo -e "${CYAN}[L2] 客户端全量单元测试（CTest）${NC}"
  bash "$SCRIPT_DIR/run-all-client-unit-tests.sh"
  echo -e "  ${GREEN}[OK]${NC} L2 完成"
fi
echo ""

# --- L2b：无头「启动 → exec → 退出」（main 生命周期，非 CTest）---
if [[ "$SKIP_HEADLESS_LIFECYCLE" == "1" ]]; then
  echo -e "${YELLOW}[L2b] 跳过无头生命周期（SKIP_HEADLESS_LIFECYCLE=1）${NC}"
elif command -v docker >/dev/null 2>&1 && $COMPOSE_CMD ps client-dev 2>/dev/null | grep -qE 'Up|running'; then
  echo -e "${CYAN}[L2b] 无头进程生命周期（verify-client-headless-lifecycle.sh，client-dev 内 $CLIENT_BUILD_DIR/RemoteDrivingClient）${NC}"
  $COMPOSE_CMD exec -T client-dev \
    env CLIENT_BIN="$CLIENT_BUILD_DIR/RemoteDrivingClient" SMOKE_MS="$HEADLESS_SMOKE_MS" \
    bash /workspace/scripts/verify-client-headless-lifecycle.sh
  echo -e "  ${GREEN}[OK]${NC} L2b 完成"
else
  HOST_BIN="${PROJECT_ROOT}/client/build/RemoteDrivingClient"
  if [[ -x "$HOST_BIN" || -f "$HOST_BIN" ]]; then
    echo -e "${CYAN}[L2b] 无头进程生命周期（宿主机 $HOST_BIN）${NC}"
    CLIENT_BIN="$HOST_BIN" SMOKE_MS="$HEADLESS_SMOKE_MS" bash "$SCRIPT_DIR/verify-client-headless-lifecycle.sh"
    echo -e "  ${GREEN}[OK]${NC} L2b 完成"
  else
    echo -e "${YELLOW}[L2b] 跳过：无运行中的 client-dev，且未找到 ${HOST_BIN}（可设 SKIP_HEADLESS_LIFECYCLE=1 消除本提示）${NC}"
  fi
fi
echo ""

# --- L3：登录/API（需 compose 栈）---
if [[ "$WITH_STACK_CHECKS" == "1" ]]; then
  echo -e "${CYAN}[L3] 登录链路（verify-client-login）${NC}"
  bash "$SCRIPT_DIR/verify-client-login.sh"
  echo -e "  ${GREEN}[OK]${NC} L3 完成"
else
  echo -e "${YELLOW}[L3] 已跳过（WITH_STACK_CHECKS=1 启用 Keycloak/Backend 登录链验证）${NC}"
fi
echo ""

echo -e "${BOLD}${GREEN}========== 本脚本范围内：客户端可自动化验证已全部通过 ==========${NC}"
echo ""
echo -e "${CYAN}未默认包含（需环境与更长耗时，按需单独执行）：${NC}"
echo "  ./scripts/build-and-verify.sh                    # 全栈 + 后端 E2E"
echo "  ./scripts/verify-client-video-pipeline.sh        # 视频管线"
echo "  ./scripts/verify-all-client-to-carla.sh          # 客户端↔CARLA 整链"
echo "  ./scripts/verify-login-ui-features.sh            # 需显示/UI"
echo "  ./scripts/verify-client-four-view-video.sh       # 四路视频：ZLM 推流 + 日志 [VideoPresent][1Hz]"
echo "  ./scripts/verify-client-headless-lifecycle.sh    # 无头：启动→quit→aboutToQuit（需 CLIENT_BIN）"
echo ""
