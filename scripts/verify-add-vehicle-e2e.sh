#!/usr/bin/env bash
# 自动化验证「增加车辆」与「删除车辆」功能：API + 后端日志（含 vin_grants 权限链）
# 1) 获取 JWT → GET /vins 基线
# 2) POST /api/v1/vehicles 添加车辆 → GET /vins 含新车 → 检查后端日志：vehicles/account_vehicles/vin_grants 三步写入
# 3) GET /admin/add-vehicle 返回 200 且含「增加车辆」
# 4) DELETE /api/v1/vehicles/{vin} → GET /vins 不含该车 → 检查后端日志：account_vehicles 解除 + vin_grants 同步删除
# 依赖：Keycloak、Backend、Postgres 已启动（如 docker compose up -d teleop-postgres keycloak backend）
# 用法：./scripts/verify-add-vehicle-e2e.sh
#       若使用多文件启动：export COMPOSE_FILES="-f docker-compose.yml -f docker-compose.vehicle.dev.yml" 后执行
#
# 日志精准分析（grep 关键字）：
#   [Backend][AddVehicle] vehicles inserted vin=...         → 车辆表写入成功
#   [Backend][AddVehicle] account_vehicles inserted vin=... → 账号绑定成功
#   [Backend][AddVehicle] vin_grants inserted vin=...       → 权限写入成功（可正常接管）
#   [Backend][AddVehicle] 503 ... failed                    → 对应步骤失败，看 err= 或后续行
#   [Backend][AddVehicle] vin_grants deleted vin=... rows=  → 解除绑定时权限同步清理
#   [Backend][DELETE /api/v1/vehicles/...] 204               → 解除绑定成功

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# 默认使用 dev compose（backend 端口 8081、含 DATABASE_URL）
COMPOSE_FILES="${COMPOSE_FILES:--f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml}"
COMPOSE_CMD="docker compose ${COMPOSE_FILES}"

BACKEND_URL="${BACKEND_URL:-http://localhost:8081}"
REALM="${REALM:-teleop}"
CLIENT_ID="${CLIENT_ID:-teleop-client}"
E2E_USER="${E2E_USER:-e2e-test}"
E2E_PASSWORD="${E2E_PASSWORD:-e2e-test-password}"
# 使用唯一 VIN 避免与已有数据冲突；测试结束会 DELETE 清理
# VIN 最多 17 字符；e2e-add- 为 9 字符，后缀取 8 位
TEST_VIN="e2e-add-$(date +%s | tail -c 8)"
TEST_MODEL="e2e-model"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[LOG] $*${NC}"; }
log_ok()     { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail()   { echo -e "  ${RED}[FAIL] $*${NC}"; }

# 0) 确保 migrations + seed 已执行（修复 user not found / 503）
log_section "0) 执行 ensure-seed-data（migrations 001/002/003 + seed）"
if bash "$SCRIPT_DIR/ensure-seed-data.sh" 2>/dev/null; then
  log_ok "ensure-seed-data 完成"
else
  echo -e "  ${YELLOW}[WARN] ensure-seed-data 失败或跳过（Postgres 未启动时正常），继续验证${NC}"
fi

# 从 backend 容器内请求 Keycloak（与 verify-vins-e2e.sh 一致）
TOKEN_URL="http://keycloak:8080/realms/${REALM}/protocol/openid-connect/token"
log_section "1) 获取 JWT (${E2E_USER})"
RESP=$($COMPOSE_CMD exec -T backend curl -s -X POST "${TOKEN_URL}" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password" \
  -d "client_id=${CLIENT_ID}" \
  -d "username=${E2E_USER}" \
  -d "password=${E2E_PASSWORD}") || true

ACCESS_TOKEN=$(echo "$RESP" | sed -n 's/.*"access_token":"\([^"]*\)".*/\1/p')
if [ -z "$ACCESS_TOKEN" ]; then
  log_fail "无法获取 access_token。Keycloak 可能未就绪或账号错误。响应: ${RESP:0:200}"
  exit 1
fi
log_ok "已获取 JWT"

# 宿主机 curl（当 backend 未在 compose 中或从宿主机验证时）
api_curl_host() {
  local method="$1"
  local path="$2"
  local data="${3:-}"
  if [ "$method" = "POST" ] && [ -n "$data" ]; then
    curl -s -w "\n%{http_code}" -X POST -H "Authorization: Bearer ${ACCESS_TOKEN}" -H "Content-Type: application/json" -d "$data" "${BACKEND_URL}${path}"
  else
    curl -s -w "\n%{http_code}" -X "$method" -H "Authorization: Bearer ${ACCESS_TOKEN}" "${BACKEND_URL}${path}"
  fi
}

# 优先在容器内请求（与 Keycloak 同网段、iss 一致）；若 backend 未在 compose 中则用宿主机
if $COMPOSE_CMD ps backend 2>/dev/null | grep -q "Up"; then
  USE_CONTAINER=1
else
  USE_CONTAINER=0
  log_section "Backend 未在 compose 中运行，使用宿主机 BACKEND_URL=${BACKEND_URL}"
  # 宿主机取 token 需能访问 Keycloak
  TOKEN_URL_HOST="${KEYCLOAK_URL:-http://localhost:8080}/realms/${REALM}/protocol/openid-connect/token"
  RESP=$(curl -s -X POST "${TOKEN_URL_HOST}" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "grant_type=password" -d "client_id=${CLIENT_ID}" \
    -d "username=${E2E_USER}" -d "password=${E2E_PASSWORD}") || true
  ACCESS_TOKEN=$(echo "$RESP" | sed -n 's/.*"access_token":"\([^"]*\)".*/\1/p')
  if [ -z "$ACCESS_TOKEN" ]; then
    log_fail "宿主机无法获取 token。请确保 Keycloak 可访问且 e2e-test 用户存在。"
    exit 1
  fi
fi

log_section "2) GET /api/v1/vins 基线"
if [ "$USE_CONTAINER" = "1" ]; then
  VINS_RESP=$($COMPOSE_CMD exec -T -e BEARER_TOKEN="${ACCESS_TOKEN}" backend sh -c 'curl -s -w "\n%{http_code}" -H "Authorization: Bearer $BEARER_TOKEN" http://localhost:8080/api/v1/vins')
else
  VINS_RESP=$(curl -s -w "\n%{http_code}" -H "Authorization: Bearer ${ACCESS_TOKEN}" "${BACKEND_URL}/api/v1/vins")
fi
HTTP_CODE=$(echo "$VINS_RESP" | tail -n1)
BODY=$(echo "$VINS_RESP" | sed '$d')
if [ "$HTTP_CODE" != "200" ]; then
  log_fail "GET /api/v1/vins 返回 HTTP ${HTTP_CODE}"
  echo "$BODY"
  exit 1
fi
log_ok "GET /api/v1/vins 返回 200"

log_section "3) POST /api/v1/vehicles 添加车辆 vin=${TEST_VIN}"
POST_BODY="{\"vin\":\"${TEST_VIN}\",\"model\":\"${TEST_MODEL}\"}"
if [ "$USE_CONTAINER" = "1" ]; then
  POST_RESP=$($COMPOSE_CMD exec -T -e BEARER_TOKEN="${ACCESS_TOKEN}" -e BODY="${POST_BODY}" backend sh -c 'curl -s -w "\n%{http_code}" -X POST -H "Authorization: Bearer $BEARER_TOKEN" -H "Content-Type: application/json" -d "$BODY" http://localhost:8080/api/v1/vehicles')
else
  POST_RESP=$(api_curl_host POST "/api/v1/vehicles" "${POST_BODY}")
fi
POST_HTTP=$(echo "$POST_RESP" | tail -n1)
POST_BODY_RESP=$(echo "$POST_RESP" | sed '$d')
if [ "$POST_HTTP" != "201" ]; then
  log_fail "POST /api/v1/vehicles 返回 HTTP ${POST_HTTP}，期望 201。body: ${POST_BODY_RESP}"
  if [ "$POST_HTTP" = "404" ]; then
    # 依据日志判断：若 backend 有 [Backend][未匹配] method=POST path=/api/v1/vehicles 则说明请求已到达但无路由，需重新构建
    BACKEND_LOGS_404=$($COMPOSE_CMD logs --tail=100 backend 2>/dev/null || true)
    if echo "$BACKEND_LOGS_404" | grep -q "\[Backend\]\[未匹配\].*POST.*/api/v1/vehicles"; then
      echo -e "  ${CYAN}[依据日志] 后端已收到请求但无匹配路由，说明当前 backend 进程未包含 POST /api/v1/vehicles（旧镜像或未重新编译）。${NC}"
      echo -e "  ${YELLOW}处理：重新构建并重启 backend 后再验证：${NC}"
      echo "    docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml build backend && docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml up -d backend"
    elif echo "$BACKEND_LOGS_404" | grep -q "\[Backend\]\[启动\] 路由已注册"; then
      if ! echo "$BACKEND_LOGS_404" | grep -q "POST /api/v1/vehicles"; then
        echo -e "  ${CYAN}[依据日志] 启动日志中未包含「POST /api/v1/vehicles」，说明当前 binary 未注册该路由，需重新构建 backend。${NC}"
      fi
      echo -e "  ${YELLOW}处理：重新构建并重启 backend。${NC}"
    else
      echo -e "  ${YELLOW}提示：若为 404，请查看 backend 日志（docker compose logs backend --tail=50）确认是否有 [Backend][未匹配] 或 [Backend][启动] 路由已注册；无匹配路由则需重新构建并重启 backend。${NC}"
    fi
  fi
  exit 1
fi
log_ok "POST /api/v1/vehicles 返回 201"

log_section "4) GET /api/v1/vins 应包含新 VIN ${TEST_VIN}"
if [ "$USE_CONTAINER" = "1" ]; then
  VINS_RESP2=$($COMPOSE_CMD exec -T -e BEARER_TOKEN="${ACCESS_TOKEN}" backend sh -c 'curl -s -w "\n%{http_code}" -H "Authorization: Bearer $BEARER_TOKEN" http://localhost:8080/api/v1/vins')
else
  VINS_RESP2=$(curl -s -w "\n%{http_code}" -H "Authorization: Bearer ${ACCESS_TOKEN}" "${BACKEND_URL}/api/v1/vins")
fi
BODY2=$(echo "$VINS_RESP2" | sed '$d')
if ! echo "$BODY2" | grep -q "\"${TEST_VIN}\""; then
  log_fail "GET /api/v1/vins 未包含新添加的 VIN ${TEST_VIN}。body: ${BODY2}"
  exit 1
fi
log_ok "车辆列表已包含 ${TEST_VIN}"

log_section "5) 检查后端日志：POST 201 及 AddVehicle 三步（vehicles / account_vehicles / vin_grants）"
BACKEND_LOGS=$($COMPOSE_CMD logs --tail=120 backend 2>/dev/null || true)
ADD_VEHICLE_FAIL=0
if [ -n "$BACKEND_LOGS" ]; then
  if echo "$BACKEND_LOGS" | grep -q "\[Backend\]\[POST /api/v1/vehicles\].*201.*${TEST_VIN}"; then
    log_ok "后端日志含 [Backend][POST /api/v1/vehicles] 201 及 vin=${TEST_VIN}"
  else
    echo -e "  ${YELLOW}[WARN] 未匹配到 POST 201 vin=${TEST_VIN}${NC}"
    ADD_VEHICLE_FAIL=1
  fi
  if echo "$BACKEND_LOGS" | grep -q "\[Backend\]\[AddVehicle\] vehicles inserted vin=${TEST_VIN}"; then
    log_ok "后端日志含 [Backend][AddVehicle] vehicles inserted vin=${TEST_VIN}"
  else
    log_fail "未匹配到 [Backend][AddVehicle] vehicles inserted vin=${TEST_VIN}（可据此判断车辆表写入是否执行）"
    ADD_VEHICLE_FAIL=1
  fi
  if echo "$BACKEND_LOGS" | grep -q "\[Backend\]\[AddVehicle\] account_vehicles inserted vin=${TEST_VIN}"; then
    log_ok "后端日志含 [Backend][AddVehicle] account_vehicles inserted vin=${TEST_VIN}"
  else
    log_fail "未匹配到 [Backend][AddVehicle] account_vehicles inserted vin=${TEST_VIN}（可据此判断账号绑定是否执行）"
    ADD_VEHICLE_FAIL=1
  fi
  if echo "$BACKEND_LOGS" | grep -q "\[Backend\]\[AddVehicle\] vin_grants inserted vin=${TEST_VIN}"; then
    log_ok "后端日志含 [Backend][AddVehicle] vin_grants inserted vin=${TEST_VIN}（权限链已写入，可正常接管）"
  else
    log_fail "未匹配到 [Backend][AddVehicle] vin_grants inserted vin=${TEST_VIN}（权限未写入则远驾链路不完整）"
    ADD_VEHICLE_FAIL=1
  fi
  if [ "$ADD_VEHICLE_FAIL" = "1" ]; then
    echo -e "  ${CYAN}[依据日志分析] 可用: grep '\\[Backend\\]\\[AddVehicle\\]' <backend_logs> 查看三步执行顺序与失败点${NC}"
    exit 1
  fi
else
  echo -e "  ${YELLOW}[WARN] 无法获取 backend 容器日志（非 compose 运行时可忽略），跳过日志断言${NC}"
fi

log_section "6) GET /admin/add-vehicle 管理页 200 且含「增加车辆」"
if [ "$USE_CONTAINER" = "1" ]; then
  ADMIN_RESP=$($COMPOSE_CMD exec -T backend curl -s -w "\n%{http_code}" "http://localhost:8080/admin/add-vehicle")
else
  ADMIN_RESP=$(curl -s -w "\n%{http_code}" "${BACKEND_URL}/admin/add-vehicle")
fi
ADMIN_HTTP=$(echo "$ADMIN_RESP" | tail -n1)
ADMIN_BODY=$(echo "$ADMIN_RESP" | sed '$d')
if [ "$ADMIN_HTTP" != "200" ]; then
  log_fail "GET /admin/add-vehicle 返回 HTTP ${ADMIN_HTTP}，期望 200"
  exit 1
fi
if ! echo "$ADMIN_BODY" | grep -q "增加车辆"; then
  log_fail "管理页 body 中未包含「增加车辆」"
  exit 1
fi
log_ok "管理页返回 200 且含「增加车辆」"

log_section "7) DELETE /api/v1/vehicles/${TEST_VIN} 解除绑定"
if [ "$USE_CONTAINER" = "1" ]; then
  DEL_RESP=$($COMPOSE_CMD exec -T -e BEARER_TOKEN="${ACCESS_TOKEN}" -e VIN="${TEST_VIN}" backend sh -c 'curl -s -w "\n%{http_code}" -X DELETE -H "Authorization: Bearer $BEARER_TOKEN" "http://localhost:8080/api/v1/vehicles/$VIN"')
else
  DEL_RESP=$(curl -s -w "\n%{http_code}" -X DELETE -H "Authorization: Bearer ${ACCESS_TOKEN}" "${BACKEND_URL}/api/v1/vehicles/${TEST_VIN}")
fi
DEL_HTTP=$(echo "$DEL_RESP" | tail -n1)
if [ "$DEL_HTTP" != "204" ]; then
  log_fail "DELETE /api/v1/vehicles/${TEST_VIN} 返回 HTTP ${DEL_HTTP}，期望 204"
  exit 1
fi
log_ok "DELETE 返回 204"

log_section "8) GET /api/v1/vins 应不再包含 ${TEST_VIN}"
if [ "$USE_CONTAINER" = "1" ]; then
  VINS_RESP3=$($COMPOSE_CMD exec -T -e BEARER_TOKEN="${ACCESS_TOKEN}" backend sh -c 'curl -s -w "\n%{http_code}" -H "Authorization: Bearer $BEARER_TOKEN" http://localhost:8080/api/v1/vins')
else
  VINS_RESP3=$(curl -s -w "\n%{http_code}" -H "Authorization: Bearer ${ACCESS_TOKEN}" "${BACKEND_URL}/api/v1/vins")
fi
BODY3=$(echo "$VINS_RESP3" | sed '$d')
if echo "$BODY3" | grep -q "\"${TEST_VIN}\""; then
  log_fail "DELETE 后 GET /api/v1/vins 仍包含 ${TEST_VIN}。body: ${BODY3}"
  exit 1
fi
log_ok "车辆列表已不包含 ${TEST_VIN}"

log_section "9) 检查后端日志：DELETE 204 及 vin_grants 同步删除"
BACKEND_LOGS_LATEST=$($COMPOSE_CMD logs --tail=80 backend 2>/dev/null || true)
DELETE_LOG_FAIL=0
if [ -n "$BACKEND_LOGS_LATEST" ]; then
  if echo "$BACKEND_LOGS_LATEST" | grep -q "\[Backend\]\[DELETE /api/v1/vehicles/${TEST_VIN}\].*204"; then
    log_ok "后端日志含 [Backend][DELETE ...] 204"
  else
    echo -e "  ${RED}[FAIL] 未匹配到 [Backend][DELETE /api/v1/vehicles/${TEST_VIN}] 204${NC}"
    DELETE_LOG_FAIL=1
  fi
  if echo "$BACKEND_LOGS_LATEST" | grep -q "\[Backend\]\[AddVehicle\] vin_grants deleted vin=${TEST_VIN}"; then
    log_ok "后端日志含 [Backend][AddVehicle] vin_grants deleted vin=${TEST_VIN}（解除绑定时权限已同步清理）"
  else
    echo -e "  ${RED}[FAIL] 未匹配到 [Backend][AddVehicle] vin_grants deleted vin=${TEST_VIN}（解除绑定后权限未清理则列表与权限不一致）${NC}"
    DELETE_LOG_FAIL=1
  fi
  if [ "$DELETE_LOG_FAIL" = "1" ]; then
    echo -e "  ${CYAN}[依据日志分析] 可用: grep '\\[Backend\\]\\[AddVehicle\\]\\|DELETE.*vehicles' <backend_logs> 定位解除绑定与 vin_grants 删除${NC}"
    exit 1
  fi
else
  echo -e "  ${YELLOW}[WARN] 无法获取 backend 容器日志，跳过 DELETE 日志断言${NC}"
fi

echo ""
echo -e "${GREEN}========== 增加/删除车辆 E2E 验证通过 ==========${NC}"
echo "  已验证: POST 201、vehicles/account_vehicles/vin_grants 三步写入、GET /vins 含新车、"
echo "          GET /admin/add-vehicle、DELETE 204、vin_grants 同步删除、GET /vins 不含该车"
echo "  日志关键字: [Backend][AddVehicle] 用于精准分析各步执行与失败点"
exit 0
