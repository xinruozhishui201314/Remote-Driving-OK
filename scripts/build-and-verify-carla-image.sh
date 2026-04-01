#!/usr/bin/env bash
# 构建 CARLA 镜像（含容器内 Bridge），启动容器并验证；验证成功后镜像固化，后续运行
# start-all-nodes.sh 时直接使用该镜像启动容器，不再构建。
#
# 用法：
#   ./scripts/build-and-verify-carla-image.sh
#   CARLA_MAP=Town01 ./scripts/build-and-verify-carla-image.sh
#
# 环境变量：CARLA_MAP（默认 Town01）、NVIDIA_VISIBLE_DEVICES 等与 start-all-nodes.sh 一致。

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml"
COMPOSE_CARLA="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
CARLA_IMAGE_NAME="${CARLA_IMAGE_NAME:-remote-driving/carla-with-bridge:latest}"
CARLA_IMAGE_VERIFIED="${CARLA_IMAGE_VERIFIED:-remote-driving/carla-with-bridge:verified}"
CARLA_MAP="${CARLA_MAP:-Town01}"
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[LOG] $*${NC}"; }
log_ok()     { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_warn()   { echo -e "  ${YELLOW}[WARN] $*${NC}"; }
log_fail()   { echo -e "  ${YELLOW}[FAIL] $*${NC}"; }

echo -e "${CYAN}========== 构建并验证 CARLA 镜像（固化后供 start-all-nodes 直接使用）==========${NC}"
echo ""

# 1. 构建镜像
log_section "步骤 1/4: 构建 CARLA 镜像（deploy/carla/Dockerfile）"
export CARLA_MAP
if ! $COMPOSE_CARLA build carla 2>&1; then
  log_fail "构建失败；请检查 deploy/carla/Dockerfile 与网络（需能拉取 carlasim/carla:latest）"
  exit 1
fi
log_ok "镜像构建完成: $CARLA_IMAGE_NAME"
echo ""

# 2. 确保 Compose 网络存在并启动 CARLA 依赖（ZLM/MQTT 供 Bridge 连接）
log_section "步骤 2/4: 启动基础服务以创建网络（postgres keycloak coturn zlmediakit mosquitto）"
$COMPOSE up -d --remove-orphans postgres keycloak coturn zlmediakit mosquitto 2>/dev/null || true
sleep 3
log_ok "基础服务已 up，网络 teleop-network 已就绪"
echo ""

# 3. 启动 CARLA 容器
log_section "步骤 3/4: 启动 CARLA 容器（场景: ${CARLA_MAP}）"
if docker ps --format '{{.Names}}' | grep -q "carla-server"; then
  log_warn "carla-server 已在运行，先停止再启动以使用新镜像"
  $COMPOSE_CARLA stop carla 2>/dev/null || true
  sleep 2
fi
if ! $COMPOSE_CARLA up -d carla 2>&1; then
  log_fail "CARLA 容器启动失败（可能缺 nvidia runtime）"
  echo "  排查: $COMPOSE_CARLA up -d carla 2>&1"
  exit 1
fi
log_ok "CARLA 容器已启动"
echo ""

# 4. 验证：等待 CARLA 端口 2000 就绪（entrypoint 先起 CARLA 再起 Bridge，最多等约 90s）
carla_port_open() {
  python3 -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(2)
try:
  s.connect(('127.0.0.1', 2000))
  s.close()
  exit(0)
except Exception:
  exit(1)
" 2>/dev/null
}
log_section "步骤 4/4: 验证 CARLA 服务（等待端口 2000 就绪，最多 90s）"
WAIT_MAX=90
WAIT_INTERVAL=3
ELAPSED=0
while [ $ELAPSED -lt $WAIT_MAX ]; do
  if carla_port_open; then
    log_ok "CARLA 端口 2000 已就绪（${ELAPSED}s）"
    break
  fi
  sleep $WAIT_INTERVAL
  ELAPSED=$((ELAPSED + WAIT_INTERVAL))
  echo "  ${ELAPSED}s: 等待 CARLA 端口 2000 ..."
done
if ! carla_port_open; then
  log_fail "超时未检测到 CARLA 端口 2000"
  log_section "容器日志: $COMPOSE_CARLA logs carla 2>&1 | tail -40"
  $COMPOSE_CARLA logs carla 2>&1 | tail -40 | sed 's/^/    /' || true
  exit 1
fi
echo ""

# 固化：打 verified 标签（可选，便于区分“已验证”镜像）
if docker image inspect "$CARLA_IMAGE_NAME" >/dev/null 2>&1; then
  docker tag "$CARLA_IMAGE_NAME" "$CARLA_IMAGE_VERIFIED" 2>/dev/null || true
  log_section "已打标签: $CARLA_IMAGE_VERIFIED"
fi

echo -e "${GREEN}========== 验证成功，CARLA 镜像已固化 ==========${NC}"
echo ""
echo "  后续运行 ./scripts/start-all-nodes.sh 或 ./scripts/start-all-nodes-and-verify.sh 时，"
echo "  将直接使用已构建的镜像启动容器，不再执行构建。"
echo ""
echo "  停止当前 CARLA 容器: $COMPOSE_CARLA stop carla"
echo "  重新启动所有节点: ./scripts/start-all-nodes.sh"
echo ""
