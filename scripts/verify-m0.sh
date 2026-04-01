#!/bin/bash
# M0 阶段完整验证脚本
# 验证所有配置文件和服务的正确性

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPLOY_DIR="${PROJECT_ROOT}/deploy"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 验证结果
PASSED=0
FAILED=0
WARNINGS=0
SKIPPED=0

echo "=========================================="
echo "M0 阶段完整验证报告"
echo "=========================================="
echo "项目路径: ${PROJECT_ROOT}"
echo "部署路径: ${DEPLOY_DIR}"
echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo ""

# 检查函数
check_pass() {
    local msg=$1
    echo -e "${GREEN}✓${NC} $msg"
    ((PASSED++))
}

check_fail() {
    local msg=$1
    echo -e "${RED}✗${NC} $msg"
    ((FAILED++))
}

check_warn() {
    local msg=$1
    echo -e "${YELLOW}⚠${NC} $msg"
    ((WARNINGS++))
}

check_skip() {
    local msg=$1
    echo -e "${BLUE}⊘${NC} $msg (跳过)"
    ((SKIPPED++))
}

# 1. 环境检查
echo "1. 环境检查"
echo "----------------------------------------"

if command -v docker >/dev/null 2>&1; then
    DOCKER_VERSION=$(docker --version)
    check_pass "Docker 已安装: ${DOCKER_VERSION}"
else
    check_fail "Docker 未安装"
    exit 1
fi

# 检查 docker-compose 或 docker compose
DOCKER_COMPOSE_CMD=""
if command -v docker-compose >/dev/null 2>&1; then
    DOCKER_COMPOSE_CMD="docker-compose"
    check_pass "docker-compose 已安装: $(docker-compose version)"
elif docker compose version >/dev/null 2>&1; then
    DOCKER_COMPOSE_CMD="docker compose"
    check_pass "docker compose 已安装: $(docker compose version)"
else
    check_fail "docker-compose 或 docker compose 未安装"
    check_warn "可以手动安装: pip install docker-compose 或使用 Docker Desktop"
    DOCKER_COMPOSE_CMD="docker-compose"  # 继续验证其他项
fi

# 2. 配置文件检查
echo ""
echo "2. 配置文件检查"
echo "----------------------------------------"

check_file() {
    local file=$1
    local desc=$2
    if [ -f "$file" ]; then
        check_pass "$desc: $file"
        return 0
    else
        check_fail "$desc 不存在: $file"
        return 1
    fi
}

check_file "${PROJECT_ROOT}/docker-compose.yml" "Docker Compose 主文件"
check_file "${DEPLOY_DIR}/.env.example" "环境变量示例文件"
check_file "${DEPLOY_DIR}/keycloak/realm-export.json" "Keycloak Realm 配置"
check_file "${DEPLOY_DIR}/keycloak/import-realm.sh" "Keycloak 导入脚本"
check_file "${DEPLOY_DIR}/postgres/init.sql" "PostgreSQL 初始化脚本"
check_file "${DEPLOY_DIR}/zlm/config.ini" "ZLMediaKit 配置"
check_file "${DEPLOY_DIR}/coturn/turnserver.conf" "Coturn 配置"
check_file "${PROJECT_ROOT}/backend/Dockerfile" "后端 Dockerfile"
check_file "${PROJECT_ROOT}/backend/CMakeLists.txt" "后端 CMakeLists.txt"
check_file "${PROJECT_ROOT}/backend/migrations/001_initial_schema.sql" "数据库迁移脚本"
check_file "${PROJECT_ROOT}/scripts/check.sh" "检查脚本"
check_file "${PROJECT_ROOT}/scripts/setup.sh" "部署脚本"

# 3. 配置文件内容验证
echo ""
echo "3. 配置文件内容验证"
echo "----------------------------------------"

# 验证 docker-compose.yml 语法
if [ -n "$DOCKER_COMPOSE_CMD" ]; then
    cd "${DEPLOY_DIR}"
    if $DOCKER_COMPOSE_CMD config >/dev/null 2>&1; then
        check_pass "docker-compose.yml 语法正确"
    else
        check_fail "docker-compose.yml 语法错误"
        $DOCKER_COMPOSE_CMD config 2>&1 | head -20
    fi
    cd "${PROJECT_ROOT}"
else
    check_skip "docker-compose.yml 语法检查（docker-compose 不可用）"
fi

# 验证 Keycloak Realm JSON
if command -v jq >/dev/null 2>&1; then
    if jq empty "${DEPLOY_DIR}/keycloak/realm-export.json" 2>/dev/null; then
        check_pass "Keycloak Realm JSON 格式正确"
        
        # 检查角色
        ROLES=$(jq -r '.roles.realm[].name' "${DEPLOY_DIR}/keycloak/realm-export.json" 2>/dev/null | sort)
        EXPECTED_ROLES="admin maintenance observer operator owner"
        for role in $EXPECTED_ROLES; do
            if echo "$ROLES" | grep -q "^${role}$"; then
                check_pass "角色 '$role' 已定义"
            else
                check_fail "角色 '$role' 未定义"
            fi
        done
    else
        check_fail "Keycloak Realm JSON 格式错误"
    fi
else
    check_warn "jq 未安装，跳过 JSON 验证"
fi

# 验证 SQL 语法（基本检查）
if grep -q "CREATE TABLE" "${PROJECT_ROOT}/backend/migrations/001_initial_schema.sql" 2>/dev/null; then
    TABLE_COUNT=$(grep -c "CREATE TABLE" "${PROJECT_ROOT}/backend/migrations/001_initial_schema.sql" 2>/dev/null || echo "0")
    if [ "$TABLE_COUNT" -ge 5 ]; then
        check_pass "数据库迁移脚本包含至少 5 张表（实际: $TABLE_COUNT）"
    else
        check_warn "数据库迁移脚本表数量较少: $TABLE_COUNT"
    fi
else
    check_fail "数据库迁移脚本格式异常"
fi

# 4. 目录结构检查
echo ""
echo "4. 目录结构检查"
echo "----------------------------------------"

check_dir() {
    local dir=$1
    local desc=$2
    if [ -d "$dir" ]; then
        check_pass "$desc: $dir"
    else
        check_fail "$desc 不存在: $dir"
    fi
}

check_dir "${PROJECT_ROOT}/backend" "后端目录"
check_dir "${PROJECT_ROOT}/backend/include" "后端头文件目录"
check_dir "${PROJECT_ROOT}/backend/src" "后端源代码目录"
check_dir "${PROJECT_ROOT}/backend/migrations" "数据库迁移目录"
check_dir "${PROJECT_ROOT}/deploy/keycloak" "Keycloak 配置目录"
check_dir "${PROJECT_ROOT}/deploy/postgres" "PostgreSQL 配置目录"
check_dir "${PROJECT_ROOT}/deploy/zlm" "ZLMediaKit 配置目录"
check_dir "${PROJECT_ROOT}/deploy/coturn" "Coturn 配置目录"
check_dir "${PROJECT_ROOT}/scripts" "脚本目录"
check_dir "${PROJECT_ROOT}/docs" "文档目录"

# 5. 端口占用检查（如果服务未运行）
echo ""
echo "5. 端口占用检查"
echo "----------------------------------------"

check_port() {
    local port=$1
    local service=$2
    
    if netstat -tuln 2>/dev/null | grep -q ":${port} " || \
       ss -tuln 2>/dev/null | grep -q ":${port} "; then
        check_warn "端口 ${port} (${service}) 已被占用"
    else
        check_pass "端口 ${port} (${service}) 可用"
    fi
}

check_port 5432 "PostgreSQL"
check_port 8080 "Keycloak"
check_port 80 "ZLMediaKit HTTP"
check_port 3000 "ZLMediaKit WebRTC Signaling"
check_port 3478 "Coturn STUN/TURN"

# 6. 服务运行状态检查（如果已启动）
echo ""
echo "6. 服务运行状态检查"
echo "----------------------------------------"

if [ -n "$DOCKER_COMPOSE_CMD" ]; then
    cd "${DEPLOY_DIR}"
    if $DOCKER_COMPOSE_CMD ps 2>/dev/null | grep -q "Up"; then
        check_pass "部分服务正在运行"
        
        # 检查各个服务
        for service in postgres keycloak zlmediakit coturn; do
            if $DOCKER_COMPOSE_CMD ps 2>/dev/null | grep -q "${service}.*Up"; then
                check_pass "服务 ${service} 正在运行"
            else
                check_warn "服务 ${service} 未运行"
            fi
        done
    else
        check_warn "服务未运行（这是正常的，如果还未启动）"
    fi
    cd "${PROJECT_ROOT}"
else
    check_skip "服务状态检查（docker-compose 不可用）"
fi

# 7. 文档完整性检查
echo ""
echo "7. 文档完整性检查"
echo "----------------------------------------"

check_file "${PROJECT_ROOT}/README.md" "项目 README"
check_file "${PROJECT_ROOT}/project_spec.md" "项目规格说明"
check_file "${PROJECT_ROOT}/M0_SUMMARY.md" "M0 阶段总结"
check_file "${PROJECT_ROOT}/M0_COMPLETE.md" "M0 完成报告"
check_file "${PROJECT_ROOT}/PROJECT_STRUCTURE.md" "项目结构文档"
check_file "${DEPLOY_DIR}/README.md" "部署说明"

# 总结
echo ""
echo "=========================================="
echo "验证完成"
echo "=========================================="
echo -e "${GREEN}通过: ${PASSED}${NC}"
if [ $FAILED -gt 0 ]; then
    echo -e "${RED}失败: ${FAILED}${NC}"
fi
if [ $WARNINGS -gt 0 ]; then
    echo -e "${YELLOW}警告: ${WARNINGS}${NC}"
fi
if [ $SKIPPED -gt 0 ]; then
    echo -e "${BLUE}跳过: ${SKIPPED}${NC}"
fi
echo ""

# 生成报告
REPORT_FILE="${PROJECT_ROOT}/M0_VERIFICATION_REPORT.md"
cat > "$REPORT_FILE" <<EOF
# M0 阶段验证报告

**生成时间**: $(date '+%Y-%m-%d %H:%M:%S')
**项目路径**: ${PROJECT_ROOT}

## 验证结果

- ✅ **通过**: ${PASSED}
- ❌ **失败**: ${FAILED}
- ⚠️ **警告**: ${WARNINGS}
- ⊘ **跳过**: ${SKIPPED}

## 环境信息

- Docker: $(docker --version 2>/dev/null || echo "未安装")
- Docker Compose: $(${DOCKER_COMPOSE_CMD:-"未安装"} version 2>/dev/null || echo "未安装")

## 下一步

1. 如果所有检查通过，可以运行部署脚本：
   \`\`\`bash
   cd scripts && ./setup.sh
   \`\`\`

2. 启动服务后，运行检查脚本：
   \`\`\`bash
   cd scripts && ./check.sh
   \`\`\`

3. 查看详细部署文档：
   - \`deploy/README.md\`
   - \`docs/M0_DEPLOYMENT.md\`
EOF

echo "详细报告已保存到: ${REPORT_FILE}"

if [ $FAILED -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✓ M0 阶段配置验证通过！${NC}"
    echo ""
    echo "下一步："
    echo "  1. 启动服务: cd deploy && docker-compose up -d"
    echo "  2. 运行检查: cd scripts && ./check.sh"
    exit 0
else
    echo ""
    echo -e "${RED}✗ 部分验证失败，请检查上述错误${NC}"
    exit 1
fi
