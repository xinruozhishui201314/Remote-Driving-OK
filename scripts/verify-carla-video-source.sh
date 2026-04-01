#!/usr/bin/env bash
# 自动化验证：当前推流是否来自 CARLA 真实相机（Python Bridge），而非 C++ Bridge 的 testsrc。
# 前提：CARLA 容器已启动（如 ./scripts/start-all-nodes.sh）。
#
# 用法：./scripts/verify-carla-video-source.sh
# 环境：EXPECT_CARLA_VIDEO=1（默认）表示期望使用 CARLA 视频源，否则判定为失败。
#
# 验证方式：1) 容器日志含「Python Bridge（CARLA 相机推流）」 2) 容器内进程为 python3 carla_bridge.py

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
EXPECT_CARLA_VIDEO="${EXPECT_CARLA_VIDEO:-1}"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_ok()   { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail() { echo -e "  ${RED}[FAIL] $*${NC}"; }
log_info() { echo -e "  ${CYAN}$*${NC}"; }

CARLA_VIDEO=0

echo ""
echo -e "${CYAN}========== 验证视频流来源是否为 CARLA（Python Bridge）==========${NC}"
echo "  期望 CARLA 视频源: EXPECT_CARLA_VIDEO=$EXPECT_CARLA_VIDEO"
echo ""

# ---------- 1) 容器运行 ----------
if ! docker ps --format '{{.Names}}' | grep -q "carla-server"; then
  log_fail "carla-server 未运行；请先启动: ./scripts/start-all-nodes.sh"
  exit 1
fi
log_ok "carla-server 已运行"

# ---------- 2) 日志：是否使用 Python Bridge（CARLA 相机）----------
# entrypoint 的该条日志在启动时输出，需检查完整日志
CARLA_LOGS="$($COMPOSE logs carla 2>&1)"
if echo "$CARLA_LOGS" | grep -qE "视频源: CARLA 仿真相机|USE_PYTHON_BRIDGE=1.*Python Bridge|Python Bridge（CARLA 相机推流）|启动 CARLA Bridge \(Python\)"; then
  log_ok "日志确认为 Python Bridge（CARLA 相机推流）"
  CARLA_VIDEO=1
else
  if echo "$CARLA_LOGS" | grep -qE "启动 CARLA Bridge \(C\+\+\)|C\+\+ Bridge 编译成功"; then
    log_info "日志显示为 C++ Bridge（默认为 testsrc，非 CARLA 画面）"
  else
    log_info "日志未明确匹配 Bridge 类型"
  fi
fi

# ---------- 3) 进程：是否为 python3 carla_bridge.py ----------
if [ "$CARLA_VIDEO" = "0" ]; then
  PROC=""
  if docker exec carla-server sh -c "ps aux 2>/dev/null" 2>/dev/null | grep -v grep | grep -q "carla_bridge\.py"; then
    PROC="python"
  fi
  if [ -z "$PROC" ] && docker exec carla-server sh -c "ps -ef 2>/dev/null" 2>/dev/null | grep -v grep | grep -q "carla_bridge\.py"; then
    PROC="python"
  fi
  if [ -n "$PROC" ]; then
    log_ok "进程确认为 Python Bridge (carla_bridge.py)"
    CARLA_VIDEO=1
  else
    if docker exec carla-server sh -c "ps aux 2>/dev/null; ps -ef 2>/dev/null" 2>/dev/null | grep -v grep | grep -q "[c]arla_bridge"; then
      log_info "进程为 C++ Bridge 二进制（非 carla_bridge.py）"
    fi
  fi
fi

# ---------- 结果 ----------
echo ""
if [ "$CARLA_VIDEO" = "1" ]; then
  echo -e "${GREEN}========== 结论：当前为 CARLA 视频流（Python Bridge）==========${NC}"
  if [ "$EXPECT_CARLA_VIDEO" = "1" ]; then
    exit 0
  fi
  echo -e "${YELLOW}  EXPECT_CARLA_VIDEO=0，但检测到 CARLA 视频源，按期望判定为失败${NC}"
  exit 1
fi

echo -e "${RED}========== 结论：未确认为 CARLA 视频流（可能为 C++ Bridge testsrc）==========${NC}"
log_info "确认方法: docker logs carla-server 2>&1 | grep 视频源"
log_info "若需使用 CARLA 画面，请设置 USE_PYTHON_BRIDGE=1 并重启 CARLA 容器（默认已为 1）"
if [ "$EXPECT_CARLA_VIDEO" = "1" ]; then
  exit 1
fi
exit 0
