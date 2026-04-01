#!/bin/bash
# 验证客户端与后端集成：VIN 列表 + 会话创建

set -e

BACKEND_URL="${BACKEND_URL:-http://localhost:8081}"
KEYCLOAK_URL="${KEYCLOAK_URL:-http://localhost:8080}"

echo "=========================================="
echo "客户端-后端集成验证"
echo "=========================================="
echo "Backend URL: $BACKEND_URL"
echo "Keycloak URL: $KEYCLOAK_URL"
echo ""

# 1) 获取 Keycloak Token（使用 e2e-test 账号）
echo "[1/4] 从 Keycloak 获取 access token..."
TOKEN_RESP=$(curl -s -X POST "$KEYCLOAK_URL/realms/teleop/protocol/openid-connect/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password" \
  -d "client_id=teleop-client" \
  -d "username=e2e-test" \
  -d "password=e2e-test-password")

ACCESS_TOKEN=$(echo "$TOKEN_RESP" | python3 -c "import sys, json; print(json.load(sys.stdin).get('access_token', ''))" 2>/dev/null || echo "")

if [ -z "$ACCESS_TOKEN" ]; then
    echo "❌ 获取 token 失败"
    echo "响应: $TOKEN_RESP"
    exit 1
fi

echo "✓ Token 获取成功（前 20 字符: ${ACCESS_TOKEN:0:20}...）"
echo ""

# 2) 调用 GET /api/v1/vins
echo "[2/4] 调用 GET /api/v1/vins..."
VINS_RESP=$(curl -s -w "\n%{http_code}" \
  -H "Authorization: Bearer $ACCESS_TOKEN" \
  "$BACKEND_URL/api/v1/vins")

HTTP_CODE=$(echo "$VINS_RESP" | tail -n 1)
VINS_BODY=$(echo "$VINS_RESP" | head -n -1)

if [ "$HTTP_CODE" != "200" ]; then
    echo "❌ 获取 VIN 列表失败，HTTP $HTTP_CODE"
    echo "响应: $VINS_BODY"
    exit 1
fi

VIN_COUNT=$(echo "$VINS_BODY" | python3 -c "import sys, json; vins = json.load(sys.stdin).get('vins', []); print(len(vins))" 2>/dev/null || echo "0")
FIRST_VIN=$(echo "$VINS_BODY" | python3 -c "import sys, json; vins = json.load(sys.stdin).get('vins', []); print(vins[0] if vins else '')" 2>/dev/null || echo "")

if [ -z "$FIRST_VIN" ]; then
    echo "⚠️  VIN 列表为空，请先在数据库中添加测试 VIN"
    exit 1
fi

echo "✓ 找到 $VIN_COUNT 个 VIN，第一个: $FIRST_VIN"
echo ""

# 3) 调用 POST /api/v1/vins/{vin}/sessions
echo "[3/4] 为 VIN $FIRST_VIN 创建会话..."
SESSION_RESP=$(curl -s -w "\n%{http_code}" \
  -X POST \
  -H "Authorization: Bearer $ACCESS_TOKEN" \
  -H "Content-Type: application/json" \
  "$BACKEND_URL/api/v1/vins/$FIRST_VIN/sessions")

HTTP_CODE=$(echo "$SESSION_RESP" | tail -n 1)
SESSION_BODY=$(echo "$SESSION_RESP" | head -n -1)

if [ "$HTTP_CODE" != "201" ]; then
    echo "❌ 创建会话失败，HTTP $HTTP_CODE"
    echo "响应: $SESSION_BODY"
    exit 1
fi

SESSION_ID=$(echo "$SESSION_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('sessionId', ''))" 2>/dev/null || echo "")
WHIP_URL=$(echo "$SESSION_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('media', {}).get('whip', ''))" 2>/dev/null || echo "")
WHEP_URL=$(echo "$SESSION_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('media', {}).get('whep', ''))" 2>/dev/null || echo "")
CONTROL_ALGO=$(echo "$SESSION_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('control', {}).get('algo', ''))" 2>/dev/null || echo "")

if [ -z "$SESSION_ID" ]; then
    echo "❌ 响应中缺少 sessionId"
    echo "响应: $SESSION_BODY"
    exit 1
fi

echo "✓ 会话创建成功"
echo "  Session ID: $SESSION_ID"
echo "  WHIP URL: $WHIP_URL"
echo "  WHEP URL: $WHEP_URL"
echo "  控制协议: $CONTROL_ALGO"
echo ""

# 4) 调用 GET /api/v1/sessions/{sessionId} 验证会话状态
echo "[4/4] 验证会话状态..."
SESSION_STATUS_RESP=$(curl -s -w "\n%{http_code}" \
  -H "Authorization: Bearer $ACCESS_TOKEN" \
  "$BACKEND_URL/api/v1/sessions/$SESSION_ID")

HTTP_CODE=$(echo "$SESSION_STATUS_RESP" | tail -n 1)
STATUS_BODY=$(echo "$SESSION_STATUS_RESP" | head -n -1)

if [ "$HTTP_CODE" != "200" ]; then
    echo "❌ 查询会话状态失败，HTTP $HTTP_CODE"
    echo "响应: $STATUS_BODY"
    exit 1
fi

SESSION_VIN=$(echo "$STATUS_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('vin', ''))" 2>/dev/null || echo "")
SESSION_STATE=$(echo "$STATUS_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('state', ''))" 2>/dev/null || echo "")

echo "✓ 会话状态查询成功"
echo "  VIN: $SESSION_VIN"
echo "  State: $SESSION_STATE"
echo ""

echo "=========================================="
echo "✅ 所有验证通过！"
echo "=========================================="
echo ""
echo "客户端现在可以："
echo "  1. 使用 Token 调用 GET /api/v1/vins 获取 VIN 列表"
echo "  2. 使用 Token + VIN 调用 POST /api/v1/vins/{vin}/sessions 创建会话"
echo "  3. 在 UI 中显示 sessionId、WHIP/WHEP URL、控制协议配置"
echo ""
