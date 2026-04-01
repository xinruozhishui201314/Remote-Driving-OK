#!/usr/bin/env bash
# 确保 CARLA 仿真容器已启动；供 run-client-ui.sh、run.sh client 等调用。
# 用法：bash scripts/ensure-carla-running.sh
# 环境变量：
#   SKIP_CARLA=1    跳过 CARLA 启动（默认 0）
#   DISPLAY         供 CARLA 窗口显示，未设置时自动推断

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# 跳过 CARLA
if [ "${SKIP_CARLA:-0}" = "1" ]; then
  exit 0
fi

# 已运行则跳过
if docker ps --format '{{.Names}}' 2>/dev/null | grep -q "carla-server"; then
  echo -e "${GREEN}[CARLA] 已运行，跳过启动${NC}"
  exit 0
fi

echo -e "${CYAN}[CARLA] 启动 CARLA 仿真（与远驾客户端同步）...${NC}"

# 1. DISPLAY 与 xhost（CARLA 窗口显示）——与远驾客户端共用同一 DISPLAY
if [ -z "$DISPLAY" ]; then
  export DISPLAY=:0
  echo -e "${YELLOW}[CARLA] DISPLAY 未设置，默认使用 :0（单显示器场景）${NC}"
else
  echo -e "${CYAN}[CARLA] 使用宿主机 DISPLAY=$DISPLAY（与远驾客户端保持一致）${NC}"
fi
if [ -f "$SCRIPT_DIR/setup-host-for-client.sh" ]; then
  . "$SCRIPT_DIR/setup-host-for-client.sh" 2>/dev/null || true
fi

# 2. 确保网络存在
docker network create teleop-network 2>/dev/null || true

# 3. 启动 CARLA（含 Python Bridge）
COMPOSE_CARLA="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
export CARLA_BRIDGE_IN_CONTAINER="${CARLA_BRIDGE_IN_CONTAINER:-1}"
export USE_PYTHON_BRIDGE="${USE_PYTHON_BRIDGE:-1}"
export CARLA_MAP="${CARLA_MAP:-Town01}"
export CARLA_SHOW_WINDOW="${CARLA_SHOW_WINDOW:-1}"

if ! docker image inspect remote-driving/carla-with-bridge:latest >/dev/null 2>&1; then
  echo -e "${YELLOW}[CARLA] 镜像 remote-driving/carla-with-bridge:latest 未找到${NC}"
  echo -e "${YELLOW}[CARLA] 请先执行: ./scripts/start-all-nodes.sh 或 ./scripts/build-carla-image.sh${NC}"
  exit 0
fi

if $COMPOSE_CARLA up -d carla 2>/dev/null; then
  echo -e "${GREEN}[CARLA] 已启动，等待约 8s 初始化...${NC}"
  sleep 8
else
  echo -e "${YELLOW}[CARLA] 启动失败或未完成；可手动执行: $COMPOSE_CARLA up -d carla${NC}"
fi
