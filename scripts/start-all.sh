#!/bin/bash
# 启动所有需要运行的镜像（仅启动，不清理、不验证、不启动客户端 UI）
#
# 使用方式：
#   bash scripts/start-all.sh           # 启动所有服务（镜像已存在时）
#   bash scripts/start-all.sh --build   # 若镜像不存在则先构建 backend/vehicle，再启动
#
# 涉及服务：postgres, keycloak, coturn, zlmediakit, backend, mosquitto,
#          prometheus, grafana, vehicle, client-dev

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml"

if [ "${1:-}" = "--build" ]; then
    echo "构建 backend 与 vehicle 镜像（若需）..."
    $COMPOSE build backend vehicle 2>/dev/null || true
    if ! docker image inspect remote-driving-client-dev:full >/dev/null 2>&1; then
        echo "未找到 client-dev 镜像 remote-driving-client-dev:full，尝试构建完备镜像..."
        bash "$SCRIPT_DIR/build-client-dev-full-image.sh" remote-driving-client-dev:full 2>/dev/null || true
    fi
fi

echo "启动所有服务..."
$COMPOSE up -d --remove-orphans

echo ""
echo "查看状态: $COMPOSE ps"
echo "查看日志: $COMPOSE logs -f"
