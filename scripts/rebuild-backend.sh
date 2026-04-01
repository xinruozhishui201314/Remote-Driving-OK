#!/bin/bash
# 强制重新编译 Backend（不重建镜像）
# 用于代码修改后快速重新编译

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE_FILES="-f docker-compose.yml -f docker-compose.dev.yml"

echo "=== 强制重新编译 Backend ==="

# 停止当前运行的 backend
if docker compose $COMPOSE_FILES ps backend | grep -q "Up"; then
    echo "停止 Backend..."
    docker compose $COMPOSE_FILES stop backend
fi

# 清理构建目录（可选，强制完全重新编译）
if [ "$1" == "--clean" ]; then
    echo "清理构建缓存..."
    docker compose $COMPOSE_FILES exec backend sh -c "rm -rf /tmp/backend-build" 2>/dev/null || true
fi

# 重启 backend（会自动重新编译）
echo "重启 Backend（将自动重新编译）..."
docker compose $COMPOSE_FILES up -d backend

echo "等待编译完成..."
sleep 5

# 显示编译日志
echo ""
echo "=== 编译日志 ==="
docker compose $COMPOSE_FILES logs backend --tail=50 -f
