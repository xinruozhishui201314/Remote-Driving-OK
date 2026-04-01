#!/usr/bin/env bash
# 等待各服务健康端点就绪（用于编排与 CI）
# 用法：
#   ./scripts/wait-for-health.sh
#   BACKEND_URL=http://backend:8081 ZLM_URL=http://zlm:80 ./scripts/wait-for-health.sh
#   WAIT_TIMEOUT=60 ./scripts/wait-for-health.sh
#
# 环境变量：
#   BACKEND_URL   - Backend 基地址，探测 GET $BACKEND_URL/health（默认 http://127.0.0.1:8081）
#   ZLM_URL       - ZLM 基地址，探测 GET $ZLM_URL/index/api/getServerConfig（默认 http://127.0.0.1:80）
#   KEYCLOAK_URL  - Keycloak 基地址，探测 GET $KEYCLOAK_URL/health/ready（默认 http://127.0.0.1:8080）
#   WAIT_TIMEOUT  - 总等待秒数（默认 60）
#   SKIP_KEYCLOAK - 1 时跳过 Keycloak 探测
#   SKIP_ZLM      - 1 时跳过 ZLM 探测
#   SKIP_BACKEND  - 1 时跳过 Backend 探测

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

BACKEND_URL="${BACKEND_URL:-http://127.0.0.1:8081}"
ZLM_URL="${ZLM_URL:-http://127.0.0.1:80}"
KEYCLOAK_URL="${KEYCLOAK_URL:-http://127.0.0.1:8080}"
WAIT_TIMEOUT="${WAIT_TIMEOUT:-60}"
SKIP_BACKEND="${SKIP_BACKEND:-0}"
SKIP_ZLM="${SKIP_ZLM:-0}"
SKIP_KEYCLOAK="${SKIP_KEYCLOAK:-0}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

log_ok()   { echo -e "${GREEN}[OK]${NC} $*"; }
log_wait() { echo -e "${YELLOW}[wait]${NC} $*"; }
log_fail() { echo -e "${RED}[FAIL]${NC} $*"; }

check_backend() {
    curl -sf --connect-timeout 2 "${BACKEND_URL%/}/health" >/dev/null 2>&1
}

check_zlm() {
    curl -sf --connect-timeout 2 "${ZLM_URL%/}/index/api/getServerConfig" >/dev/null 2>&1
}

check_keycloak() {
    curl -sf --connect-timeout 2 "${KEYCLOAK_URL%/}/health/ready" >/dev/null 2>&1
}

start=$(date +%s)
backend_ok=0
zlm_ok=0
kc_ok=0

echo "等待服务就绪（超时 ${WAIT_TIMEOUT}s）"
echo "  BACKEND_URL=${BACKEND_URL}"
echo "  ZLM_URL=${ZLM_URL}"
echo "  KEYCLOAK_URL=${KEYCLOAK_URL}"
echo ""

while true; do
    now=$(date +%s)
    if [ $(( now - start )) -ge "$WAIT_TIMEOUT" ]; then
        echo ""
        [ "$SKIP_BACKEND" != "1" ] && [ $backend_ok -eq 0 ] && log_fail "Backend 未就绪: ${BACKEND_URL%/}/health"
        [ "$SKIP_ZLM" != "1" ] && [ $zlm_ok -eq 0 ] && log_fail "ZLM 未就绪: ${ZLM_URL%/}/index/api/getServerConfig"
        [ "$SKIP_KEYCLOAK" != "1" ] && [ $kc_ok -eq 0 ] && log_fail "Keycloak 未就绪: ${KEYCLOAK_URL%/}/health/ready"
        exit 1
    fi

    [ "$SKIP_BACKEND" != "1" ] && [ $backend_ok -eq 0 ] && check_backend && backend_ok=1 && log_ok "Backend 就绪"
    [ "$SKIP_ZLM" != "1" ] && [ $zlm_ok -eq 0 ] && check_zlm && zlm_ok=1 && log_ok "ZLM 就绪"
    [ "$SKIP_KEYCLOAK" != "1" ] && [ $kc_ok -eq 0 ] && check_keycloak && kc_ok=1 && log_ok "Keycloak 就绪"

    need=0
    [ "$SKIP_BACKEND" != "1" ] && [ $backend_ok -eq 0 ] && need=1
    [ "$SKIP_ZLM" != "1" ] && [ $zlm_ok -eq 0 ] && need=1
    [ "$SKIP_KEYCLOAK" != "1" ] && [ $kc_ok -eq 0 ] && need=1
    [ $need -eq 0 ] && break

    log_wait "等待中... (Backend=$backend_ok ZLM=$zlm_ok Keycloak=$kc_ok)"
    sleep 2
done

echo ""
echo "所有指定服务已就绪。"
