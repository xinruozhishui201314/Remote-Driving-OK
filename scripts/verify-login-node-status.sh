#!/usr/bin/env bash
# 验证登录页「节点状态」功能：与客户端 NodeHealthChecker 一致，对 Backend/Keycloak/ZLM 做健康探测
# 用法：./scripts/verify-login-node-status.sh
# 可选：SERVER_URL=http://backend:8080 或 http://localhost:8081（与客户端登录页服务器地址一致）

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE_FILES="${COMPOSE_FILES:--f docker-compose.yml -f docker-compose.vehicle.dev.yml}"
COMPOSE_CMD="docker compose ${COMPOSE_FILES}"
SERVER_URL="${SERVER_URL:-http://localhost:8081}"

GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'
log_section() { echo -e "${CYAN}[LOG] $*${NC}"; }
log_ok()     { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail()   { echo -e "  ${RED}[FAIL] $*${NC}"; }

echo ""
echo -e "${CYAN}========== 登录页节点状态验证（与客户端 NodeHealthChecker 一致）==========${NC}"
echo "  SERVER_URL=$SERVER_URL"
echo ""

# 解析 serverUrl 得到 backend / keycloak / zlm 的 base URL（与 client nodehealthchecker.cpp 一致）
BACKEND_HEALTH="${SERVER_URL%/}/health"
if [[ "$SERVER_URL" =~ ^https?://backend ]]; then
  KEYCLOAK_HEALTH="http://keycloak:8080/health/ready"
  ZLM_URL="http://zlmediakit:80/index/api/getServerConfig"
else
  # 宿主机：同 host 端口改为 8080 为 Keycloak
  KEYCLOAK_HEALTH="${SERVER_URL//:8081/:8080}"
  [[ "$KEYCLOAK_HEALTH" == *":8080"* ]] || KEYCLOAK_HEALTH="http://localhost:8080/health/ready"
  KEYCLOAK_HEALTH="${KEYCLOAK_HEALTH%/}/health/ready"
  ZLM_URL="http://localhost:80/index/api/getServerConfig"
fi
ZLM_VIDEO_URL="${ZLM_VIDEO_URL:-}"
if [[ -n "$ZLM_VIDEO_URL" ]]; then
  ZLM_URL="${ZLM_VIDEO_URL%/}/index/api/getServerConfig"
fi

FAILED=0

log_section "1) Backend GET /health"
if curl -sf -o /dev/null --connect-timeout 3 "$BACKEND_HEALTH" 2>/dev/null; then
  log_ok "Backend 可达 $BACKEND_HEALTH"
else
  log_fail "Backend 不可达 $BACKEND_HEALTH"
  FAILED=1
fi

log_section "2) Keycloak GET /health/ready"
if curl -sf -o /dev/null --connect-timeout 5 "$KEYCLOAK_HEALTH" 2>/dev/null; then
  log_ok "Keycloak 可达 $KEYCLOAK_HEALTH"
else
  log_fail "Keycloak 不可达 $KEYCLOAK_HEALTH"
  FAILED=1
fi

log_section "3) ZLM GET /index/api/getServerConfig（可选）"
if curl -sf -o /dev/null --connect-timeout 3 "$ZLM_URL" 2>/dev/null; then
  log_ok "ZLM 可达 $ZLM_URL"
else
  log_fail "ZLM 不可达 $ZLM_URL（拉流前非必须）"
  # 不把 ZLM 失败算作 FAILED，登录不依赖 ZLM
fi

echo ""
if [ $FAILED -eq 0 ]; then
  echo -e "${GREEN}========== 节点状态验证通过 ==========${NC}"
  echo "  客户端登录页「检测」按钮应显示 Backend/Keycloak 正常；ZLM 视部署而定。"
  exit 0
else
  echo -e "${RED}========== 节点状态验证未通过 ==========${NC}"
  echo "  请先启动 Backend/Keycloak 或检查 SERVER_URL。"
  exit 1
fi
