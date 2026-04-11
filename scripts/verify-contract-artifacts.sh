#!/usr/bin/env bash
# MVP：每层真源至少一条机器校验 — HTTP(OpenAPI) + MQTT(JSON Schema + golden)。
# 客户端 Facade：./scripts/verify-client-ui-module-contract.sh（见 client-ci / UI 链路）。
#
# 依赖：自动创建 .venv-contract 并 pip 安装 openapi-spec-validator、jsonschema、PyYAML
#（不使用 -r 聚合文件，避免与全局 pip hash 策略冲突）。
#
# 用法（仓库根）:
#   ./scripts/verify-contract-artifacts.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

fail() { echo -e "${RED}[FAIL] $*${NC}" >&2; exit 1; }

echo -e "${CYAN}>>> verify-contract-artifacts（OpenAPI + MQTT Schema + golden）${NC}"

VENV="$PROJECT_ROOT/.venv-contract"
if [[ ! -x "$VENV/bin/python" ]]; then
  echo "  创建本地 venv: $VENV"
  python3 -m venv "$VENV" || fail "无法创建 venv（需 python3-venv）"
fi

# 显式安装（--no-cache-dir 避免损坏 wheel 缓存触发 hash 误报）
env PIP_REQUIRE_HASHES= PIP_CONSTRAINT= "$VENV/bin/pip" install --no-cache-dir -q \
  "openapi-spec-validator>=0.7.1,<0.8" \
  "jsonschema>=4.10" \
  "PyYAML>=6" \
  || fail "pip 安装契约校验依赖失败（检查网络/代理/PEP668；可尝试 pip cache purge）"

"$VENV/bin/python" "$SCRIPT_DIR/validate_contract_artifacts.py" || fail "validate_contract_artifacts.py"

echo -e "${GREEN}[OK] verify-contract-artifacts 完成${NC}"
