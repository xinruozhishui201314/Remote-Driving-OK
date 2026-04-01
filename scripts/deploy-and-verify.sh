#!/bin/bash
# M0 阶段部署和验证脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPLOY_DIR="${PROJECT_ROOT}/deploy"

# 添加 docker-compose 到 PATH
export PATH="$HOME/.local/bin:$PATH"

# 颜色输出
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=========================================="
echo "M0 阶段部署和验证"
echo "=========================================="
echo ""

# 检查 docker-compose
if ! command -v docker-compose >/dev/null 2>&1; then
    echo -e "${RED}✗ docker-compose 未找到${NC}"
    echo "请先运行: pip3 install --user docker-compose"
    echo "然后添加 PATH: export PATH=\"\$HOME/.local/bin:\$PATH\""
    exit 1
fi

echo -e "${GREEN}✓${NC} docker-compose 已安装: $(docker-compose --version)"
echo ""

# 进入项目根目录（docker-compose.yml 在根目录）
cd "${PROJECT_ROOT}"

# 1. 检查配置文件
echo "1. 检查配置文件..."
if docker-compose -f docker-compose.yml config >/dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} docker-compose.yml 配置正确"
else
    echo -e "${RED}✗${NC} docker-compose.yml 配置错误"
    docker-compose config
    exit 1
fi

# 2. 检查 .env 文件
if [ ! -f .env ]; then
    echo "创建 .env 文件..."
    cp .env.example .env
    echo -e "${YELLOW}⚠${NC} 已创建 .env 文件，请根据需要修改密码"
fi

# 3. 启动服务
echo ""
echo "2. 启动服务..."
docker-compose -f docker-compose.yml up -d

echo ""
echo "等待服务启动..."
sleep 10

# 4. 检查服务状态
echo ""
echo "3. 检查服务状态..."
docker-compose -f docker-compose.yml ps

# 5. 等待服务就绪
echo ""
echo "4. 等待服务就绪..."
MAX_WAIT=120
WAITED=0

check_service() {
    local service=$1
    local check_cmd=$2
    
    while [ $WAITED -lt $MAX_WAIT ]; do
        if eval "$check_cmd" >/dev/null 2>&1; then
            return 0
        fi
        sleep 2
        WAITED=$((WAITED + 2))
        echo -n "."
    done
    return 1
}

echo -n "等待 PostgreSQL"
if check_service "postgres" "docker-compose -f docker-compose.yml exec -T postgres pg_isready -U teleop_user -d teleop_db"; then
    echo -e " ${GREEN}✓${NC}"
else
    echo -e " ${RED}✗${NC} 超时"
fi

echo -n "等待 Keycloak"
if check_service "keycloak" "curl -sf http://localhost:8080/health/ready"; then
    echo -e " ${GREEN}✓${NC}"
else
    echo -e " ${YELLOW}⚠${NC} 可能还在启动中"
fi

echo -n "等待 ZLMediaKit"
if check_service "zlmediakit" "curl -sf http://localhost/index/api/getServerConfig"; then
    echo -e " ${GREEN}✓${NC}"
else
    echo -e " ${YELLOW}⚠${NC} 可能还在启动中"
fi

# 6. 验证服务
echo ""
echo "5. 验证服务..."
echo "----------------------------------------"

# PostgreSQL
if docker-compose -f docker-compose.yml exec -T postgres pg_isready -U teleop_user -d teleop_db >/dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} PostgreSQL 健康"
else
    echo -e "${RED}✗${NC} PostgreSQL 未就绪"
fi

# Keycloak
if curl -sf http://localhost:8080/health/ready >/dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} Keycloak 健康"
    echo "  Admin Console: http://localhost:8080/admin"
    echo "  默认账号: admin / admin"
else
    echo -e "${YELLOW}⚠${NC} Keycloak 可能还在启动中"
fi

# ZLMediaKit
if curl -sf http://localhost/index/api/getServerConfig >/dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} ZLMediaKit API 可用"
    echo "  API: http://localhost/index/api/getServerConfig"
else
    echo -e "${YELLOW}⚠${NC} ZLMediaKit 可能还在启动中"
fi

# Coturn
if docker-compose -f docker-compose.yml ps coturn | grep -q "Up"; then
    echo -e "${GREEN}✓${NC} Coturn 运行中"
else
    echo -e "${YELLOW}⚠${NC} Coturn 未运行"
fi

# 7. 检查 Keycloak Realm
echo ""
echo "6. 检查 Keycloak Realm..."
echo "----------------------------------------"

KEYCLOAK_ADMIN="${KEYCLOAK_ADMIN:-admin}"
KEYCLOAK_ADMIN_PASSWORD="${KEYCLOAK_ADMIN_PASSWORD:-admin}"

# 等待 Keycloak 完全启动
sleep 5

TOKEN=$(curl -s -X POST "http://localhost:8080/realms/master/protocol/openid-connect/token" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "username=${KEYCLOAK_ADMIN}" \
    -d "password=${KEYCLOAK_ADMIN_PASSWORD}" \
    -d "grant_type=password" \
    -d "client_id=admin-cli" 2>/dev/null | grep -o '"access_token":"[^"]*' | cut -d'"' -f4)

if [ -n "$TOKEN" ]; then
    REALM_CHECK=$(curl -s -w "%{http_code}" -o /dev/null \
        -X GET "http://localhost:8080/admin/realms/teleop" \
        -H "Authorization: Bearer ${TOKEN}" \
        -H "Content-Type: application/json")
    
    if [ "$REALM_CHECK" = "200" ]; then
        echo -e "${GREEN}✓${NC} Keycloak Realm 'teleop' 已存在"
        
        # 检查角色
        ROLES=$(curl -s -X GET "http://localhost:8080/admin/realms/teleop/roles" \
            -H "Authorization: Bearer ${TOKEN}" \
            -H "Content-Type: application/json" 2>/dev/null)
        
        for role in admin owner operator observer maintenance; do
            if echo "$ROLES" | grep -q "\"name\":\"${role}\""; then
                echo -e "  ${GREEN}✓${NC} 角色 '${role}' 已定义"
            else
                echo -e "  ${RED}✗${NC} 角色 '${role}' 未找到"
            fi
        done
    else
        echo -e "${YELLOW}⚠${NC} Keycloak Realm 'teleop' 不存在（可能需要导入）"
        echo "  运行: cd deploy/keycloak && ./import-realm.sh"
    fi
else
    echo -e "${YELLOW}⚠${NC} 无法获取 Keycloak Token（服务可能还在启动中）"
fi

# 8. 查看日志（最后几行）
echo ""
echo "7. 服务日志摘要..."
echo "----------------------------------------"
echo "PostgreSQL:"
docker-compose -f docker-compose.yml logs --tail=3 postgres 2>/dev/null | tail -2 || echo "  无日志"
echo ""
echo "Keycloak:"
docker-compose -f docker-compose.yml logs --tail=3 keycloak 2>/dev/null | tail -2 || echo "  无日志"
echo ""
echo "ZLMediaKit:"
docker-compose -f docker-compose.yml logs --tail=3 zlmediakit 2>/dev/null | tail -2 || echo "  无日志"

# 总结
echo ""
echo "=========================================="
echo "部署完成"
echo "=========================================="
echo ""
echo "服务访问地址:"
echo "  - Keycloak Admin: http://localhost:8080/admin"
echo "  - ZLMediaKit API: http://localhost/index/api/getServerConfig"
echo ""
echo "查看日志:"
echo "  docker-compose logs -f [service_name]"
echo ""
echo "停止服务:"
echo "  docker-compose down"
echo ""
echo "运行完整检查:"
echo "  cd scripts && ./check.sh"
echo "=========================================="
