#!/usr/bin/env bash
# 按 client/.clang-format（Google 基线）格式化 client/src 与 client/tests 下全部 .cpp/.h。
# 宿主机若无 clang-format-18，可在 client-dev 容器内执行本脚本。
#
# 用法：
#   ./scripts/format-client-cpp.sh
#   FORMAT_CMD=clang-format-18 ./scripts/format-client-cpp.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLIENT_DIR="${CLIENT_DIR:-$REPO_ROOT/client}"
FMT="${FORMAT_CMD:-clang-format-18}"

if ! command -v "$FMT" >/dev/null 2>&1; then
  echo "[format-client-cpp] 未找到 $FMT。请安装 LLVM 18 套件，或在 remote-driving-client-dev:full 内执行。" >&2
  exit 1
fi

cd "$CLIENT_DIR"
n=0
while IFS= read -r -d "" f; do
  "$FMT" -i "$f"
  n=$((n + 1))
done < <(find src tests -type f \( -name "*.cpp" -o -name "*.h" \) -print0)

echo "[format-client-cpp] Formatted $n files under $CLIENT_DIR (src, tests)"
