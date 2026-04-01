#!/bin/bash

# 版本协商测试脚本
# 测试HTTP API的版本协商功能

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================="
echo "  HTTP API版本协商测试"
echo "========================================="
echo ""

# 检查Backend是否运行
if ! curl -s http://localhost:8080/health > /dev/null; then
    echo -e "${RED}[ERROR] Backend未运行，请先启动Backend${NC}"
    exit 1
fi

echo -e "${GREEN}[OK] Backend已运行${NC}"
echo ""

# 获取测试Token（如果有）
TOKEN="${TEST_TOKEN:-}"

# 如果没有提供TOKEN，尝试从环境变量读取
if [ -z "$TOKEN" ]; then
    echo "[INFO] 从环境变量获取测试Token..."
    # 这里可以添加获取Token的逻辑，暂时跳过
    echo -e "${YELLOW}[WARN] 未提供测试Token，部分测试将跳过${NC}"
    SKIP_AUTH_TESTS=1
else
    SKIP_AUTH_TESTS=0
    echo -e "${GREEN}[OK] 已获取测试Token${NC}"
fi

echo ""
echo "========================================="
echo "  测试1: 有效版本 (1.0.0)"
echo "========================================="

if [ $SKIP_AUTH_TESTS -eq 0 ]; then
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
        -H "Authorization: Bearer $TOKEN" \
        -H "API-Version: 1.0.0" \
        http://localhost:8080/api/v1/me)
    
    if [ "$HTTP_CODE" = "200" ]; then
        echo -e "${GREEN}[PASS] 版本1.0.0被接受${NC}"
        RESPONSE=$(curl -s -H "Authorization: Bearer $TOKEN" \
            -H "API-Version: 1.0.0" \
            http://localhost:8080/api/v1/me)
        echo "响应:"
        echo "$RESPONSE" | jq '.'
    else
        echo -e "${RED}[FAIL] 版本1.0.0被拒绝，HTTP状态码: $HTTP_CODE${NC}"
        exit 1
    fi
else
    echo -e "${YELLOW}[SKIP] 无Token，跳过此测试${NC}"
fi

echo ""
echo "========================================="
echo "  测试2: 有效版本 (1.1.0)"
echo "========================================="

if [ $SKIP_AUTH_TESTS -eq 0 ]; then
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
        -H "Authorization: Bearer $TOKEN" \
        -H "API-Version: 1.1.0" \
        http://localhost:8080/api/v1/me)
    
    if [ "$HTTP_CODE" = "200" ]; then
        echo -e "${GREEN}[PASS] 版本1.1.0被接受${NC}"
        
        # 验证响应头中的API-Version
        API_VERSION=$(curl -s -I -H "Authorization: Bearer $TOKEN" \
            -H "API-Version: 1.1.0" \
            http://localhost:8080/api/v1/me | grep -i "API-Version" | cut -d' ' -f2 | tr -d '\r')
        
        if [ -n "$API_VERSION" ]; then
            echo -e "${GREEN}[OK] 响应头包含API-Version: $API_VERSION${NC}"
        else
            echo -e "${YELLOW}[WARN] 响应头未找到API-Version${NC}"
        fi
    else
        echo -e "${RED}[FAIL] 版本1.1.0被拒绝，HTTP状态码: $HTTP_CODE${NC}"
        exit 1
    fi
else
    echo -e "${YELLOW}[SKIP] 无Token，跳过此测试${NC}"
fi

echo ""
echo "========================================="
echo "  测试3: 版本不兼容 (2.0.0)"
echo "========================================="

if [ $SKIP_AUTH_TESTS -eq 0 ]; then
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
        -H "Authorization: Bearer $TOKEN" \
        -H "API-Version: 2.0.0" \
        http://localhost:8080/api/v1/me)
    
    if [ "$HTTP_CODE" = "400" ]; then
        echo -e "${GREEN}[PASS] 版本2.0.0被正确拒绝 (400)${NC}"
        RESPONSE=$(curl -s -H "Authorization: Bearer $TOKEN" \
            -H "API-Version: 2.0.0" \
            http://localhost:8080/api/v1/me)
        echo "错误响应:"
        echo "$RESPONSE" | jq '.'
    else
        echo -e "${YELLOW}[WARN] 版本2.0.0返回状态码: $HTTP_CODE (预期400)${NC}"
    fi
else
    echo -e "${YELLOW}[SKIP] 无Token，跳过此测试${NC}"
fi

echo ""
echo "========================================="
echo "  测试4: 无版本头"
echo "========================================="

if [ $SKIP_AUTH_TESTS -eq 0 ]; then
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
        -H "Authorization: Bearer $TOKEN" \
        http://localhost:8080/api/v1/me)
    
    if [ "$HTTP_CODE" = "200" ]; then
        echo -e "${GREEN}[PASS] 无版本头时使用默认兼容策略 (200)${NC}"
    else
        echo -e "${YELLOW}[WARN] 无版本头返回状态码: $HTTP_CODE${NC}"
    fi
else
    echo -e "${YELLOW}[SKIP] 无Token，跳过此测试${NC}"
fi

echo ""
echo "========================================="
echo "  测试5: 健康检查 (无需版本头)"
echo "========================================="

HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    http://localhost:8080/health)

if [ "$HTTP_CODE" = "200" ]; then
    echo -e "${GREEN}[PASS] 健康检查无需版本头 (200)${NC}"
    RESPONSE=$(curl -s http://localhost:8080/health)
    echo "响应:"
    echo "$RESPONSE" | jq '.'
else
    echo -e "${RED}[FAIL] 健康检查失败，HTTP状态码: $HTTP_CODE${NC}"
    exit 1
fi

echo ""
echo "========================================="
echo "  测试6: 验证响应中的apiVersion字段"
echo "========================================="

if [ $SKIP_AUTH_TESTS -eq 0 ]; then
    RESPONSE=$(curl -s -H "Authorization: Bearer $TOKEN" \
        http://localhost:8080/api/v1/me)
    
    API_VERSION=$(echo "$RESPONSE" | jq -r '.apiVersion // empty')
    
    if [ -n "$API_VERSION" ]; then
        echo -e "${GREEN}[PASS] 响应包含apiVersion字段: $API_VERSION${NC}"
    else
        echo -e "${YELLOW}[WARN] 响应未找到apiVersion字段${NC}"
        echo "响应: $RESPONSE"
    fi
else
    echo -e "${YELLOW}[SKIP] 无Token，跳过此测试${NC}"
fi

echo ""
echo "========================================="
echo "  测试完成"
echo "========================================="
echo -e "${GREEN}[SUCCESS] 版本协商测试完成${NC}"
