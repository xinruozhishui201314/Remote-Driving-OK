#!/bin/bash
# M0-M1 开发脚本
# 用于开发环境的启动、测试和重启

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 颜色输出
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=========================================="
echo "远程驾驶系统 - 开发模式"
echo "=========================================="
echo ""

# 检查 Docker
if ! command -v docker >/dev/null 2>&1; then
    echo -e "${RED}✗${NC} Docker 未安装"
    exit 1
fi

# 检查 Docker Compose
if ! command -v docker-compose >/dev/null 2>&1; then
    echo -e "${YELLOW}⚠${NC} docker-compose 未安装，尝试使用 docker compose"
    if ! docker compose version >/dev/null 2>&1; then
        echo -e "${RED}✗${NC} docker compose 也不可用"
        echo "请安装 Docker Compose 或 Docker Compose Plugin"
        exit 1
    fi
fi

echo -e "${GREEN}✓${NC} Docker 已安装"

# 进入项目根目录
cd "${PROJECT_ROOT}"

# 检查 deploy/.env 文件
if [ ! -f "deploy/.env" ]; then
    echo "创建 .env 文件..."
    cp deploy/.env.example deploy/.env
    echo -e "${YELLOW}⚠${NC} 请根据需要修改 deploy/.env 中的密码"
    echo ""
fi

# 启动开发环境
echo "启动开发环境服务..."
echo ""

# 使用 docker-compose（尝试两种方式）
COMPOSE_CMD=""
if command -v docker-compose >/dev/null 2>&1; then
    COMPOSE_CMD="docker-compose"
elif docker compose version >/dev/null 2>&1; then
    COMPOSE_CMD="docker compose"
else
    echo -e "${RED}✗${NC} 不可用"
    exit 1
fi

# 启动核心服务
echo "启动核心服务（PostgreSQL, Keycloak, ZLMediaKit, Coturn）..."
$COMPOSE_CMD -f docker-compose.yml up -d postgres keycloak zlmediakit coturn

echo ""
echo "等待服务启动..."
sleep 10

# 检查服务状态
echo "检查服务状态..."
$COMPOSE_CMD -f docker-compose.yml ps

echo ""
echo "=========================================="
echo "开发环境已启动"
echo "=========================================="
echo ""
echo "服务地址:"
echo "  - Keycloak Admin: http://localhost:8080/admin"
echo "  - ZLMediaKit API: http://localhost/index/api/getServerConfig"
echo ""
echo "开发命令:"
echo "  查看日志: $COMPOSE_CMD -f docker-compose logs -f"
echo "  停止服务: $COMPOSE_CMD -f docker-compose.yml down"
echo "  重启服务: $COMPOSE_CMD -f docker-compose.yml restart"
echo "  运行检查: bash scripts/check.sh"
echo ""
echo "开发提示:"
echo "  - 客户端开发: docker-compose -f docker-compose.yml run --rm client-dev bash"
echo "  - 后端开发: docker-compose -f docker-compose.yml run --rm backend bash"
echo "=========================================="
