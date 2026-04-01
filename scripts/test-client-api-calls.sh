#!/bin/bash
# 客户端 API 调用验证脚本（模拟客户端行为）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

BACKEND_URL="${BACKEND_URL:-http://localhost:8081}"
KEYCLOAK_URL="${KEYCLOAK_URL:-http://localhost:8080}"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}客户端 API 调用验证（模拟客户端行为）${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# 1. 获取 Token（模拟登录）
echo -e "${YELLOW}[1/4] 模拟客户端登录（获取 Token）${NC}"
TOKEN_RESP=$(curl -s -X POST "$KEYCLOAK_URL/realms/teleop/protocol/openid-connect/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password" \
  -d "client_id=teleop-client" \
  -d "username=e2e-test" \
  -d "password=e2e-test-password")

ACCESS_TOKEN=$(echo "$TOKEN_RESP" | python3 -c "import sys, json; print(json.load(sys.stdin).get('access_token', ''))" 2>/dev/null || echo "")

if [ -z "$ACCESS_TOKEN" ]; then
    echo -e "${RED}✗ 登录失败：无法获取 Token${NC}"
    echo "响应: $TOKEN_RESP"
    exit 1
fi

echo -e "${GREEN}✓ 登录成功，Token 获取成功${NC}"
echo "  Token 前 20 字符: ${ACCESS_TOKEN:0:20}..."
echo ""

# 2. 获取 VIN 列表（模拟车辆选择）
echo -e "${YELLOW}[2/4] 模拟客户端获取 VIN 列表${NC}"
VINS_RESP=$(curl -s -w "\n%{http_code}" \
  -H "Authorization: Bearer $ACCESS_TOKEN" \
  "$BACKEND_URL/api/v1/vins")

HTTP_CODE=$(echo "$VINS_RESP" | tail -n 1)
VINS_BODY=$(echo "$VINS_RESP" | head -n -1)

if [ "$HTTP_CODE" != "200" ]; then
    echo -e "${RED}✗ 获取 VIN 列表失败，HTTP $HTTP_CODE${NC}"
    echo "响应: $VINS_BODY"
    exit 1
fi

VIN_COUNT=$(echo "$VINS_BODY" | python3 -c "import sys, json; vins = json.load(sys.stdin).get('vins', []); print(len(vins))" 2>/dev/null || echo "0")
FIRST_VIN=$(echo "$VINS_BODY" | python3 -c "import sys, json; vins = json.load(sys.stdin).get('vins', []); print(vins[0] if vins else '')" 2>/dev/null || echo "")

if [ -z "$FIRST_VIN" ]; then
    echo -e "${RED}✗ VIN 列表为空${NC}"
    exit 1
fi

echo -e "${GREEN}✓ VIN 列表获取成功${NC}"
echo "  VIN 数量: $VIN_COUNT"
echo "  第一个 VIN: $FIRST_VIN"
echo ""

# 3. 创建会话（模拟会话创建）
echo -e "${YELLOW}[3/4] 模拟客户端创建会话${NC}"
SESSION_RESP=$(curl -s -w "\n%{http_code}" \
  -X POST \
  -H "Authorization: Bearer $ACCESS_TOKEN" \
  -H "Content-Type: application/json" \
  "$BACKEND_URL/api/v1/vins/$FIRST_VIN/sessions")

HTTP_CODE=$(echo "$SESSION_RESP" | tail -n 1)
SESSION_BODY=$(echo "$SESSION_RESP" | head -n -1)

if [ "$HTTP_CODE" != "201" ]; then
    echo -e "${RED}✗ 创建会话失败，HTTP $HTTP_CODE${NC}"
    echo "响应: $SESSION_BODY"
    exit 1
fi

SESSION_ID=$(echo "$SESSION_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('sessionId', ''))" 2>/dev/null || echo "")
WHIP_URL=$(echo "$SESSION_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('media', {}).get('whip', ''))" 2>/dev/null || echo "")
WHEP_URL=$(echo "$SESSION_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('media', {}).get('whep', ''))" 2>/dev/null || echo "")
CONTROL_ALGO=$(echo "$SESSION_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('control', {}).get('algo', ''))" 2>/dev/null || echo "")

if [ -z "$SESSION_ID" ]; then
    echo -e "${RED}✗ 响应中缺少 sessionId${NC}"
    echo "响应: $SESSION_BODY"
    exit 1
fi

echo -e "${GREEN}✓ 会话创建成功${NC}"
echo "  Session ID: $SESSION_ID"
echo "  WHIP URL: $WHIP_URL"
echo "  WHEP URL: $WHEP_URL"
echo "  控制协议: $CONTROL_ALGO"
echo ""

# 4. 查询会话状态（模拟会话信息查询）
echo -e "${YELLOW}[4/4] 模拟客户端查询会话状态${NC}"
SESSION_STATUS_RESP=$(curl -s -w "\n%{http_code}" \
  -H "Authorization: Bearer $ACCESS_TOKEN" \
  "$BACKEND_URL/api/v1/sessions/$SESSION_ID")

HTTP_CODE=$(echo "$SESSION_STATUS_RESP" | tail -n 1)
STATUS_BODY=$(echo "$SESSION_STATUS_RESP" | head -n -1)

if [ "$HTTP_CODE" != "200" ]; then
    echo -e "${RED}✗ 查询会话状态失败，HTTP $HTTP_CODE${NC}"
    echo "响应: $STATUS_BODY"
    exit 1
fi

SESSION_VIN=$(echo "$STATUS_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('vin', ''))" 2>/dev/null || echo "")
SESSION_STATE=$(echo "$STATUS_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('state', ''))" 2>/dev/null || echo "")

echo -e "${GREEN}✓ 会话状态查询成功${NC}"
echo "  VIN: $SESSION_VIN"
echo "  State: $SESSION_STATE"
echo ""

echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}✅ 所有 API 调用验证通过！${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "客户端应该能够："
echo "  1. ✅ 成功登录并获取 Token"
echo "  2. ✅ 成功获取 VIN 列表"
echo "  3. ✅ 成功创建会话并获取会话信息"
echo "  4. ✅ 成功查询会话状态"
echo ""
echo "这些 API 调用与客户端 UI 操作对应："
echo "  - 登录 → 获取 Token"
echo "  - 车辆选择 → 获取 VIN 列表"
echo "  - 创建会话 → POST /api/v1/vins/{vin}/sessions"
echo "  - 显示会话信息 → GET /api/v1/sessions/{sessionId}"
echo ""
