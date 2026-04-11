#!/usr/bin/env bash
# 一键串联：L2 服务层 CTest 目标 CMake 注册门禁 + UI 契约 + 驾驶布局静态 + 视频管线结构 + 会话壳/四路视频 QML 清点。
# 对应矩阵：docs/CLIENT_UI_FEATURE_COVERAGE_MATRIX.md（§7.3 服务层 L2）
#
# 用法（仓库根目录）：
#   ./scripts/verify-client-ui-and-video-coverage.sh
#   WITH_DOCKER_LOGIN_SMOKE=1 ./scripts/verify-client-ui-and-video-coverage.sh   # 额外跑 verify-login-ui-features（需 client-dev）
#   WITH_LAYOUT_WIDTHS=1 ./scripts/verify-client-ui-and-video-coverage.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

WITH_DOCKER_LOGIN_SMOKE="${WITH_DOCKER_LOGIN_SMOKE:-0}"
WITH_LAYOUT_WIDTHS="${WITH_LAYOUT_WIDTHS:-0}"

COMPOSE_FILES="${COMPOSE_FILES:--f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml}"
COMPOSE_CMD="docker compose ${COMPOSE_FILES}"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

step() { echo -e "${CYAN}>>> $*${NC}"; }

fail() { echo -e "${RED}[FAIL] $*${NC}" >&2; exit 1; }

echo ""
echo -e "${CYAN}========== verify-client-ui-and-video-coverage（L2 CMake 门禁 + UI/视频 L1 + Qt UI Google 门禁）==========${NC}"
echo ""

step "1/8 CMake 已注册 L2 服务层 CTest（降级/恢复/诊断/WHEP URL）"
_ensure_ctest_service_l2() {
  local name="$1"
  if ! grep -qE "add_executable\\(${name}\\b|add_client_isolated_module_test\\(${name}\\b" \
      "$PROJECT_ROOT/client/CMakeLists.txt"; then
    fail "client/CMakeLists.txt 未注册 CTest 目标: ${name}"
  fi
}
_ensure_ctest_service_l2 test_webrtcurlresolve
_ensure_ctest_service_l2 test_degradationmanager
_ensure_ctest_service_l2 test_errorrecoverymanager
_ensure_ctest_service_l2 test_diagnosticsservice
echo "  L2 服务层四目标已注册"

step "2/8 verify-client-contract.sh（QML 控制路径）"
bash "$SCRIPT_DIR/verify-client-contract.sh" || fail "verify-client-contract.sh"

step "3/8 verify-client-ui-module-contract.sh（DrivingFacade v3 + canonical QML 链）"
bash "$SCRIPT_DIR/verify-client-ui-module-contract.sh" || fail "verify-client-ui-module-contract.sh"

step "4/8 verify-client-qt-ui-google-style.sh（qmllint + UI C++ format，Google 级可执行）"
bash "$SCRIPT_DIR/verify-client-qt-ui-google-style.sh" || fail "verify-client-qt-ui-google-style.sh"

step "5/8 verify-driving-layout.sh（远驾布局静态）"
bash "$SCRIPT_DIR/verify-driving-layout.sh" || fail "verify-driving-layout.sh"

step "6/8 verify-client-video-pipeline.sh（视频 C++/QML 结构）"
bash "$SCRIPT_DIR/verify-client-video-pipeline.sh" || fail "verify-client-video-pipeline.sh"

step "7/8 会话壳 + 四路视频标题 + RemoteVideoSurface 清点"

check_file() {
  local p="$PROJECT_ROOT/$1"
  [[ -f "$p" ]] || fail "缺失文件: $1"
}

check_file client/qml/shell/SessionWorkspace.qml
check_file client/qml/shell/LoginStage.qml
check_file client/qml/shell/VehiclePickStage.qml
check_file client/qml/shell/DrivingStageHost.qml
check_file client/qml/shell/SessionConstants.qml
check_file client/qml/components/VideoPanel.qml
check_file client/qml/components/driving/DrivingLeftRail.qml
check_file client/qml/components/driving/DrivingRightRail.qml
check_file client/qml/components/driving/DrivingCenterColumn.qml

grep -q "SessionConstants.stageLogin" client/qml/shell/SessionWorkspace.qml \
  || fail "SessionWorkspace 应含 SessionConstants.stageLogin"
grep -q "RemoteVideoSurface" client/qml/components/VideoPanel.qml \
  || fail "VideoPanel.qml 应含 RemoteVideoSurface"
grep -q "RemoteVideoSurface" client/qml/components/driving/DrivingCenterColumn.qml \
  || fail "DrivingCenterColumn.qml 应含 RemoteVideoSurface（主视）"

grep -q 'title: "左视图"' client/qml/components/driving/DrivingLeftRail.qml \
  || fail "DrivingLeftRail 应含左视图 VideoPanel 标题"
grep -q 'title: "后视图"' client/qml/components/driving/DrivingLeftRail.qml \
  || fail "DrivingLeftRail 应含后视图 VideoPanel 标题"
grep -q 'title: "右视图"' client/qml/components/driving/DrivingRightRail.qml \
  || fail "DrivingRightRail 应含右视图 VideoPanel 标题"
grep -q 'text: "主视图"' client/qml/components/driving/DrivingCenterColumn.qml \
  || fail "DrivingCenterColumn 应含主视图文案"

echo "  会话壳与四路视频静态清点通过"

step "8/8 可选子脚本"
if [[ "$WITH_LAYOUT_WIDTHS" == "1" ]]; then
  bash "$SCRIPT_DIR/verify-layout-widths.sh" || fail "verify-layout-widths.sh"
  echo "  verify-layout-widths.sh 已通过"
else
  echo "  跳过 verify-layout-widths.sh（设 WITH_LAYOUT_WIDTHS=1 启用）"
fi

if [[ "$WITH_DOCKER_LOGIN_SMOKE" == "1" ]]; then
  if command -v docker >/dev/null 2>&1 && $COMPOSE_CMD ps client-dev 2>/dev/null | grep -qE 'Up|running'; then
    bash "$SCRIPT_DIR/verify-login-ui-features.sh" || fail "verify-login-ui-features.sh"
    echo "  verify-login-ui-features.sh 已通过"
  else
    fail "WITH_DOCKER_LOGIN_SMOKE=1 但未检测到运行中的 client-dev；请先 compose up client-dev"
  fi
else
  echo "  跳过 verify-login-ui-features.sh（设 WITH_DOCKER_LOGIN_SMOKE=1 且启动 client-dev）"
fi

echo ""
echo -e "${GREEN}========== verify-client-ui-and-video-coverage 全部通过 ==========${NC}"
echo "  服务层 L2 单测映射见 docs/CLIENT_UI_FEATURE_COVERAGE_MATRIX.md §7.3；脚本速查 §8"
