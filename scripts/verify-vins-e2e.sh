#!/bin/bash
# 自动化验证 GET /api/v1/vins：用 e2e-test 用户取 Keycloak token，请求 /vins，校验返回含 E2ETESTVIN0000001
# 依赖：Keycloak、backend 已启动；realm 含 e2e-test 用户，DB 已执行 03_seed_test_data
# 从 backend 容器内请求 Keycloak（http://keycloak:8080），保证 JWT iss 与 backend 期望一致

set -e

BACKEND_URL="${BACKEND_URL:-http://localhost:8081}"
REALM="${REALM:-teleop}"
CLIENT_ID="${CLIENT_ID:-teleop-client}"
E2E_USER="${E2E_USER:-e2e-test}"
E2E_PASSWORD="${E2E_PASSWORD:-e2e-test-password}"
EXPECTED_VIN="${EXPECTED_VIN:-E2ETESTVIN0000001}"

TOKEN_URL="http://keycloak:8080/realms/${REALM}/protocol/openid-connect/token"

echo "取 Token (容器内 keycloak:8080) (${E2E_USER})"
RESP=$(docker compose exec -T backend curl -s -X POST "${TOKEN_URL}" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password" \
  -d "client_id=${CLIENT_ID}" \
  -d "username=${E2E_USER}" \
  -d "password=${E2E_PASSWORD}") || true

ACCESS_TOKEN=$(echo "$RESP" | sed -n 's/.*"access_token":"\([^"]*\)".*/\1/p')
if [ -z "$ACCESS_TOKEN" ]; then
  echo "FAIL: 无法获取 access_token。响应: $RESP"
  exit 1
fi

echo "请求 GET /api/v1/vins (容器内 localhost:8080)"
VINS_RESP=$(docker compose exec -T -e BEARER_TOKEN="${ACCESS_TOKEN}" backend sh -c 'curl -s -w "\n%{http_code}" -H "Authorization: Bearer $BEARER_TOKEN" http://localhost:8080/api/v1/vins')
HTTP_CODE=$(echo "$VINS_RESP" | tail -n1)
BODY=$(echo "$VINS_RESP" | sed '$d')

if [ "$HTTP_CODE" != "200" ]; then
  echo "FAIL: /api/v1/vins 返回 HTTP ${HTTP_CODE}，body: ${BODY}"
  exit 1
fi

if echo "$BODY" | grep -q "\"${EXPECTED_VIN}\""; then
  echo "PASS: /api/v1/vins 返回 200，且包含 VIN ${EXPECTED_VIN}"
  echo "body: $BODY"
  exit 0
fi

echo "FAIL: /api/v1/vins 返回 200 但 body 中未包含预期 VIN ${EXPECTED_VIN}"
echo "body: $BODY"
exit 1
