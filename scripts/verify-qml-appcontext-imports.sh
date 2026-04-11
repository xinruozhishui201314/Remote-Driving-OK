#!/usr/bin/env bash
# 静态检查：子目录 QML 若引用 AppContext. 则必须在文件前 25 行包含正确的父目录 import，
# 避免运行期 ReferenceError: AppContext is not defined（见 client/qml/qmldir 注释）。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
QML_ROOT="$REPO_ROOT/client/qml"

failures=0

check_file() {
  local f="$1"
  local pattern="$2"
  if grep -q 'AppContext\.' "$f" 2>/dev/null; then
    if ! head -n 25 "$f" | grep -qE "$pattern"; then
      echo "[verify-qml-appcontext-imports] FAIL: $f uses AppContext but missing required import in first 25 lines (expected pattern: $pattern)" >&2
      failures=$((failures + 1))
    fi
  fi
}

# shell/、SessionConstants 等：父目录为 qml 根 → import ".."
while IFS= read -r -d '' f; do
  check_file "$f" '^import "\.\."'
done < <(find "$QML_ROOT/shell" -maxdepth 1 -name '*.qml' -print0 2>/dev/null)

# components/ 直下（非 driving）
while IFS= read -r -d '' f; do
  check_file "$f" '^import "\.\."'
done < <(find "$QML_ROOT/components" -maxdepth 1 -name '*.qml' -print0 2>/dev/null)

# components/driving/：qml 根需 import "../.."
while IFS= read -r -d '' f; do
  check_file "$f" '^import "\.\./\.\."'
done < <(find "$QML_ROOT/components/driving" -maxdepth 1 -name '*.qml' -print0 2>/dev/null)

# driving/internal/：不得使用 AppContext（DrivingFacade v3 → facade.appServices）；门禁见 verify-client-ui-module-contract.sh

if [[ "$failures" -gt 0 ]]; then
  echo "[verify-qml-appcontext-imports] 共 $failures 个文件未通过" >&2
  exit 1
fi

echo "[verify-qml-appcontext-imports] OK（已扫描 shell/、components/、components/driving/ 下使用 AppContext 的 QML；internal/ 由 verify-client-ui-module-contract 约束）"
exit 0
