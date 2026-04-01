#!/bin/bash
# 快速开发脚本：使用已构建的开发镜像，挂载源码，容器内编译运行
# 避免每次修改代码都重新构建镜像

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE_FILES="-f docker-compose.yml -f docker-compose.dev.yml"

# 颜色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Backend 快速开发模式 ===${NC}"
echo "使用已构建的开发镜像，挂载源码进行编译运行"
echo ""

# 检查开发镜像是否存在
if ! docker images | grep -q "remote-driving-backend.*latest"; then
    echo -e "${YELLOW}开发镜像不存在，正在构建...${NC}"
    docker compose $COMPOSE_FILES build backend
    echo -e "${GREEN}镜像构建完成${NC}"
    echo ""
fi

# 启动服务（如果未运行）
if ! docker compose $COMPOSE_FILES ps backend | grep -q "Up"; then
    echo -e "${GREEN}启动 Backend 服务...${NC}"
    docker compose $COMPOSE_FILES up -d backend
    echo "等待编译完成..."
    sleep 5
    
    # 等待编译完成（最多等待 3 分钟）
    MAX_WAIT=180
    ELAPSED=0
    while [ $ELAPSED -lt $MAX_WAIT ]; do
        if docker compose $COMPOSE_FILES exec backend sh -c "ps aux | grep teleop_backend | grep -v grep" >/dev/null 2>&1; then
            echo -e "${GREEN}Backend 已启动${NC}"
            break
        fi
        sleep 2
        ELAPSED=$((ELAPSED + 2))
        echo -n "."
    done
    echo ""
else
    echo -e "${GREEN}Backend 服务已在运行${NC}"
fi

# 显示日志
echo ""
echo -e "${GREEN}=== Backend 日志（Ctrl+C 退出） ===${NC}"
docker compose $COMPOSE_FILES logs -f backend
