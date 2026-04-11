#!/usr/bin/env bash
# 构建 RemoteDrivingClient 后，检查 qmltyperegistrar 生成物是否与 Git 提交一致。
# 与 verify-qml-changes.sh / regenerate-client-qmltypes.sh 配套；CI 或本地在具备 Qt/CMake 时使用。
#
# 用法（仓库根）：
#   ./scripts/verify-qmltypes-match-generated.sh
# Docker（全仓库挂载）：
#   docker run --rm -v "$PWD:/repo" remote-driving-client-dev:full bash /repo/scripts/verify-qmltypes-match-generated.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

fail() { echo -e "${RED}[FAIL] $*${NC}" >&2; exit 1; }

FILES=(
  "client/qml/remote-driving-cpp.qmltypes"
  "client/qml/DrivingFacade/driving-facade.qmltypes"
)

for f in "${FILES[@]}"; do
  [[ -f "$ROOT/$f" ]] || fail "缺少 $f（请先完整构建 RemoteDrivingClient）"
done

run_build() {
  local build_dir="${VERIFY_QMLTYPES_BUILD_DIR:-${TMPDIR:-/tmp}/verify-qmltypes-build}"
  local jobs="${VERIFY_QMLTYPES_JOBS:-$(nproc 2>/dev/null || echo 4)}"
  mkdir -p "$build_dir"
  ( cd "$build_dir" && cmake "$ROOT/client" -DCMAKE_BUILD_TYPE="${VERIFY_QMLTYPES_BUILD_TYPE:-Debug}" \
    && cmake --build . --target RemoteDrivingClient -j"$jobs" )
}

echo -e "${CYAN}>>> verify-qmltypes-match-generated（构建后 git diff --exit-code）${NC}"
run_build

if git diff --quiet -- "${FILES[@]}"; then
  echo -e "${GREEN}[OK] qmltypes 与当前工作树一致${NC}"
  exit 0
fi

echo -e "${RED}以下文件与构建生成结果不一致，请运行 ./scripts/regenerate-client-qmltypes.sh 并提交：${NC}"
git diff --stat -- "${FILES[@]}" || true
git diff -- "${FILES[@]}" | head -120 || true
exit 1
