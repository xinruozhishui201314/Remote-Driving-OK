#!/bin/bash
# 快速重建 Backend：构建 + 重启
# 用于代码修改后的快速迭代

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== 快速重建 Backend ===${NC}"
echo ""

# 构建
echo -e "${YELLOW}构建 Backend 镜像...${NC}"
if docker compose build backend; then
    echo -e "${GREEN}构建完成${NC}"
else
    echo "构建失败"
    exit 1
fi

echo ""

# 重启
echo -e "${YELLOW}重启 Backend 服务...${NC}"
docker compose restart backend

echo ""
echo -e "${GREEN}等待服务启动...${NC}"
sleep 3

# 检查状态
echo ""
echo "=== 服务状态 ==="
docker compose ps backend

echo ""
echo "=== 最新日志 ==="
docker compose logs backend --tail=10

echo ""
echo -e "${GREEN}完成！${NC}"
echo "查看完整日志: docker compose logs -f backend"
echo "测试 API: ./scripts/test-client-backend-integration.sh"
