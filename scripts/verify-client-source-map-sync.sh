#!/usr/bin/env bash
# MVP：校验 `docs/CLIENT_UNIT_TEST_SOURCE_MAP.md` 中 CTest 索引与 `client/CMakeLists.txt`
# 中单测注册一致（add_test / add_client_unit_test / add_client_isolated_module_test）。
#
# 用法（仓库根目录）：./scripts/verify-client-source-map-sync.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CMAKE_FILE="$REPO_ROOT/client/CMakeLists.txt"
DOC_FILE="$REPO_ROOT/docs/CLIENT_UNIT_TEST_SOURCE_MAP.md"

if [[ ! -f "$CMAKE_FILE" || ! -f "$DOC_FILE" ]]; then
  echo "[source-map-sync] 缺少 $CMAKE_FILE 或 $DOC_FILE" >&2
  exit 1
fi

tmp_cmake="$(mktemp)"
tmp_doc="$(mktemp)"
cleanup() { rm -f "$tmp_cmake" "$tmp_doc"; }
trap cleanup EXIT

{
  grep 'add_test(NAME test_' "$CMAKE_FILE" | sed -E 's/.*add_test\(NAME ([A-Za-z0-9_]+).*/\1/'
  grep 'add_test(NAME client_' "$CMAKE_FILE" | sed -E 's/.*add_test\(NAME ([A-Za-z0-9_]+).*/\1/'
  grep 'add_client_unit_test(test_' "$CMAKE_FILE" | sed -E 's/.*add_client_unit_test\(([A-Za-z0-9_]+).*/\1/'
  grep 'add_client_isolated_module_test(test_' "$CMAKE_FILE" | sed -E 's/.*add_client_isolated_module_test\(([A-Za-z0-9_]+).*/\1/'
} | sort -u >"$tmp_cmake"

# 仅解析「CTest 名称索引」小节表格第一列（`| \`ctest_name\``），避免映射大表误匹配
sed -n '/^## CTest 名称索引/,/^## 被测生产源码/p' "$DOC_FILE" \
  | sed -n 's/^| `\([A-Za-z0-9_]*\)`.*/\1/p' \
  | grep -E '^(test_|client_)' \
  | sort -u >"$tmp_doc"

missing_in_doc="$(comm -23 "$tmp_cmake" "$tmp_doc" || true)"
extra_in_doc="$(comm -13 "$tmp_cmake" "$tmp_doc" || true)"

if [[ -n "$missing_in_doc" ]]; then
  echo "[source-map-sync][FAIL] CMake 已注册单测，但文档 CTest 表缺少：" >&2
  echo "$missing_in_doc" >&2
  exit 1
fi

if [[ -n "$extra_in_doc" ]]; then
  echo "[source-map-sync][FAIL] 文档 CTest 表中有 CMake 未注册的名称：" >&2
  echo "$extra_in_doc" >&2
  exit 1
fi

echo "[source-map-sync][OK] CTest 名称与 CLIENT_UNIT_TEST_SOURCE_MAP.md 一致（$(wc -l <"$tmp_cmake") 项）"
