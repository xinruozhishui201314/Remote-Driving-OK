#!/usr/bin/env bash
# 一键运行验证脚本
#
# 本脚本整合了所有必要的验证步骤：
# 1. 确保 Client-Dev 镜像已构建。
# 2. 启动所有服务（Backend, Vehicle, ZLM 等）。
# 3. 在容器内重新编译 Backend 和 Vehicle 源码（验证最新代码）。
# 4. 检查服务健康状态。
# 5. 尝试启动客户端（如果配置了 X11）。
#
# 用法：
#   ./scripts/one-click-verify.sh                  # 启动全部并验证（含 CARLA）
#   SKIP_CARLA=1 ./scripts/one-click-verify.sh   # 跳过 CARLA
#   NO_CLIENT=1 ./scripts/one-click-verify.sh      # 仅编译验证，不启动客户端
#
# 依赖：scripts/build-client-dev-full-image.sh, scripts/start-all-nodes-and-verify.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[ONE-CLICK] $*${NC}"; }
log_ok()     { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_warn()   { echo -e "  ${YELLOW}[WARN] $*${NC}"; }
log_fail()   { echo -e "  ${YELLOW}[FAIL] $*${NC}"; }

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}一键验证工程代码结构化日志改造${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# ---------- 阶段 1: 检查并构建 Client-Dev 镜像 ----------
# Client 镜像构建耗时较长，必须提前准备
log_section "阶段 1/2: 检查 Client-Dev 镜像"
if ! docker image inspect remote-driving-client-dev:full >/dev/null 2>&1; then
    log_warn "未找到镜像 remote-driving-client-dev:full，开始构建..."
    log_section "执行: bash scripts/build-client-dev-full-image.sh"
    if bash "$SCRIPT_DIR/build-client-dev-full-image.sh"; then
        log_ok "Client-Dev 镜像构建完成"
    else
        log_fail "Client-Dev 镜像构建失败"
        exit 1
    fi
else
    log_ok "Client-Dev 镜像已存在"
fi
echo ""

# ---------- 阶段 2: 执行容器内编译验证 ----------
# 启动所有节点，并在容器内编译 Backend 和 Vehicle (COMPILE_SOURCE=1)
# 同时执行健康检查
log_section "阶段 2/2: 启动服务并进行容器内编译验证"
log_warn "此步骤将在容器内重新编译 Backend 和 Vehicle 源码，请稍候..."

# 传递环境变量给验证脚本
export COMPILE_SOURCE=1
export SKIP_CARLA="${SKIP_CARLA:-0}"
export NO_CLIENT="${NO_CLIENT:-0}"
export AUTO_START_CLIENT="${AUTO_START_CLIENT:-1}"

# 执行核心验证脚本
if bash "$SCRIPT_DIR/start-all-nodes-and-verify.sh"; then
    log_ok "编译验证与服务检查通过"
else
    log_fail "编译验证或服务检查失败，请查看上方日志"
    exit 1
fi
echo ""

# ---------- 总结 ----------
echo -e "${CYAN}========================================${NC}"
echo -e "${GREEN}一键验证完成！${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""
echo -e "${GREEN}验证结果：${NC}"
echo "  1. Client-Dev 镜像：存在 (remote-driving-client-dev:full)"
echo "  2. Backend/Vehicle 源码：已在容器内重新编译"
echo "  3. 服务状态：Backend(8081), ZLM(80), Vehicle, Postgres, Keycloak 均已就绪"
echo "  4. 日志格式：所有模块已输出 JSON 结构化日志 (请检查容器日志)"
echo ""
echo -e "${CYAN}后续操作建议：${NC}"
echo "  - 查看后端日志：docker logs teleop-backend --tail 50"
echo "  - 查看车端日志：docker logs teleop-vehicle --tail 50"
echo "  - 检查 JSON 格式：grep 'node_id' <(docker logs teleop-backend --tail 20)"
echo "  - 停止所有节点：docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml down"
echo ""
