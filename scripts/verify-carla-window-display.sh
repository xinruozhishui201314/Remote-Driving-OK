#!/usr/bin/env bash
# 验证 CARLA 仿真窗口是否能正确显示（Python Bridge 模式）。
# 检查：容器运行、DISPLAY 配置、X11 挂载、entrypoint 日志中的窗口模式。
#
# 用法：bash scripts/verify-carla-window-display.sh
# 前置：CARLA 容器已启动（start-all-nodes-and-verify.sh 或 start-all-nodes.sh 已执行）

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
log_fail() { echo -e "${RED}[FAIL] $*${NC}"; }
log_warn() { echo -e "${YELLOW}[WARN] $*${NC}"; }
log_info() { echo -e "${CYAN}[INFO] $*${NC}"; }

FAILED=0

echo -e "${CYAN}========== CARLA 窗口显示验证 ==========${NC}"
echo ""

# 1. 容器是否运行
log_info "1. 检查 CARLA 容器状态..."
if ! docker ps --format '{{.Names}}' 2>/dev/null | grep -q "carla-server"; then
  log_fail "CARLA 容器(carla-server) 未运行"
  log_info "请先执行: bash scripts/start-all-nodes-and-verify.sh 或 bash scripts/start-all-nodes.sh"
  exit 1
fi
log_ok "CARLA 容器已运行"
echo ""

# 2. CARLA 服务是否正常运行（端口 2000 可连接 + CarlaUE4 进程存在）
log_info "2. 检查 CARLA 服务运行状态..."
CARLA_PORT_OK=0
if command -v nc >/dev/null 2>&1; then
  nc -z 127.0.0.1 2000 2>/dev/null && CARLA_PORT_OK=1
elif python3 -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(2)
try:
  s.connect(('127.0.0.1', 2000))
  s.close()
  exit(0)
except Exception:
  exit(1)
" 2>/dev/null; then
  CARLA_PORT_OK=1
fi
if [ "$CARLA_PORT_OK" = "1" ]; then
  log_ok "CARLA 端口 2000 可连接（RPC 服务正常）"
else
  log_fail "CARLA 端口 2000 不可达；CARLA 可能仍在启动（约需 50s）或已崩溃"
  log_info "  排查: docker logs carla-server 2>&1 | tail -50"
  FAILED=1
fi
# CarlaUE4 进程
if docker exec carla-server ps aux 2>/dev/null | grep -v grep | grep -q CarlaUE4; then
  log_ok "CarlaUE4 进程已运行"
else
  log_warn "未检测到 CarlaUE4 进程；CARLA 可能尚未启动完成或已退出"
fi
echo ""

# 3. 容器内 DISPLAY 环境变量
log_info "3. 检查容器内 DISPLAY..."
CARLA_DISPLAY=$(docker exec carla-server printenv DISPLAY 2>/dev/null || echo "")
if [ -z "$CARLA_DISPLAY" ]; then
  log_fail "容器内 DISPLAY 未设置"
  FAILED=1
else
  log_ok "容器内 DISPLAY=$CARLA_DISPLAY"
fi
echo ""

# 4. X11 socket 挂载
log_info "4. 检查 X11 socket 挂载..."
if [ -n "$CARLA_DISPLAY" ]; then
  X_NUM="${CARLA_DISPLAY#*:}"
  if docker exec carla-server test -S "/tmp/.X11-unix/X${X_NUM}" 2>/dev/null; then
    log_ok "X11 socket /tmp/.X11-unix/X${X_NUM} 存在"
  elif docker exec carla-server test -d /tmp/.X11-unix 2>/dev/null; then
    log_warn "X11 目录存在但 socket X${X_NUM} 可能不匹配；宿主机请执行: xhost +local:docker"
  else
    log_fail "X11 未正确挂载；docker-compose.carla.yml 需包含: - /tmp/.X11-unix:/tmp/.X11-unix"
    FAILED=1
  fi
else
  log_warn "DISPLAY 未设置，跳过 X11 socket 检查"
fi
echo ""

# 5. entrypoint 日志：窗口模式 vs 无头模式
#    使用 tail 获取最新日志（Bridge 启动在末尾，head 会漏掉）
log_info "5. 检查 CARLA 启动模式（窗口 vs 无头）..."
CARLA_LOGS=$(docker logs carla-server 2>&1 | tail -200)
if echo "$CARLA_LOGS" | grep -q "显示仿真窗口"; then
  log_ok "entrypoint 已以窗口模式启动 CARLA（未使用 -RenderOffScreen）"
elif echo "$CARLA_LOGS" | grep -q "无头模式"; then
  log_fail "CARLA 以无头模式启动（-RenderOffScreen）；需设置 CARLA_SHOW_WINDOW=1 后重跑"
  FAILED=1
elif echo "$CARLA_LOGS" | grep -qE "启动 CARLA 场景|启动 CARLA"; then
  log_ok "entrypoint 已启动 CARLA（镜像可能为旧版，未输出 DISPLAY 模式；DISPLAY=$CARLA_DISPLAY 已配置）"
elif [ -n "$CARLA_DISPLAY" ] && ! echo "$CARLA_LOGS" | grep -q "无头模式"; then
  log_ok "DISPLAY 已配置且未检测到无头模式；CARLA 应尝试显示窗口"
else
  log_warn "未在日志中找到明确的窗口/无头模式标识；请检查: docker logs carla-server 2>&1 | tail -80"
fi
echo ""

# 6. 检查是否有 display 连接错误
log_info "6. 检查是否有 display 连接错误..."
if echo "$CARLA_LOGS" | grep -qiE "cannot open display|could not connect to display|unable to open display|failed to connect.*display"; then
  log_fail "日志中检测到 display 连接失败"
  log_info "请确保：1) 宿主机执行 xhost +local:docker；2) DISPLAY 与宿主机一致（宿主机 ls /tmp/.X11-unix/ 确认有 X0 或 X1）"
  FAILED=1
else
  log_ok "未检测到 display 连接错误"
fi
echo ""

# 7. Bridge 启动状态（Python/C++ Bridge 在 entrypoint 末尾，需用 tail 获取）
log_info "7. 检查 Bridge 启动状态..."
if echo "$CARLA_LOGS" | grep -q "USE_PYTHON_BRIDGE=1"; then
  log_ok "Python Bridge 已启用（USE_PYTHON_BRIDGE=1）"
  if echo "$CARLA_LOGS" | grep -qE "启动 Python Bridge|启动 C++ Bridge"; then
    log_ok "Bridge 已启动（entrypoint 已 exec 到 Bridge 进程）"
  else
    log_warn "未在日志中看到「启动 Python Bridge」；可能仍在 pip 安装或 MQTT/ZLM 未启动导致 Bridge 阻塞"
    log_info "  若仅启动 carla 未启动 mosquitto/zlmediakit，Bridge 连接 MQTT 会失败；建议用 start-all-nodes-and-verify.sh 全链路启动"
  fi
elif echo "$CARLA_LOGS" | grep -q "USE_PYTHON_BRIDGE=0"; then
  log_ok "C++ Bridge 模式（USE_PYTHON_BRIDGE=0）"
  if echo "$CARLA_LOGS" | grep -q "启动 C++ Bridge"; then
    log_ok "C++ Bridge 已启动"
  else
    log_warn "未在日志中看到「启动 C++ Bridge」"
  fi
else
  log_warn "未在日志中找到 USE_PYTHON_BRIDGE；entrypoint 可能仍在「额外等待 45 秒」阶段（约 48s 后才会输出 Bridge 相关日志）"
  log_info "  若等待不足 60s，可稍后重跑验证或执行: docker logs carla-server 2>&1 | tail -80"
fi
echo ""

# 8. 检查 Bridge 进程是否在运行
log_info "8. 检查 Bridge 进程..."
if docker exec carla-server ps aux 2>/dev/null | grep -qE "carla_bridge|python.*carla_bridge"; then
  log_ok "Bridge 进程已运行"
else
  log_warn "未检测到 Bridge 进程（carla_bridge 或 python carla_bridge）；可能仍在启动或 MQTT/ZLM 连接失败导致退出"
fi
echo ""

# 9. 采集完整日志与深度诊断（便于精准定位问题）
LOG_ARCHIVE="/tmp/carla-window-diagnose-$(date +%Y%m%d-%H%M%S).log"
log_info "9. 采集 CARLA 完整日志与深度诊断到: $LOG_ARCHIVE"
{
  echo "===== 1. docker logs carla-server (完整输出) ====="
  docker logs carla-server 2>&1
  LOG_LEN=$(docker logs carla-server 2>&1 | wc -l)
  echo "[诊断] 日志行数: $LOG_LEN （若为 0 则 entrypoint 可能未执行或立即失败）"
  echo ""

  echo "===== 2. 容器状态 (docker inspect) ====="
  docker inspect carla-server 2>/dev/null | grep -E '"Status"|"Running"|"RestartCount"|"OOMKilled"|"ExitCode"|"Error"|"Entrypoint"|"Cmd"' | head -20
  echo ""

  echo "===== 3. entrypoint 文件检查（挂载的 /workspace/entrypoint.sh） ====="
  docker exec carla-server ls -la /workspace/entrypoint.sh 2>&1 || echo "[FAIL] 文件不存在或不可访问"
  docker exec carla-server file /workspace/entrypoint.sh 2>&1 || true
  echo "首行（检测 CRLF，若有 ^M 则为 Windows 换行）:"
  docker exec carla-server head -1 /workspace/entrypoint.sh 2>/dev/null | cat -A || docker exec carla-server head -1 /workspace/entrypoint.sh 2>/dev/null || true
  echo ""

  echo "===== 4. 容器实际 entrypoint/cmd ====="
  docker inspect carla-server --format '{{.Config.Entrypoint}}' 2>/dev/null
  docker inspect carla-server --format '{{.Config.Cmd}}' 2>/dev/null
  echo ""

  echo "===== 5. 容器内进程列表（完整） ====="
  docker exec carla-server ps aux 2>/dev/null || echo "[FAIL] ps 执行失败"
  echo ""

  echo "===== 6. entrypoint 关键行（grep） ====="
  docker logs carla-server 2>&1 | grep -Ei "entrypoint|display|RenderOffScreen|CarlaUE4|USE_PYTHON_BRIDGE|启动.*Bridge|Bridge\]|入口脚本" || echo "(无匹配)"
  echo ""
  echo "===== 6b. Bridge 启动阶段与 Spectator 日志 ====="
  docker logs carla-server 2>&1 | grep -E "\[carla-bridge\]|\[Bridge\]|\[Spectator\]|entrypoint.*Bridge|阶段" || echo "(无匹配)"
  echo ""

  echo "===== 7. 环境变量（DISPLAY/CARLA_SHOW_WINDOW） ====="
  docker exec carla-server printenv DISPLAY 2>/dev/null || echo "DISPLAY 未设置"
  docker exec carla-server printenv CARLA_SHOW_WINDOW 2>/dev/null || echo "CARLA_SHOW_WINDOW 未设置"
  echo ""

  echo "===== 8. 宿主机挂载源文件存在性 ====="
  echo "deploy/carla/entrypoint.sh: $(test -f deploy/carla/entrypoint.sh && echo '存在' || echo '不存在')"
  echo "carla-bridge 目录: $(test -d carla-bridge && echo '存在' || echo '不存在')"
} > "$LOG_ARCHIVE" 2>&1 || true
log_ok "CARLA 日志已保存，可用于离线分析: $LOG_ARCHIVE"

# 汇总
echo -e "${CYAN}========== 验证完成 ==========${NC}"
LOG_LINES=$(docker logs carla-server 2>&1 | wc -l)
[ -z "$LOG_LINES" ] && LOG_LINES=0
if [ "$LOG_LINES" -eq 0 ]; then
  log_fail "docker logs 为空！entrypoint 可能未执行；请查看深度诊断: $LOG_ARCHIVE"
  echo "  可能原因: 1) 挂载的 entrypoint.sh 路径错误 2) 文件 CRLF 导致 bash 无法解析 3) 权限问题"
  echo "  临时回退: 在 docker-compose.carla.yml 中删除 entrypoint 行，重建镜像后重试"
  FAILED=1
fi
if [ $FAILED -eq 0 ]; then
  log_ok "CARLA 窗口显示配置正常；若仍无窗口，请确认宿主机有图形环境且 xhost +local:docker 已执行，并将日志文件发起排查: $LOG_ARCHIVE"
  exit 0
else
  log_fail "存在配置问题；请查看深度诊断日志: $LOG_ARCHIVE"
  exit 1
fi
