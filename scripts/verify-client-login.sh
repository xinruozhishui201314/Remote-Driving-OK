#!/usr/bin/env bash
# 验证客户端登录链路与日志：Keycloak → JWT → GET /api/v1/vins
# 用于精准定位 e2e-test 登录后闪退或车辆列表拉取失败。
# 用法：./scripts/verify-client-login.sh
#       可选：COMPOSE_FILES="-f docker-compose.yml -f docker-compose.vehicle.dev.yml" ./scripts/verify-client-login.sh
#       可选：DO_BUILD_CLIENT=1 在 client-dev 容器内编译客户端后再给出运行与日志检查说明

set -e
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE_FILES="${COMPOSE_FILES:--f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml}"
COMPOSE_CMD="docker compose ${COMPOSE_FILES}"
KEYCLOAK_URL="${KEYCLOAK_URL:-http://localhost:8080}"
BACKEND_URL="${BACKEND_URL:-http://localhost:8081}"
REALM="${REALM:-teleop}"
CLIENT_ID="${CLIENT_ID:-teleop-client}"
E2E_USER="${E2E_USER:-e2e-test}"
E2E_PASSWORD="${E2E_PASSWORD:-e2e-test-password}"
DO_BUILD_CLIENT="${DO_BUILD_CLIENT:-0}"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[LOG] $*${NC}"; }
log_ok()     { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail()   { echo -e "  ${RED}[FAIL] $*${NC}"; }
log_warn()   { echo -e "  ${YELLOW}[WARN] $*${NC}"; }

echo ""
echo -e "${CYAN}========== 客户端登录链路验证（Keycloak → JWT → /api/v1/vins）==========${NC}"
echo ""

# 1) Keycloak token（与客户端 Keycloak 登录一致）
log_section "1) 从 Keycloak 获取 JWT (${E2E_USER})"
if $COMPOSE_CMD ps backend 2>/dev/null | grep -q "Up"; then
  TOKEN_URL="http://keycloak:8080/realms/${REALM}/protocol/openid-connect/token"
  RESP=$($COMPOSE_CMD exec -T backend curl -s -w "\n%{http_code}" -X POST "${TOKEN_URL}" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "grant_type=password" \
    -d "client_id=${CLIENT_ID}" \
    -d "username=${E2E_USER}" \
    -d "password=${E2E_PASSWORD}") || true
else
  TOKEN_URL="${KEYCLOAK_URL}/realms/${REALM}/protocol/openid-connect/token"
  RESP=$(curl -s -w "\n%{http_code}" -X POST "${TOKEN_URL}" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "grant_type=password" \
    -d "client_id=${CLIENT_ID}" \
    -d "username=${E2E_USER}" \
    -d "password=${E2E_PASSWORD}") || true
fi
HTTP_CODE=$(echo "$RESP" | tail -n1)
BODY=$(echo "$RESP" | sed '$d')
ACCESS_TOKEN=$(echo "$BODY" | sed -n 's/.*"access_token":"\([^"]*\)".*/\1/p')

if [ -z "$ACCESS_TOKEN" ]; then
  log_fail "未获取到 access_token。HTTP=$HTTP_CODE bodyPreview=${BODY:0:200}"
  echo "  可能原因: Keycloak 未就绪、realm/用户不存在、密码错误。请检查 Keycloak 日志与 realm 导入。"
  exit 1
fi
log_ok "已获取 JWT tokenLen=${#ACCESS_TOKEN}"

# 2) GET /api/v1/vins（与客户端登录成功后拉取车辆列表一致）
log_section "2) GET /api/v1/vins (Bearer JWT)"
if $COMPOSE_CMD ps backend 2>/dev/null | grep -q "Up"; then
  VINS_RESP=$($COMPOSE_CMD exec -T -e BEARER_TOKEN="${ACCESS_TOKEN}" backend curl -s -w "\n%{http_code}" \
    -H "Authorization: Bearer ${ACCESS_TOKEN}" "http://localhost:8080/api/v1/vins") || true
else
  VINS_RESP=$(curl -s -w "\n%{http_code}" -H "Authorization: Bearer ${ACCESS_TOKEN}" "${BACKEND_URL}/api/v1/vins") || true
fi
VINS_HTTP=$(echo "$VINS_RESP" | tail -n1)
VINS_BODY=$(echo "$VINS_RESP" | sed '$d')

if [ "$VINS_HTTP" != "200" ]; then
  log_fail "GET /api/v1/vins 返回 HTTP $VINS_HTTP body=${VINS_BODY:0:150}"
  echo "  可能原因: Backend 未就绪、JWT 无效、或 backend 未注册 GET /api/v1/vins。"
  exit 1
fi
if ! echo "$VINS_BODY" | grep -q '"vins"'; then
  log_fail "响应中无 vins 字段 body=${VINS_BODY:0:200}"
  exit 1
fi
log_ok "GET /api/v1/vins 返回 200，车辆列表有效"

# 3) 客户端日志关键字说明
log_section "3) 客户端运行后请依据日志精准定位问题"
echo "  登录流程预期日志顺序（e2e-test）："
echo "    [Client][Auth] 发起登录: username= e2e-test serverUrl= ..."
echo "    [Client][Auth] Keycloak token URL: .../realms/teleop/protocol/openid-connect/token"
echo "    [Client][Auth] onLoginReply: HTTP 200 bodySize= ..."
echo "    [Client][Auth] onLoginReply: 登录成功 username= e2e-test tokenLen= ... → emit loginSucceeded"
echo "    [Client][Main] loginSucceeded: isTestToken= false ... 调用 loadVehicleList serverUrl= ..."
echo "    [Client][车辆列表] 请求 GET .../api/v1/vins hasToken= true tokenLen= ..."
echo "    [Client][车辆列表] onVehicleListReply: HTTP 200 bodySize= ..."
echo "    [Client][车辆列表] 已加载 count= N vins= ..."
echo "  若闪退，请查看最后一条 [Client][Auth] 或 [Client][车辆列表] 或 [Client][Main] 判断断点。"
echo "  失败时常见："
echo "    - onLoginReply: HTTP 错误 → Keycloak 不可达或返回 4xx"
echo "    - 响应非 JSON / 响应中无 access_token → Keycloak 返回了错误页或格式不符"
echo "    - 车辆列表 请求错误 / HTTP 错误 → Backend 不可达或 401"
echo ""

# 4) 可选：编译客户端
if [ "$DO_BUILD_CLIENT" = "1" ]; then
  log_section "4) 编译客户端 (client-dev 容器)"
  if ! $COMPOSE_CMD ps client-dev 2>/dev/null | grep -q "Up"; then
    log_warn "client-dev 未运行，跳过编译。请先: docker compose up -d client-dev"
  else
    BUILD_CMD='mkdir -p /tmp/client-build && cd /tmp/client-build && (test -f CMakeCache.txt || cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug) && make -j4'
    _out="$(mktemp)"
    if $COMPOSE_CMD exec -T client-dev bash -c "$BUILD_CMD" >"$_out" 2>&1; then
      tail -20 "$_out"
      rm -f "$_out"
      log_ok "客户端编译完成。运行并抓日志示例："
      echo "  $COMPOSE_CMD exec -it -e DISPLAY=\$DISPLAY client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient 2>&1 | tee /tmp/client.log'"
      echo "  登录 e2e-test 后: grep -E \"\\[Client\\]\\[Auth\\]|\\[Client\\]\\[车辆列表\\]|\\[Client\\]\\[Main\\]|\\[Client\\]\\[UI\\]\" /tmp/client.log"
    else
      tail -30 "$_out"
      rm -f "$_out"
      log_fail "客户端编译失败，请查看上方输出"
      exit 1
    fi
  fi
else
  echo "  如需编译并运行客户端，可执行: DO_BUILD_CLIENT=1 $0"
fi

echo ""
echo -e "${GREEN}========== 登录链路验证通过 ==========${NC}"
echo "  Keycloak 出 JWT、Backend GET /api/v1/vins 正常。客户端修改后请重新编译并按上述日志关键字排查。"
exit 0
