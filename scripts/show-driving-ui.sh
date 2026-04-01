#!/usr/bin/env bash
# 直接启动远驾操作界面（仅驾驶界面，跳过登录/选车）
# 用法：bash scripts/show-driving-ui.sh
#
# 前置条件：
#   - 镜像 remote-driving-client-dev:full 已构建（否则先运行 bash scripts/build-client-dev-full-image.sh）
#   - 宿主机有 X11 显示（Linux 桌面环境）
#
# 本脚本会：
#   1. 启动必要服务（Postgres / Keycloak / Mosquitto / ZLM / Backend / Client-dev，无需 vehicle）
#   2. 编译客户端（若未编译）
#   3. 弹出远驾操作界面（CLIENT_AUTO_CONNECT_VIDEO=1 约 2 秒后自动进入，无需登录）

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# 布局预览专用 compose：client-dev 仅依赖 backend，无需 vehicle
COMPOSE_LAYOUT="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml -f docker-compose.layout-preview.yml"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

die() {
    echo -e "${RED}$*${NC}"
    exit 1
}

echo -e "${CYAN}========== 启动远驾操作界面（布局预览）==========${NC}"
echo ""

# 1. 检查镜像
if ! docker image inspect remote-driving-client-dev:full >/dev/null 2>&1; then
    die "✗ 镜像 remote-driving-client-dev:full 不存在\n  请先执行: bash scripts/build-client-dev-full-image.sh"
fi
echo -e "${GREEN}✓ 镜像已就绪${NC}"

# 2. 检查 X11/DISPLAY（提前检查，避免启动服务后才发现无法显示）
if [ -z "$DISPLAY" ]; then
    export DISPLAY=:0
    echo -e "${YELLOW}设置 DISPLAY=$DISPLAY${NC}"
fi
if ! xhost 2>&1 | grep -q "LOCAL:"; then
    echo -e "${YELLOW}设置 X11 权限（xhost +local:docker）...${NC}"
    if ! xhost +local:docker 2>/dev/null; then
        die "✗ X11 权限设置失败。请确保：\n  1) 在有图形桌面的终端运行（非 SSH 无头环境）\n  2) 若为 WSL2，需安装 WSLg 或 VcXsrv"
    fi
fi
echo -e "${GREEN}✓ X11 已就绪 (DISPLAY=$DISPLAY)${NC}"

# 3. 确保网络存在
docker network create teleop-network 2>/dev/null || true

# 4. 启动必要服务（使用 layout-preview 跳过 vehicle）
echo ""
echo -e "${CYAN}启动服务（Postgres / Keycloak / Mosquitto / ZLM / Backend / Client-dev）...${NC}"
$COMPOSE_LAYOUT up -d --remove-orphans teleop-postgres teleop-mosquitto zlmediakit 2>/dev/null || true
sleep 3
$COMPOSE_LAYOUT up -d --remove-orphans keycloak 2>/dev/null || true
sleep 4
docker rm -f remote-driving-backend-1 2>/dev/null || true
$COMPOSE_LAYOUT up -d --force-recreate --remove-orphans backend 2>/dev/null || true
sleep 3
# 强制重建 client-dev 以确保 volume 挂载生效（QML 修改后需重新挂载）
$COMPOSE_LAYOUT up -d --force-recreate --no-build --remove-orphans client-dev 2>/dev/null || true
sleep 2

# 5. 验证 client-dev 已运行
if ! $COMPOSE_LAYOUT ps client-dev 2>/dev/null | grep -q "Up"; then
    echo -e "${RED}✗ client-dev 容器未运行${NC}"
    echo "  尝试查看日志: $COMPOSE_LAYOUT logs client-dev"
    $COMPOSE_LAYOUT ps 2>/dev/null | sed 's/^/    /'
    die "请检查上述输出，或运行: bash scripts/diagnose-start-all.sh"
fi
echo -e "${GREEN}✓ 服务已启动${NC}"

# 6. 编译客户端
echo ""
echo -e "${CYAN}检查客户端编译...${NC}"
if ! $COMPOSE_LAYOUT exec -T client-dev bash -c "test -x /tmp/client-build/RemoteDrivingClient" 2>/dev/null; then
    echo "  首次编译中（约 1–2 分钟）..."
    if ! $COMPOSE_LAYOUT exec -T client-dev bash -c "
        mkdir -p /tmp/client-build && cd /tmp/client-build
        [ ! -f CMakeCache.txt ] && cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug
        make -j4
    " 2>&1 | tail -20; then
        die "✗ 客户端编译失败。请检查: $COMPOSE_LAYOUT exec -it client-dev bash -c 'cd /tmp/client-build && make -j4'"
    fi
fi
echo -e "${GREEN}✓ 客户端已就绪${NC}"

# 7. 启动界面（仅远驾操作界面，跳过登录/选车）
echo ""
echo -e "${GREEN}========== 弹出远驾操作界面 ==========${NC}"
echo "  已跳过登录/选车，直接显示驾驶界面（右视图、高精地图等）"
echo "  CLIENT_AUTO_CONNECT_VIDEO=1 约 2 秒后自动进入"
echo ""
echo -e "${YELLOW}若窗口未弹出，请检查：${NC}"
echo "  - echo \$DISPLAY 应为 :0 或 :1"
echo "  - xhost +local:docker 已执行"
echo "  - 在图形桌面终端运行，非 SSH 无头环境"
echo ""
echo -e "${YELLOW}按 Ctrl+C 可关闭客户端${NC}"
echo ""

# 从 /workspace/client 运行，确保能找到 qml/main.qml（client 源码挂载点）
# QML_DISABLE_DISK_CACHE=1 禁用 QML 磁盘缓存，确保 QML 修改立即生效
$COMPOSE_LAYOUT exec -it \
  -e DISPLAY="${DISPLAY:-:0}" \
  -e CLIENT_AUTO_CONNECT_VIDEO=1 \
  -e QML_DISABLE_DISK_CACHE=1 \
  -w /workspace/client client-dev bash -c "/tmp/client-build/RemoteDrivingClient"
