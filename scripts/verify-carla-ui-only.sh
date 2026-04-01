#!/usr/bin/env bash
# 仅验证 CARLA 仿真界面：单独启动 CARLA 并检查窗口是否能显示。
# 不启动 Backend、Client、Vehicle 等，用于快速排查 CARLA 窗口问题。
#
# 用法：bash scripts/verify-carla-ui-only.sh
# 环境变量：
#   DISPLAY           宿主机 DISPLAY（未设置时默认 :0）
#   CARLA_SHOW_WINDOW  1=显示窗口（默认）；0=无头模式
#   RESTART_CARLA      1=强制重启 CARLA 后验证（默认 0，已运行则跳过启动）
#   TRY_DISPLAY_0      1=若当前 DISPLAY 无窗口则尝试 :0（Wayland/XWayland 常用 :0）

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_ok()   { echo -e "${GREEN}[OK] $*${NC}"; }
log_warn() { echo -e "${YELLOW}[WARN] $*${NC}"; }
log_info() { echo -e "${CYAN}[INFO] $*${NC}"; }

echo -e "${CYAN}========== 仅验证 CARLA 仿真界面 ==========${NC}"
echo ""

# 1. DISPLAY 与 xhost（必须与宿主机实际 X11 socket 匹配）
#    宿主机 ls /tmp/.X11-unix/ 可查看可用 socket（X0、X1 等）
if [ -z "$DISPLAY" ]; then
  # 自动选择第一个存在的 socket
  for x in 0 1 2; do
    if [ -S "/tmp/.X11-unix/X${x}" ]; then
      export DISPLAY=:${x}
      log_info "DISPLAY 未设置，自动选择宿主机存在的 socket: DISPLAY=:${x}"
      break
    fi
  done
  [ -z "$DISPLAY" ] && export DISPLAY=:0 && log_warn "未找到 X11 socket，默认 :0（可能无法显示窗口）"
else
  X_NUM="${DISPLAY#*:}"; X_NUM="${X_NUM%%.*}"
  if [ -n "$X_NUM" ] && [ ! -S "/tmp/.X11-unix/X${X_NUM}" ]; then
    log_warn "宿主机 DISPLAY=$DISPLAY 但 /tmp/.X11-unix/X${X_NUM} 不存在"
    for x in 0 1 2; do
      if [ -S "/tmp/.X11-unix/X${x}" ]; then
        export DISPLAY=:${x}
        log_info "改用宿主机存在的 socket: DISPLAY=:${x}"
        break
      fi
    done
  else
    log_ok "使用宿主机 DISPLAY=$DISPLAY"
  fi
fi
if [ -f "$SCRIPT_DIR/setup-host-for-client.sh" ]; then
  . "$SCRIPT_DIR/setup-host-for-client.sh" 2>/dev/null || true
fi
export CARLA_SHOW_WINDOW="${CARLA_SHOW_WINDOW:-1}"
echo ""

# 2. 检查镜像
if ! docker image inspect remote-driving/carla-with-bridge:latest >/dev/null 2>&1; then
  log_warn "镜像 remote-driving/carla-with-bridge:latest 未找到"
  echo "  请先执行: ./scripts/build-carla-image.sh 或 ./scripts/start-all-nodes.sh"
  exit 1
fi
log_ok "CARLA 镜像已就绪"
echo ""

# 3. 启动或重启 CARLA
COMPOSE_CARLA="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
export CARLA_BRIDGE_IN_CONTAINER="${CARLA_BRIDGE_IN_CONTAINER:-1}"
export CARLA_MAP="${CARLA_MAP:-Town01}"

if docker ps --format '{{.Names}}' 2>/dev/null | grep -q "carla-server"; then
  if [ "${RESTART_CARLA:-0}" = "1" ]; then
    log_info "强制重启 CARLA..."
    docker stop carla-server 2>/dev/null || true
    docker rm -f carla-server 2>/dev/null || true
    sleep 2
  else
    log_ok "CARLA 已在运行，跳过启动"
    echo ""
    bash "$SCRIPT_DIR/verify-carla-window-display.sh" || exit $?
    bash "$SCRIPT_DIR/verify-carla-spectator.sh" || exit $?
    exit 0
  fi
fi

log_info "启动 CARLA + MQTT + ZLM（Bridge 需连接 MQTT/ZLM，否则无法完成启动）..."
docker network create teleop-network 2>/dev/null || true
# 启动 mosquitto、zlmediakit、carla；Bridge 连接 MQTT/ZLM 后才能完成初始化并输出完整日志
if ! $COMPOSE_CARLA up -d teleop-mosquitto zlmediakit carla 2>&1; then
  echo -e "${RED}[FAIL] CARLA 启动失败${NC}"
  exit 1
fi
log_ok "CARLA 已启动，等待约 90s 初始化（entrypoint 跳过 apt 时约 50s 即可；首次 apt 安装时需 2–3 分钟）..."
sleep 90
echo ""

# 4. 运行窗口显示验证
bash "$SCRIPT_DIR/verify-carla-window-display.sh" || exit $?

# 5. 运行 Spectator 功能验证（Bridge 视角跟随车辆）
bash "$SCRIPT_DIR/verify-carla-spectator.sh" || exit $?

echo ""
log_info "若仍无 CARLA 窗口，可尝试："
echo "  1) 确认宿主机 X11 socket：ls /tmp/.X11-unix/  # 有 X1 则用 DISPLAY=:1，有 X0 则用 :0"
echo "  2) 验证 X11：docker run --rm -e DISPLAY=\$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix x11-apps xeyes"
echo "  3) 查看完整日志与 Bridge 状态：docker logs carla-server 2>&1 | tail -100"
echo "  4) 详见排障文档：docs/CARLA_LOG_ANALYSIS.md、docs/CARLA_UI_TROUBLESHOOTING.md"
