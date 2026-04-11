#!/usr/bin/env bash
# 客户端「模块化」自动化验证入口：串联仓库内已有脚本与（可选）CTest。
#
# 覆盖层次（由浅入深）：
#   A. 静态/契约：QML 控制路径、驾驶布局 QML 关键字（不启动 GUI）
#   B. 单元测试：CMake CTest（需 Qt6::Test + 已在 client-dev 内编译过客户端）
#   C. 登录链路：Keycloak → JWT → GET /api/v1/vins（需 compose 栈，与真实客户端登录一致）
#   D. 整链/CARLA：见 verify-all-client-to-carla.sh（本脚本不默认执行）
#
# 用法：
#   ./scripts/verify-all-client-modules.sh
#   WITH_DOCKER_CTEST=0 ./scripts/verify-all-client-modules.sh    # 仅静态，跳过 CTest
#   WITH_STACK_CHECKS=1 ./scripts/verify-all-client-modules.sh   # 含 verify-client-login
#   WITH_FOUR_VIEW_VERIFY=1 ./scripts/verify-all-client-modules.sh  # 含四路视频日志断言（需 ZLM 推流）
# 更完整一键（L1+L2+可选 L3）：./scripts/run-all-client-functional-tests.sh
#   ./scripts/verify-all-client-modules.sh --help
#
# 说明：
#   - 「所有模块功能正常」在工程上需分层：纯逻辑可单测；QML 需语法/静态/启动日志；
#     视频/WebRTC/真 UI 需栈或录屏工具（Squish/Qt Test GUI 等），本脚本覆盖 A+B+C 与文档指引 D。
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE_FILES="${COMPOSE_FILES:--f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml}"
COMPOSE_CMD="docker compose ${COMPOSE_FILES}"

# 默认尝试跑 CTest（与「一键尽可能覆盖」一致）；无 Docker/client-dev 时跳过或失败见下方逻辑
WITH_DOCKER_CTEST="${WITH_DOCKER_CTEST:-1}"
WITH_STACK_CHECKS="${WITH_STACK_CHECKS:-0}"
WITH_FOUR_VIEW_VERIFY="${WITH_FOUR_VIEW_VERIFY:-0}"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

PASSED=0
FAILED=0
SKIPPED=0

pass() { echo -e "  ${GREEN}[OK]${NC} $*"; PASSED=$((PASSED + 1)); }
fail() { echo -e "  ${RED}[FAIL]${NC} $*"; FAILED=$((FAILED + 1)); }
skip() { echo -e "  ${YELLOW}[SKIP]${NC} $*"; SKIPPED=$((SKIPPED + 1)); }

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  sed -n '1,35p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
fi

echo ""
echo -e "${BOLD}${CYAN}========== 客户端模块化验证（verify-all-client-modules）==========${NC}"
echo ""

# --- A: 静态 / 契约 / UI+视频 L1（串联，见 docs/CLIENT_UI_FEATURE_COVERAGE_MATRIX.md）---
echo -e "${CYAN}[A] 静态与 QML 契约 + UI/视频覆盖（无 Docker 亦可）${NC}"
if bash "$SCRIPT_DIR/verify-client-ui-and-video-coverage.sh"; then
  pass "verify-client-ui-and-video-coverage.sh（契约+布局+视频管线+壳层/四路清点）"
else
  fail "verify-client-ui-and-video-coverage.sh"
fi

# --- B: CTest（client-dev 容器内）---
if [[ "$WITH_DOCKER_CTEST" == "1" ]]; then
  echo ""
  echo -e "${CYAN}[B] 客户端 C++ 单元测试（CTest，需 client-dev + /tmp/client-build）${NC}"
  if ! command -v docker >/dev/null 2>&1; then
    skip "未安装 docker，跳过 CTest"
  elif ! $COMPOSE_CMD ps client-dev 2>/dev/null | grep -qE 'Up|running'; then
    skip "client-dev 未运行；请先 compose up client-dev 或设 WITH_DOCKER_CTEST=0"
  else
    if $COMPOSE_CMD exec -T client-dev bash -c 'test -f /tmp/client-build/CTestTestfile.cmake'; then
      if $COMPOSE_CMD exec -T client-dev bash -c 'cd /tmp/client-build && ctest --output-on-failure'; then
        pass "ctest --output-on-failure（/tmp/client-build）"
      else
        fail "ctest（见上方用例输出；若未编译 Test 目标请先容器内 cmake+make）"
      fi
    else
      skip "无 /tmp/client-build/CTestTestfile.cmake；请在 client-dev 内编译客户端（含 Qt6 Test）"
    fi
  fi
else
  echo ""
  echo -e "${YELLOW}[B] CTest 已跳过（无 Docker 或 client-dev 未运行；设 WITH_DOCKER_CTEST=0 可抑制本警告）${NC}"
fi

# --- C: 登录链路（与客户端拉车辆列表一致）---
if [[ "$WITH_STACK_CHECKS" == "1" ]]; then
  echo ""
  echo -e "${CYAN}[C] 登录与 Backend API（verify-client-login）${NC}"
  if bash "$SCRIPT_DIR/verify-client-login.sh"; then
    pass "verify-client-login.sh"
  else
    fail "verify-client-login.sh（需 Keycloak/Backend 栈就绪）"
  fi
else
  echo ""
  echo -e "${YELLOW}[C] 登录链路已跳过（设 WITH_STACK_CHECKS=1 启用；或单独跑 ./scripts/build-and-verify.sh）${NC}"
fi

# --- D: 四路视频（ZLM 四路在册 + client-dev + DISPLAY；见 docs/CLIENT_UI_FEATURE_COVERAGE_MATRIX.md §7）---
if [[ "$WITH_FOUR_VIEW_VERIFY" == "1" ]]; then
  echo ""
  echo -e "${CYAN}[D] 四路视频呈现验证（verify-client-four-view-video.sh）${NC}"
  if bash "$SCRIPT_DIR/verify-client-four-view-video.sh"; then
    pass "verify-client-four-view-video.sh"
  else
    fail "verify-client-four-view-video.sh（需推流、VIN 与 CLIENT_AUTO_CONNECT_TEST_VIN 一致）"
  fi
fi

echo ""
echo -e "${BOLD}========== 汇总 ==========${NC}"
echo -e "  ${GREEN}OK: $PASSED${NC}  ${RED}FAIL: $FAILED${NC}  ${YELLOW}SKIP: $SKIPPED${NC}"
echo ""
echo "进阶（视频/整链/CARLA）："
echo "  docs/CLIENT_UI_FEATURE_COVERAGE_MATRIX.md"
echo "  ./scripts/build-and-verify.sh"
echo "  ./scripts/verify-all-client-to-carla.sh"
echo "  ./scripts/verify-client-video-pipeline.sh [--build --run]"
echo "  WITH_DOCKER_LOGIN_SMOKE=1 ./scripts/verify-client-ui-and-video-coverage.sh"
echo "  ./scripts/verify-client-four-view-video.sh"
echo "  WITH_FOUR_VIEW_VERIFY=1 ./scripts/verify-all-client-modules.sh"
echo "  ./scripts/verify-login-ui-features.sh   # 需 GUI/容器显示"
echo ""

if [[ "$FAILED" -gt 0 ]]; then
  exit 1
fi
exit 0
