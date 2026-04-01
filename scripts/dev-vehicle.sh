#!/bin/bash
# Vehicle-side 开发模式脚本：使用开发配置启动/重启车辆端

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "$PROJECT_ROOT"

COMPOSE_FILES="-f docker-compose.yml -f docker-compose.vehicle.dev.yml"

case "${1:-up}" in
  build)
    echo "构建 Vehicle-side 开发镜像（只需执行一次或 Dockerfile.dev 变更时）..."
    docker compose $COMPOSE_FILES build vehicle
    ;;
  up)
    echo "启动 Vehicle-side（开发模式：挂载源码，容器内编译）..."
    docker compose $COMPOSE_FILES up -d vehicle
    echo ""
    echo "查看日志：docker compose $COMPOSE_FILES logs -f vehicle"
    echo "重启（重新编译）：docker compose $COMPOSE_FILES restart vehicle"
    ;;
  restart)
    echo "重启 Vehicle-side（手动触发重新编译）..."
    docker compose $COMPOSE_FILES restart vehicle
    docker compose $COMPOSE_FILES logs -f vehicle
    ;;
  logs)
    docker compose $COMPOSE_FILES logs -f vehicle
    ;;
  stop)
    docker compose $COMPOSE_FILES stop vehicle
    ;;
  down)
    docker compose $COMPOSE_FILES down
    ;;
  *)
    echo "用法: $0 {build|up|restart|logs|stop|down}"
    echo ""
    echo "   build   - 构建 Vehicle-side 开发镜像"
    echo "   up      - 启动 Vehicle-side（默认）"
    echo "   restart - 重启 Vehicle-side（修改代码后执行，容器内重新编译）"
    echo "   logs    - 查看车辆端日志"
    echo "   stop    - 停止 Vehicle-side"
    echo "   down    - 停止并删除容器（含网络）"
    exit 1
    ;;
esac

