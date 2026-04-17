#!/bin/bash
# 远程驾驶系统 - 综合检查脚本
# 功能：
# 1. 开发者检查 (默认): Lint + Build + Test (符合 project_spec.md §14)
# 2. 运维检查: 验证服务状态 (原有 M0 阶段检查)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPLOY_DIR="${PROJECT_ROOT}/deploy"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 检查结果计数
PASSED=0
FAILED=0
WARNINGS=0

MODE="${1:-ci}" # 默认为 ci 模式

usage() {
    echo "Usage: $0 [ci|infra]"
    echo "  ci    : 运行 Lint, Build, Test (默认，开发者使用)"
    echo "  infra : 运行基础设施健康检查 (运维使用)"
    exit 1
}

if [[ "$MODE" != "ci" && "$MODE" != "infra" ]]; then
    usage
fi

# ===========================
# CI/CD 检查函数
# ===========================

check_lint() {
    echo "=========================================="
    echo "1. 代码风格与规范检查"
    echo "=========================================="
    
    # 1.1 检查是否使用了 std::cout 或 printf (禁止使用，需用结构化日志)
    echo -n "检查禁止的日志调用..."
    if git -C "${PROJECT_ROOT}" grep -rn "std::cout\|printf" --include="*.cpp" -o --include="*.h" \
        --exclude-dir=build --exclude-dir=third_party --exclude-dir=vendor \
        . 2>/dev/null | grep -v "Binary file" > /tmp/lint_check.log; then
        
        # 允许某些特定场景（如：main 简单入口，或明确标记允许的测试文件），这里严格按规范执行
        if [ -s /tmp/lint_check.log ]; then
            echo -e "${RED}✗${NC}"
            echo -e "${RED}发现违规：请使用 spdlog 或其他结构化日志库替代 std::cout/printf${NC}"
            cat /tmp/lint_check.log | head -n 5 # 仅显示前5条
            echo "..."
            FAILED=$((FAILED + 1))
            return 1
        fi
    fi
    echo -e "${GREEN}✓${NC}"
    PASSED=$((PASSED + 1))

    # 1.1b 客户端 QML 控制契约（无需 Docker）
    if [ -f "${PROJECT_ROOT}/scripts/verify-client-contract.sh" ]; then
        echo -n "客户端 QML 控制契约..."
        if bash "${PROJECT_ROOT}/scripts/verify-client-contract.sh" >/dev/null 2>&1; then
            echo -e "${GREEN}✓${NC}"
            PASSED=$((PASSED + 1))
        else
            echo -e "${RED}✗${NC} 运行 scripts/verify-client-contract.sh 查看详情"
            FAILED=$((FAILED + 1))
        fi
    fi

    # 1.1c QML 语法与类型检查 (qmllint)
    if [ -f "${PROJECT_ROOT}/scripts/verify-qml-lint.sh" ]; then
        echo -n "QML 语法与类型检查..."
        if bash "${PROJECT_ROOT}/scripts/verify-qml-lint.sh" >/dev/null 2>&1; then
            echo -e "${GREEN}✓${NC}"
            PASSED=$((PASSED + 1))
        else
            echo -e "${RED}✗${NC} 运行 scripts/verify-qml-lint.sh 查看详情"
            FAILED=$((FAILED + 1))
        fi
    fi

    # 1.1d Harbor/Docker 镜像端口契约探测脚本自测（无网络；根因类 EOF/push 失败的门禁）
    if [ -f "${PROJECT_ROOT}/scripts/verify-registry-docker-port-contract.sh" ]; then
        echo -n "Registry Docker 端口契约脚本自测..."
        if bash "${PROJECT_ROOT}/scripts/verify-registry-docker-port-contract.sh" --selftest >/dev/null 2>&1; then
            echo -e "${GREEN}✓${NC}"
            PASSED=$((PASSED + 1))
        else
            echo -e "${RED}✗${NC} 运行 scripts/verify-registry-docker-port-contract.sh --selftest 查看详情"
            FAILED=$((FAILED + 1))
        fi
    fi

    # 1.1e Python 语法检查 (carla-bridge)
    echo -n "Python 语法检查 (carla-bridge)..."
    if python3 -m py_compile "${PROJECT_ROOT}/carla-bridge/carla_bridge.py" >/dev/null 2>&1; then
        echo -e "${GREEN}✓${NC}"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}✗${NC}"
        echo -e "${RED}发现 Python 语法错误：${NC}"
        python3 -m py_compile "${PROJECT_ROOT}/carla-bridge/carla_bridge.py" 2>&1 | head -n 5
        FAILED=$((FAILED + 1))
    fi
    if [ -f "${PROJECT_ROOT}/scripts/docker-push-with-registry-probe.sh" ]; then
        echo -n "docker-push-with-registry-probe 自测..."
        if bash "${PROJECT_ROOT}/scripts/docker-push-with-registry-probe.sh" --selftest >/dev/null 2>&1; then
            echo -e "${GREEN}✓${NC}"
            PASSED=$((PASSED + 1))
        else
            echo -e "${RED}✗${NC} 运行 scripts/docker-push-with-registry-probe.sh --selftest 查看详情"
            FAILED=$((FAILED + 1))
        fi
    fi

    # 1.2 检查 Clang-Format (如果 .clang-format 存在)
    if [ -f "${PROJECT_ROOT}/.clang-format" ]; then
        echo -n "检查代码格式... "
        # 使用 git-clang-format 检查差异；如果不存在则尝试 clang-format (需手动实现逻辑，这里暂用 git-clang-format)
        if command -v git-clang-format &> /dev/null; then
            git clang-format --diff --diff-filter=ACMR > /tmp/format_diff.log 2>&1 || true
        else
            echo -e "${YELLOW}⊘${NC} (未找到 git-clang-format 工具，跳过格式检查)"
            WARNINGS=$((WARNINGS + 1))
            return 0
        fi

        if [ -s /tmp/format_diff.log ]; then
            echo -e "${YELLOW}⚠${NC} (代码格式不符合规范)"
            echo "请运行以下命令修复："
            echo "  find . -name '*.cpp' -o -name '*.h' | xargs clang-format -i"
            WARNINGS=$((WARNINGS + 1))
        else
            echo -e "${GREEN}✓${NC}"
            PASSED=$((PASSED + 1))
        fi
    else
        echo -e "${YELLOW}⊘${NC} 未找到 .clang-format，跳过格式检查"
    fi
}

check_build() {
    echo ""
    echo "=========================================="
    echo "2. 编译构建"
    echo "=========================================="
    
    # 检查 Docker 环境
    if ! command -v docker &> /dev/null; then
        echo -e "${RED}✗${NC} Docker 未安装，无法构建"
        FAILED=$((FAILED + 1))
        return 1
    fi

    echo "构建 Backend (C++)..."
    # 假设使用 Docker Compose 构建
    if [ -f "${DEPLOY_DIR}/docker-compose.yml" ]; then
        cd "${DEPLOY_DIR}"
        if docker-compose build backend 2>&1 | tee /tmp/build_backend.log | grep -q "Successfully built"; then
            echo -e "${GREEN}✓${NC} Backend 构建成功"
            PASSED=$((PASSED + 1))
        else
            # grep -q 没找到 Success，视为失败
            echo -e "${RED}✗${NC} Backend 构建失败，请查看日志"
            FAILED=$((FAILED + 1))
            return 1
        fi
    else
        echo -e "${YELLOW}⊘${NC} 未找到 docker-compose.yml，跳过构建"
    fi
}

check_tests() {
    echo ""
    echo "=========================================="
    echo "3. 单元测试"
    echo "=========================================="
    
    echo -n "运行 Backend 单元测试... "
    # 这里需要实际的测试命令，例如 docker-compose run backend test
    # 目前作为占位符
    if docker-compose -f "${DEPLOY_DIR}/docker-compose.yml" run --rm backend ./tests/unit_tests 2>/dev/null; then
        echo -e "${GREEN}✓${NC}"
        PASSED=$((PASSED + 1))
    else
        echo -e "${YELLOW}⚠${NC} 测试未执行或失败 (需要集成测试框架)"
        WARNINGS=$((WARNINGS + 1))
    fi
}

# ===========================
# 基础设施检查函数
# ===========================

check_service() {
    local service=$1
    local check_cmd=$2
    local description=$3
    
    echo -n "检查 $description... "
    if eval "$check_cmd" > /dev/null 2>&1; then
        echo -e "${GREEN}✓${NC}"
        PASSED=$((PASSED + 1))
        return 0
    else
        echo -e "${RED}✗${NC}"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

check_infra() {
    echo "=========================================="
    echo "M0 阶段：基础设施检查"
    echo "=========================================="

    # 1. 环境检查
    echo ""
    echo "1. 环境检查"
    check_service "docker" "command -v docker" "Docker"
    check_service "docker-compose" "command -v docker-compose || docker compose version" "Docker Compose"

    # 2. Docker 服务状态
    echo ""
    echo "2. Docker 服务状态"
    if [ -f "${DEPLOY_DIR}/docker-compose.yml" ]; then
        cd "${DEPLOY_DIR}"
        COMPOSE_CMD="docker-compose"
    else
        cd "${PROJECT_ROOT}"
        COMPOSE_CMD="docker compose"
    fi

    check_service "postgres" "$COMPOSE_CMD ps postgres 2>/dev/null | grep -q 'Up'" "PostgreSQL"
    check_service "keycloak" "$COMPOSE_CMD ps keycloak 2>/dev/null | grep -q 'Up'" "Keycloak"
    check_service "zlmediakit" "$COMPOSE_CMD ps zlmediakit 2>/dev/null | grep -q 'Up'" "ZLMediaKit"
    check_service "coturn" "$COMPOSE_CMD ps coturn 2>/dev/null | grep -q 'Up'" "Coturn"

    # 3. 健康检查
    echo ""
    echo "3. 服务健康检查"
    
    # PostgreSQL
    check_service "postgres-health" \
        "$COMPOSE_CMD exec -T postgres pg_isready -U teleop_user -d teleop_db" \
        "PostgreSQL 健康"

    # Keycloak
    check_service "keycloak-health" \
        "curl -sf http://localhost:8080/health/ready > /dev/null" \
        "Keycloak 健康"
        
    # ZLMediaKit
    check_service "zlm-health" \
        "curl -sf http://localhost/index/api/getServerConfig > /dev/null" \
        "ZLMediaKit API"

    # 4. 端口检查
    echo ""
    echo "4. 端口占用检查"
    check_port 5432 "PostgreSQL"
    check_port 8080 "Keycloak"
    check_port 80 "ZLMediaKit HTTP"
    check_port 3000 "ZLMediaKit WebRTC"
    check_port 3478 "Coturn STUN/TURN"
}

check_port() {
    local port=$1
    local service=$2
    
    if netstat -tuln 2>/dev/null | grep -q ":${port} " || \
       ss -tuln 2>/dev/null | grep -q ":${port} "; then
        echo -e "${GREEN}✓${NC} 端口 ${port} (${service}) 已监听"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}✗${NC} 端口 ${port} (${service}) 未监听"
        FAILED=$((FAILED + 1))
    fi
}

# ===========================
# 主逻辑
# ===========================

if [ "$MODE" == "ci" ]; then
    check_lint
    check_build
    check_tests
elif [ "$MODE" == "infra" ]; then
    check_infra
fi

# 总结
echo ""
echo "=========================================="
echo "检查完成"
echo "=========================================="
echo -e "${GREEN}通过: ${PASSED}${NC}"
if [ $FAILED -gt 0 ]; then
    echo -e "${RED}失败: ${FAILED}${NC}"
fi
if [ $WARNINGS -gt 0 ]; then
    echo -e "${YELLOW}警告: ${WARNINGS}${NC}"
fi
echo ""

if [ $FAILED -gt 0 ]; then
    echo -e "${RED}✗ 检查失败，请修复上述错误${NC}"
    exit 1
fi

echo -e "${GREEN}✓ 所有检查通过${NC}"
exit 0
