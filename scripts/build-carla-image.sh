#!/usr/bin/env bash
# 构建满足 C++ Bridge 运行的 CARLA 镜像（deploy/carla/Dockerfile），并保存为 tar 供后续加载使用。
# 后续启动（start-all-nodes.sh / compose up carla）将优先使用本地该镜像；若无则从保存的 tar 加载。
#
# 用法：
#   ./scripts/build-carla-image.sh
#   SAVE_CARLA_IMAGE=0 ./scripts/build-carla-image.sh   # 不保存 tar（仅构建）
#   CARLA_MAP=Town01 ./scripts/build-carla-image.sh     # 仅影响后续启动时的默认地图
#
# 保存路径：deploy/carla/carla-with-bridge-latest.tar（每次构建覆盖为最新）

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE_CARLA="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
IMAGE_NAME="${CARLA_IMAGE_NAME:-remote-driving/carla-with-bridge:latest}"
SAVE_IMAGE="${SAVE_CARLA_IMAGE:-1}"
CARLA_IMAGE_TAR="${CARLA_IMAGE_TAR:-$PROJECT_ROOT/deploy/carla/carla-with-bridge-latest.tar}"
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[build-carla] $*${NC}"; }
log_ok()      { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail()    { echo -e "  ${YELLOW}[FAIL] $*${NC}"; }
log_progress() { echo -e "  ${CYAN}[进度] $*${NC}"; }

echo -e "${CYAN}========== 构建 CARLA + C++ Bridge 运行环境镜像 ==========${NC}"
echo "  镜像名: $IMAGE_NAME"
echo "  基础镜像: 宿主机已有 carlasim/carla:latest"
echo "  构建依赖: 宿主机 deploy/carla/deps/（Paho 源码 + cmake.tar.gz，需先执行 clone-deps.sh）"
[ "$SAVE_IMAGE" = "1" ] && echo "  构建后保存: $CARLA_IMAGE_TAR"
echo ""

if [ ! -d "${PROJECT_ROOT}/deploy/carla/deps/paho.mqtt.c" ] || [ ! -d "${PROJECT_ROOT}/deploy/carla/deps/paho.mqtt.cpp" ] || [ ! -f "${PROJECT_ROOT}/deploy/carla/deps/cmake.tar.gz" ]; then
  log_section "准备构建依赖到 deploy/carla/deps/（Paho + CMake）"
  bash "${PROJECT_ROOT}/deploy/carla/clone-deps.sh" || { log_fail "clone-deps.sh 失败"; exit 1; }
  log_progress "依赖就绪"
fi

# 构建镜像（带进度与耗时）
log_section "开始构建镜像（apt → Paho C → Paho C++ → pip，预计数分钟～十余分钟）"
log_progress "开始时间: $(date '+%Y-%m-%d %H:%M:%S')"
SECONDS=0
# --progress=plain 使 RUN 内 echo 等输出实时显示，便于查看 [build] 进度
if $COMPOSE_CARLA build --progress=plain carla 2>&1; then
  BUILD_ELAPSED=$SECONDS
  log_ok "镜像构建完成: $IMAGE_NAME（耗时 ${BUILD_ELAPSED}s）"
  log_progress "结束时间: $(date '+%Y-%m-%d %H:%M:%S')"

  if [ "$SAVE_IMAGE" = "1" ]; then
    log_section "保存镜像到文件（后续可从该文件加载使用最新构建）"
    mkdir -p "$(dirname "$CARLA_IMAGE_TAR")"
    SAVE_START=$SECONDS
    if docker save -o "$CARLA_IMAGE_TAR" "$IMAGE_NAME" 2>/dev/null; then
      SAVE_ELAPSED=$((SECONDS - SAVE_START))
      TAR_SIZE=$(du -h "$CARLA_IMAGE_TAR" 2>/dev/null | cut -f1)
      log_ok "已保存: $CARLA_IMAGE_TAR（大小 ${TAR_SIZE:-?}，耗时 ${SAVE_ELAPSED}s）"
    else
      log_fail "保存失败（磁盘空间或路径可写性）"
    fi
  fi

  echo ""
  echo "  后续使用「最新构建的镜像」启动："
  echo "    ./scripts/start-all-nodes.sh   # 无本地镜像时会从 $CARLA_IMAGE_TAR 加载"
  echo "  或仅启动 CARLA："
  echo "    $COMPOSE_CARLA up -d carla"
  echo ""
  echo "  对 C++ Bridge 逐项功能验证："
  echo "    ./scripts/verify-carla-bridge-cpp-features.sh"
  echo "  远驾客户端到 CARLA 整链验证："
  echo "    ./scripts/verify-full-chain-client-to-carla.sh"
  echo ""
  exit 0
fi
log_fail "构建失败；请检查 deploy/carla/Dockerfile 与网络（需能拉取 carlasim/carla:latest）"
log_progress "结束时间: $(date '+%Y-%m-%d %H:%M:%S')（总耗时 ${SECONDS}s）"
exit 1
