#!/bin/bash
# Backend 开发环境功能验证脚本
# 验证容器内编译和运行的所有功能

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

PASSED=0
FAILED=0

# 测试函数
test_check() {
    local name="$1"
    local command="$2"
    local expected_status="${3:-200}"
    
    echo -n "测试: $name... "
    if result=$(eval "$command" 2>&1); then
        http_code=$(echo "$result" | tail -n 1)
        body=$(echo "$result" | head -n -1)
        
        if [ "$http_code" == "$expected_status" ]; then
            echo -e "${GREEN}✓ 通过${NC}"
            PASSED=$((PASSED + 1))
            return 0
        else
            echo -e "${RED}✗ 失败 (期望 HTTP $expected_status，实际 $http_code)${NC}"
            echo "  响应: $body"
            FAILED=$((FAILED + 1))
            return 1
        fi
    else
        echo -e "${RED}✗ 失败${NC}"
        echo "  错误: $result"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Backend 开发环境功能验证${NC}"
echo -e "${BLUE}========================================${NC}"
echo "Backend URL: $BACKEND_URL"
echo "Keycloak URL: $KEYCLOAK_URL"
echo ""

# 1. 检查容器状态
echo -e "${YELLOW}[1/8] 检查容器状态${NC}"
if docker compose -f docker-compose.yml -f docker-compose.dev.yml ps backend | grep -q "Up"; then
    echo -e "${GREEN}✓ Backend 容器运行中${NC}"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗ Backend 容器未运行${NC}"
    FAILED=$((FAILED + 1))
    exit 1
fi

# 2. 检查 Backend 进程
echo -e "${YELLOW}[2/8] 检查 Backend 进程${NC}"
if docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend sh -c "ps aux | grep teleop_backend | grep -v grep" >/dev/null 2>&1; then
    PID=$(docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend sh -c "ps aux | grep teleop_backend | grep -v grep" | awk '{print $2}')
    echo -e "${GREEN}✓ Backend 进程运行中 (PID: $PID)${NC}"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗ Backend 进程未运行${NC}"
    FAILED=$((FAILED + 1))
    exit 1
fi

# 3. 检查文件监控
echo -e "${YELLOW}[3/8] 检查文件监控${NC}"
if docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend sh -c "ps aux | grep inotifywait | grep -v grep" >/dev/null 2>&1; then
    echo -e "${GREEN}✓ 文件监控运行中${NC}"
    PASSED=$((PASSED + 1))
else
    echo -e "${YELLOW}⚠ 文件监控未运行（可能已编译完成）${NC}"
fi

# 4. 检查本地依赖项
echo -e "${YELLOW}[4/8] 检查本地依赖项${NC}"
if docker compose -f docker-compose.yml -f docker-compose.dev.yml exec backend sh -c "test -f /app/deps/cpp-httplib/httplib.h && test -f /app/deps/nlohmann_json/include/nlohmann/json.hpp" >/dev/null 2>&1; then
    echo -e "${GREEN}✓ 本地依赖项已挂载${NC}"
    PASSED=$((PASSED + 1))
else
    echo -e "${YELLOW}⚠ 本地依赖项未找到（将使用远程下载）${NC}"
fi

# 5. 健康检查
echo -e "${YELLOW}[5/8] 健康检查${NC}"
test_check "GET /health" "curl -s -w '\n%{http_code}' $BACKEND_URL/health"

# 6. 数据库就绪检查
echo -e "${YELLOW}[6/8] 数据库就绪检查${NC}"
test_check "GET /ready" "curl -s -w '\n%{http_code}' $BACKEND_URL/ready"

# 7. 获取 Token
echo -e "${YELLOW}[7/8] 获取 Keycloak Token${NC}"
TOKEN_RESP=$(curl -s -X POST "$KEYCLOAK_URL/realms/teleop/protocol/openid-connect/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password" \
  -d "client_id=teleop-client" \
  -d "username=e2e-test" \
  -d "password=e2e-test-password")

ACCESS_TOKEN=$(echo "$TOKEN_RESP" | python3 -c "import sys, json; print(json.load(sys.stdin).get('access_token', ''))" 2>/dev/null || echo "")

if [ -z "$ACCESS_TOKEN" ]; then
    echo -e "${RED}✗ 获取 Token 失败${NC}"
    echo "  响应: $TOKEN_RESP"
    FAILED=$((FAILED + 1))
    exit 1
else
    echo -e "${GREEN}✓ Token 获取成功${NC}"
    PASSED=$((PASSED + 1))
fi

# 8. 测试 API 端点
echo ""
echo -e "${YELLOW}[8/8] 测试 API 端点${NC}"

# 8.1 GET /api/v1/me
test_check "GET /api/v1/me" "curl -s -w '\n%{http_code}' -H 'Authorization: Bearer $ACCESS_TOKEN' $BACKEND_URL/api/v1/me"

# 8.2 GET /api/v1/vins
test_check "GET /api/v1/vins" "curl -s -w '\n%{http_code}' -H 'Authorization: Bearer $ACCESS_TOKEN' $BACKEND_URL/api/v1/vins"

# 8.3 POST /api/v1/vins/{vin}/sessions
VIN=$(curl -s -H "Authorization: Bearer $ACCESS_TOKEN" "$BACKEND_URL/api/v1/vins" | python3 -c "import sys, json; vins = json.load(sys.stdin).get('vins', []); print(vins[0] if vins else '')" 2>/dev/null || echo "")
if [ -z "$VIN" ]; then
    echo -e "${RED}✗ 无法获取 VIN${NC}"
    FAILED=$((FAILED + 1))
else
    echo "  使用 VIN: $VIN"
    SESSION_RESP=$(curl -s -w "\n%{http_code}" \
      -X POST \
      -H "Authorization: Bearer $ACCESS_TOKEN" \
      -H "Content-Type: application/json" \
      "$BACKEND_URL/api/v1/vins/$VIN/sessions")
    
    HTTP_CODE=$(echo "$SESSION_RESP" | tail -n 1)
    SESSION_BODY=$(echo "$SESSION_RESP" | head -n -1)
    
    if [ "$HTTP_CODE" == "201" ]; then
        SESSION_ID=$(echo "$SESSION_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('sessionId', ''))" 2>/dev/null || echo "")
        WHIP_URL=$(echo "$SESSION_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('media', {}).get('whip', ''))" 2>/dev/null || echo "")
        WHEP_URL=$(echo "$SESSION_BODY" | python3 -c "import sys, json; print(json.load(sys.stdin).get('media', {}).get('whep', ''))" 2>/dev/null || echo "")
        
        if [ -n "$SESSION_ID" ]; then
            echo -e "${GREEN}✓ POST /api/v1/vins/$VIN/sessions 成功${NC}"
            echo "  Session ID: $SESSION_ID"
            echo "  WHIP URL: $WHIP_URL"
            echo "  WHEP URL: $WHEP_URL"
            PASSED=$((PASSED + 1))
            
            # 8.4 GET /api/v1/sessions/{sessionId}
            test_check "GET /api/v1/sessions/$SESSION_ID" "curl -s -w '\n%{http_code}' -H 'Authorization: Bearer $ACCESS_TOKEN' $BACKEND_URL/api/v1/sessions/$SESSION_ID"
        else
            echo -e "${RED}✗ 响应中缺少 sessionId${NC}"
            FAILED=$((FAILED + 1))
        fi
    else
        echo -e "${RED}✗ POST /api/v1/vins/$VIN/sessions 失败 (HTTP $HTTP_CODE)${NC}"
        echo "  响应: $SESSION_BODY"
        FAILED=$((FAILED + 1))
    fi
fi

# 总结
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}验证结果${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "通过: ${GREEN}$PASSED${NC}"
echo -e "失败: ${RED}$FAILED${NC}"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}✅ 所有测试通过！${NC}"
    exit 0
else
    echo -e "${RED}❌ 部分测试失败${NC}"
    exit 1
fi
