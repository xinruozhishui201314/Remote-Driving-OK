#!/usr/bin/env bash
# 客户端 UI 质量四链路一键串联：
#   1) 架构契约（DrivingFacade / 控制路径）
#   2) QML 静态（AppContext 扫描）
#   3) 布局与视频管线结构（grep + 可选 CMake）
#   4) 官方工具链：qmllint + QML 相关 C++ clang-format（需 Qt / Docker）
#
# CI 无 Docker、无 Qt 时：
#   CLIENT_UI_CHAIN_CI_LITE=1 ./scripts/verify-client-ui-quality-chain.sh
#   将跳过步骤 4，其余仍可在 ubuntu-latest 上执行。
#
# 用法（仓库根）：
#   ./scripts/verify-client-ui-quality-chain.sh
#   CLIENT_UI_CHAIN_CI_LITE=1 ./scripts/verify-client-ui-quality-chain.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
NC='\033[0m'

fail() { echo -e "${RED}[FAIL] $*${NC}" >&2; exit 1; }
step() { echo -e "\n${CYAN}========== $* ==========${NC}"; }

echo -e "${CYAN}>>> verify-client-ui-quality-chain（官方工具链 + 契约 + 门禁 + 结构验证）${NC}"
if [[ "${CLIENT_UI_CHAIN_CI_LITE:-0}" == "1" ]]; then
  echo -e "${YELLOW}  模式: CLIENT_UI_CHAIN_CI_LITE=1（跳过 qmllint + QML 相关 C++ format）${NC}"
fi

step "[链 A] 架构契约：UI 模块（DrivingFacade v3）"
bash "$SCRIPT_DIR/verify-client-ui-module-contract.sh" || fail "verify-client-ui-module-contract.sh"

step "[链 B] 架构契约：QML 控制路径（禁止直发 vehicle/control）"
bash "$SCRIPT_DIR/verify-client-contract.sh" || fail "verify-client-contract.sh"

step "[链 C] QML 静态：AppContext 使用扫描"
if [[ -f "$SCRIPT_DIR/verify-qml-appcontext-imports.sh" ]]; then
  bash "$SCRIPT_DIR/verify-qml-appcontext-imports.sh" || fail "verify-qml-appcontext-imports.sh"
else
  echo "  [SKIP] 无 verify-qml-appcontext-imports.sh"
fi

step "[链 D] 布局静态（verify-driving-layout 快速模式）"
bash "$SCRIPT_DIR/verify-driving-layout.sh" || fail "verify-driving-layout.sh"

step "[链 E] 视频管线结构（verify-client-video-pipeline，无 --build）"
bash "$SCRIPT_DIR/verify-client-video-pipeline.sh" || fail "verify-client-video-pipeline.sh"

step "[链 E2] CPU 视频 RGBA8888 契约（禁止 BGR/RGB32 热路径）"
bash "$SCRIPT_DIR/verify-client-video-rgba-contract.sh" || fail "verify-client-video-rgba-contract.sh"

if [[ "${CLIENT_UI_CHAIN_CI_LITE:-0}" == "1" ]]; then
  echo -e "\n${YELLOW}>>> 跳过 [链 F]：verify-client-qt-ui-google-style（需 qmllint 或 Docker）${NC}"
  echo "  完整链路请在 client-dev 或本机执行: ./scripts/verify-client-qt-ui-google-style.sh"
else
  step "[链 F] 官方工具链：qmllint + QML 相关 C++ format"
  bash "$SCRIPT_DIR/verify-client-qt-ui-google-style.sh" || fail "verify-client-qt-ui-google-style.sh"
fi

if [[ "${VERIFY_QMLTYPES_DRIFT:-0}" == "1" ]]; then
  step "[链 G] qmltypes 生成物与 Git 一致（需本机 CMake/Qt 或容器内依赖）"
  bash "$SCRIPT_DIR/verify-qmltypes-match-generated.sh" || fail "verify-qmltypes-match-generated.sh"
fi

echo -e "\n${GREEN}[OK] verify-client-ui-quality-chain 完成${NC}"
echo "  测试/观测：ctest、verify-all-client-modules、OBSERVABILITY_METRICS 见 docs/CLIENT_UI_QUALITY_CHAIN.md"
