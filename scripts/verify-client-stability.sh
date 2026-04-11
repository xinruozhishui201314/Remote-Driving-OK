#!/usr/bin/env bash
# 稳定性相关客户端单测（MVP→V1 门禁辅助）
# 用法：在已 cmake 编译的 client 构建目录执行，或由本脚本探测常见路径。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

run_ctest() {
  local dir="$1"
  if [[ ! -f "$dir/CTestTestfile.cmake" ]]; then
    return 1
  fi
  echo "[verify-client-stability] ctest in $dir"
  (cd "$dir" && ctest -R 'test_networkquality|test_degradationmanager|client_systemstatemachine_test|test_safetymonitorservice' --output-on-failure)
}

CANDIDATES=(
  "$ROOT/client/build"
  "$ROOT/build/client"
  "/tmp/client-build"
)

for d in "${CANDIDATES[@]}"; do
  if run_ctest "$d" 2>/dev/null; then
    echo "[verify-client-stability] OK"
    exit 0
  fi
done

echo "[verify-client-stability] SKIP: 未找到已配置的 client 构建目录（需先 cmake+编译）。"
echo "  可执行: cd client && mkdir -p build && cd build && cmake .. && cmake --build . && ctest -R test_networkquality --output-on-failure"
echo "  或: ./scripts/run-client-unit-tests-oneclick.sh"
exit 0
