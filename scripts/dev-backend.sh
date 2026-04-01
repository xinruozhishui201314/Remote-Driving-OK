#!/bin/bash
# Backend 开发模式脚本：使用开发配置启动/重启 backend
# 修改代码后只需重启容器，容器内会自动重新编译

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "$PROJECT_ROOT"

COMPOSE_FILES="-f docker-compose.yml -f docker-compose.dev.yml"

case "${1:-up}" in
  build)
    echo "构建开发镜像（只需执行一次）..."
    docker compose $COMPOSE_FILES build backend
    ;;
  up)
    echo "启动 backend（开发模式：挂载源码，容器内编译）..."
    docker compose $COMPOSE_FILES up -d backend
    echo ""
    echo "查看日志：docker compose $COMPOSE_FILES logs -f backend"
    echo "重启（重新编译）：docker compose $COMPOSE_FILES restart backend"
    ;;
  restart)
    echo "重启 backend（手动触发重新编译）..."
    echo "注意：开发模式会自动监控文件变化，通常无需手动重启"
    docker compose $COMPOSE_FILES restart backend
    docker compose $COMPOSE_FILES logs -f backend
    ;;
  logs)
    docker compose $COMPOSE_FILES logs -f backend
    ;;
  stop)
    docker compose $COMPOSE_FILES stop backend
    ;;
  down)
    docker compose $COMPOSE_FILES down
    ;;
  *)
    echo "用法: $0 {build|up|restart|logs|stop|down}"
    echo ""
    echo "   build   - 构建开发镜像（首次或 Dockerfile.dev 变更时）"
    echo "   up      - 启动 backend（默认）"
    echo "   restart - 重启 backend（修改代码后执行，容器内重新编译）"
    echo "   logs    - 查看日志"
    echo "   stop    - 停止 backend"
    echo "   down    - 停止并删除容器"
    exit 1
    ;;
esac
