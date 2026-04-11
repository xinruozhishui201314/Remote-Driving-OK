#!/usr/bin/env bash
# V2：对 base 与当前 revision 的 OpenAPI 做 breaking 检测（Tufin oasdiff）。
# PR / 发布流水线中应对比 GITHUB_BASE_REF 或 origin/main。
#
# 环境变量:
#   CONTRACT_OPENAPI_BASE_REF  用于 git show 的 ref，默认 origin/main
#   CONTRACT_SKIP_OASDIFF=1    跳过（未装 docker 的本地环境）
#
# 用法:
#   ./scripts/verify-openapi-breaking-change.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

fail() { echo -e "${RED}[FAIL] $*${NC}" >&2; exit 1; }

SPEC_REL="backend/api/openapi.yaml"
SPEC="$PROJECT_ROOT/$SPEC_REL"
BASE_REF="${CONTRACT_OPENAPI_BASE_REF:-origin/main}"

if [[ "${CONTRACT_SKIP_OASDIFF:-0}" == "1" ]]; then
  echo -e "${YELLOW}[SKIP] CONTRACT_SKIP_OASDIFF=1${NC}"
  exit 0
fi

if ! command -v docker >/dev/null 2>&1; then
  echo -e "${YELLOW}[SKIP] 未找到 docker，无法运行 oasdiff；CI 中应启用或设 CONTRACT_SKIP_OASDIFF=1${NC}"
  exit 0
fi

TMP_BASE="$PROJECT_ROOT/.contract-openapi-base.yaml"
cleanup() { rm -f "$TMP_BASE"; }
trap cleanup EXIT

echo -e "${CYAN}>>> verify-openapi-breaking-change（base=$BASE_REF）${NC}"

git fetch origin "${BASE_REF#origin/}" --depth=50 2>/dev/null || true
if ! git show "${BASE_REF}:${SPEC_REL}" >"$TMP_BASE" 2>/dev/null; then
  echo -e "${YELLOW}[SKIP] 无法读取 ${BASE_REF}:${SPEC_REL}（可能是首次新增 OpenAPI）${NC}"
  exit 0
fi

if cmp -s "$TMP_BASE" "$SPEC"; then
  echo -e "${GREEN}[OK] OpenAPI 与 base 一致，无文件级 diff${NC}"
  exit 0
fi

# breaking 子命令：存在不兼容变更时非零退出
if docker run --rm \
  -v "$PROJECT_ROOT:/w" \
  -w /w \
  tufin/oasdiff:latest \
  breaking "/w/.contract-openapi-base.yaml" "/w/$SPEC_REL"; then
  echo -e "${GREEN}[OK] oasdiff：未发现 breaking change${NC}"
else
  fail "oasdiff 检测到 OpenAPI breaking change；若有意变更请 bump API major 并更新 consumers（见 project_spec.md §12）"
fi
