#!/usr/bin/env bash
# V1：跨服务契约 — OpenAPI 中声明的路径与 Backend C++ 注册路由一致性（启发式扫描）。
# 依赖：仅 PyYAML（与 validate_api_against_openapi.py 一致，系统包常已带）。
#
# 用法:
#   ./scripts/verify-contract-v1-cross-service.sh
# 若需将「实现未写入 OpenAPI」的 WARNING 视为失败:
#   CONTRACT_API_WARNINGS_AS_ERRORS=1 ./scripts/verify-contract-v1-cross-service.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

fail() { echo -e "${RED}[FAIL] $*${NC}" >&2; exit 1; }

TMP_OUT="$(mktemp)"
cleanup() { rm -f "$TMP_OUT"; }
trap cleanup EXIT

set +e
python3 "$SCRIPT_DIR/validate_api_against_openapi.py" >"$TMP_OUT" 2>&1
code=$?
set -e
cat "$TMP_OUT"

if [[ "$code" -ne 0 ]]; then
  fail "validate_api_against_openapi.py 未通过（OpenAPI 与 Backend 路由不一致或 OpenAPI YAML 无效）"
fi

if [[ "${CONTRACT_API_WARNINGS_AS_ERRORS:-0}" == "1" ]]; then
  if grep -q "^WARNINGS:" "$TMP_OUT" || grep -q "Warnings: *[1-9]" "$TMP_OUT"; then
    echo -e "${YELLOW}CONTRACT_API_WARNINGS_AS_ERRORS=1：存在 WARNING，视为失败${NC}"
    fail "请补全 OpenAPI 文档或收敛 Backend 路由"
  fi
fi

echo -e "${GREEN}[OK] verify-contract-v1-cross-service 完成${NC}"
