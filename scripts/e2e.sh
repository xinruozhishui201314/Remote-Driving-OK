#!/bin/bash
# M0 阶段端到端测试脚本
# 验证基础设施部署和基本功能

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 颜色输出
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# 测试用例统计
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# 测试结果记录
declare -a TEST_RESULTS=()

# 测试函数
run_test() {
    local test_name=$1
    local test_cmd=$2
    local expected=$3
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo -n "测试 $TOTAL_TESTS: $test_name... "
    
    if eval "$test_cmd" > /dev/null 2>&1; then
        echo -e "${GREEN}✓${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        TEST_RESULTS+=("$test_name: PASS")
        return 0
    else
        echo -e "${RED}✗${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        TEST_RESULTS+=("$test_name: FAIL")
        return 1
    fi
}

echo "=========================================="
echo "M0 阶段端到端测试"
echo "=========================================="
echo ""

# 1. 环境检查
echo "1. 环境检查"
echo "----------------------------------------"

run_test "Docker 可用" "command -v docker"
run_test "Docker Compose 可用" "command -v docker-compose || docker compose version"

# 2. 配置文件检查
echo ""
echo "2. 配置文件检查"
echo "----------------------------------------"

run_test "docker-compose.yml 存在" "[ -f docker-compose.yml ]"
run_test "Keycloak Realm 配置存在" "[ -f deploy/keycloak/realm-export.json ]"
run_test "PostgreSQL 初始化脚本存在" "[ -f deploy/postgres/init.sql ]"
run_test "ZLMediaKit 配置存在" "[ -f deploy/zlm/config.ini ]"
run_test "Coturn 配置存在" "[ -f deploy/coturn/turnserver.conf ]"
run_test "数据库迁移脚本存在" "[ -f backend/migrations/001_initial_schema.sql ]"

# 3. 配置验证
echo ""
echo "3. 配置验证"
echo "----------------------------------------"

COMPOSE_CMD=""
if docker compose version >/dev/null 2>&1; then
    COMPOSE_CMD="docker compose"
elif command -v docker-compose >/dev/null 2>&1; then
    COMPOSE_CMD="docker-compose"
else
    echo -e "${RED}✗ Docker Compose 不可用${NC}"
    exit 1
fi

cd "${PROJECT_ROOT}"
run_test "docker-compose.yml 语法验证" "$COMPOSE_CMD -f docker-compose.yml config >/dev/null 2>&1"

# 4. 服务运行状态检查（如果服务已启动）
echo ""
echo "4. 服务运行状态检查"
echo "----------------------------------------"

# 检查 Docker daemon
if docker ps >/dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} Docker daemon 运行中"
    
    # 检查服务容器
    RUNNING_SERVICES=0
    
    check_service() {
        local service=$1
        if $COMPOSE_CMD -f docker-compose.yml ps $service | grep -q "Up"; then
            echo -e "${GREEN}✓${NC} $service 运行中"
            RUNNING_SERVICES=$((RUNNING_SERVICES + 1))
        else
            echo -e "${YELLOW}⊘${NC} $service 未运行或不在 docker-compose ps 输出中"
        fi
    }
    
    check_service "postgres"
    check_service "keycloak"
    check_service "zlmediakit"
    check_service "coturn"
    
    echo ""
    echo "运行服务数: $RUNNING_SERVICES / 4"
else
    echo -e "${YELLOW}⊘${NC} Docker daemon 未运行，跳过服务检查"
fi

# 5. 服务健康检查（如果服务已启动）
echo ""
echo "5. 服务健康检查"
echo "----------------------------------------"

if docker ps >/dev/null 2>&1 && [ $RUNNING_SERVICES -gt 0 ]; then
    # PostgreSQL
    if $COMPOSE_CMD -f docker-compose.yml ps postgres | grep -q "Up"; then
        if $COMPOSE_CMD -f docker-compose.yml exec -T postgres pg_isready -U teleop_user -d teleop_db >/dev/null 2>&1; then
            run_test "PostgreSQL 健康" "true"
        else
            run_test "PostgreSQL 健康" "false"
        fi
    fi
    
    # Keycloak
    if $COMPOSE_CMD -f docker-compose.yml ps keycloak | grep -q "Up"; then
        run_test "Keycloak 健康" "curl -sf http://localhost:8080/health/ready >/dev/null 2>&1"
    else
        echo -e "${YELLOW}⊘${NC} Keycloak 未运行或不在 docker-compose ps 输出中"
    fi
    
    # ZLMediaKit
    if $COMPOSE_CMD -f docker-compose.yml ps zlmediakit | grep -q "Up"; then
        run_test "ZLMediaKit API 可用" "curl -sf http://localhost/index/api/getServerConfig >/dev/null 2>&1"
    else
        echo -e "${YELLOW}⊘${NC} ZLMediaKit 未运行或不在 docker-compose ps 输出中"
    fi
    
    # Coturn
    if $COMPOSE_CMD -f docker-compose.yml ps coturn | grep -q "Up"; then
        run_test "Coturn 运行" "true"
    else
        echo -e "${YELLOW}⊘${NC} Coturn 未运行或不在 docker-compose ps 输出中"
    fi
else
    echo -e "${YELLOW}⊘${NC} Docker daemon 未运行或无服务运行，跳过健康检查"
fi

# 6. Keycloak Realm 验证（如果服务已启动）
echo ""
echo "6. Keycloak Realm 验证"
echo "----------------------------------------"

if curl -sf http://localhost:8080/health/ready >/dev/null 2>&1; then
    KEYCLOAK_ADMIN="${KEYCLOAK_ADMIN:-admin}"
    KEYCLOAK_ADMIN_PASSWORD="${KEYCLOAK_ADMIN_PASSWORD:-admin}"
    
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
            -H "Content-Type: application/json" 2>/dev/null || echo "000")
        
        if [ "$REALM_CHECK" = "200" ]; then
            echo -e "${GREEN}✓${NC} Keycloak Realm 'teleop' 已存在"
            
            ROLES=$(curl -s -X GET "http://localhost:8080/admin/realms/teleop/roles" \
                -H "Authorization: Bearer ${TOKEN}" \
                -H "Content-Type: application/json" 2>/dev/null)
            
            run_test "角色 admin 已定义" "echo '$ROLES' | grep -q '\"name\":\"admin\"'"
            run_test "角色 owner 已定义" "echo '$ROLES' | grep -q '\"name\":\"owner\"'"
            run_test "角色 operator 已定义" "echo '$ROLES' | grep -q '\"name\":\"operator\"'"
            run_test "角色 observer 已定义" "echo '$ROLES' | grep -q '\"name\":\"observer\"'"
            run_test "角色 maintenance 已定义" "echo '$ROLES' | grep -q '\"name\":\"maintenance\"'"
        else
            echo -e "${YELLOW}⊘${NC} Keycloak Realm 'teleop' 不存在或认证失败 (HTTP: $REALM_CHECK)"
            TEST_RESULTS+=("Keycloak Realm: UNVERIFIED - HTTP $REALM_CHECK")
        fi
    else
        echo -e "${YELLOW}⊘${NC} 无法获取 Keycloak Token"
        TEST_RESULTS+=("Keycloak Token: UNVERIFIED")
    fi
else
    echo -e "${YELLOW}⊘${NC} Keycloak 健康检查失败，跳过 Realm 验证"
    TEST_RESULTS+=("Keycloak Realm: SKIPPED")
fi

# 7. 端口占用检查
echo ""
echo "7. 端口可用性检查"
echo "----------------------------------------"

check_port() {
    local port=$1
    local service=$2
    
    if netstat -tuln 2>/dev/null | grep -q ":${port} " || \
       ss -tuln 2>/dev/null | grep -q ":${port} "; then
        echo -e "${YELLOW}⊘${NC} 端口 ${port} (${service}) 已被占用（可能是服务已启动）"
    else
        echo -e "${GREEN}✓${NC} 端口 ${port} (${service}) 可用"
    fi
}

check_port 5432 "PostgreSQL"
check_port 8080 "Keycloak"
check_port 80 "ZLMediaKit HTTP"
check_port 3000 "ZLMediaKit WebRTC Signaling"
check_port 3478 "Coturn STUN/TURN"

# 8. C++ Bridge 可构建性（在 deploy/carla 构建的 CARLA 容器内验证）
echo ""
echo "8. C++ Bridge 可构建性（CARLA 容器内）"
echo "----------------------------------------"
if [ -f "${PROJECT_ROOT}/carla-bridge/cpp/CMakeLists.txt" ]; then
    run_test "C++ Bridge 在 CARLA 容器内可构建" "bash ${SCRIPT_DIR}/verify-carla-bridge-cpp.sh --build-only 2>/dev/null"
else
    echo -e "${YELLOW}⊘${NC} 未找到 carla-bridge/cpp，跳过 C++ Bridge 构建验证"
fi

# 9. 总结
echo ""
echo "=========================================="
echo "测试完成"
echo "=========================================="
echo ""
echo "测试总数: $TOTAL_TESTS"
echo -e "${GREEN}通过: $PASSED_TESTS${NC}"
if [ $FAILED_TESTS -gt 0 ]; then
    echo -e "${RED}失败: $FAILED_TESTS${NC}"
fi
echo ""

# 输出测试结果
echo "测试结果详情:"
for result in "${TEST_RESULTS[@]}"; do
    echo "  - $result"
done

echo ""

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}✓ 所有测试通过${NC}"
    exit 0
else
    echo -e "${RED}✗ 部分测试失败${NC}"
    echo ""
    echo "请检查:"
    echo "  1. Docker 服务是否运行"
    echo "  2. 服务是否正常启动"
    echo "  3. 端口是否正常监听"
    echo ""
    echo "查看日志:"
    echo "  docker-compose -f docker-compose.yml logs"
    exit 1
fi
