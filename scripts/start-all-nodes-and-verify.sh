#!/usr/bin/env bash
# 一键编译验证 + 启动所有远驾与仿真闭环节点，并判断各环节节点启动是否正常。
# 先对 Backend / Vehicle / Client-Dev 做编译验证（复用 parallel-compile-verify.sh 的镜像缓存），
# 再启动 Compose + CARLA 等节点并检查状态；功能验证请在远驾客户端界面中操作。
#
# 用法：
#   ./scripts/start-all-nodes-and-verify.sh           # 编译验证 → 启动全部 → 检查状态，可选自动弹出客户端
#   SKIP_CARLA=1 ./scripts/start-all-nodes-and-verify.sh   # 不启动 CARLA
#   SKIP_COMPILE=1 ./scripts/start-all-nodes-and-verify.sh  # 跳过编译验证，直接启动并检查（镜像已就绪时加速）
#
# 阶段 0：检测并关闭已运行节点，避免端口/资源冲突。
# 阶段 1：编译验证（Backend / Vehicle / Client-Dev），使用 *:compile-cache 缓存，失败则退出。
# 阶段 2：启动所有节点（start-all-nodes.sh）。
# 阶段 3：检查各节点状态；可选启动客户端。
#
# 环境变量：与 start-all-nodes.sh 一致（CARLA_MAP、SKIP_CARLA、CARLA_BRIDGE_IN_CONTAINER=1 等）；另：
#   NO_CLIENT          1=不打印客户端步骤、不自动启动客户端
#   AUTO_START_CLIENT  1=节点检查后自动在容器内启动远驾客户端（默认）；0=仅打印启动命令
#   SKIP_COMPILE       1=跳过编译验证阶段，直接启动并检查（默认 0=先编译验证）

clear

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# shellcheck disable=SC1091
source "$SCRIPT_DIR/lib/teleop-logs.sh"
teleop_logs_init

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml"
COMPOSE_CARLA="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
# 阶段 0 停全链路时需包含 dev（Backend 用 dev 启动），确保 Backend 等一并 down
COMPOSE_DOWN="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml -f docker-compose.carla.yml"
log_section() { echo -e "${CYAN}[LOG] $*${NC}"; }
log_ok()     { echo -e "${GREEN}[OK] $*${NC}"; }
log_warn()   { echo -e "${YELLOW}[WARN] $*${NC}"; }
log_fail()   { echo -e "${RED}[FAIL] $*${NC}"; }

# 确保 CARLA + Bridge 在容器内运行（与 start-all-nodes.sh 默认一致）
export CARLA_BRIDGE_IN_CONTAINER="${CARLA_BRIDGE_IN_CONTAINER:-1}"

echo -e "${CYAN}========== 一键编译验证 + 启动所有节点 ==========${NC}"
echo -e "${CYAN}（先编译验证 Backend/Vehicle/Client-Dev，再启动节点并检查；功能验证请在远驾客户端界面中操作）${NC}"
echo ""

# ---------- 阶段 0: 检测并关闭已运行的节点，避免端口/资源冲突 ----------
log_section "阶段 0: 检测是否已有本项目的节点在运行"
RUNNING_COMPOSE=0
RUNNING_CARLA=0
if $COMPOSE ps 2>/dev/null | grep -q "Up"; then
  RUNNING_COMPOSE=1
fi
if docker ps --format '{{.Names}}' 2>/dev/null | grep -qE "carla-server|remote-driving.*carla"; then
  RUNNING_CARLA=1
fi
if [ "$RUNNING_COMPOSE" = "1" ] || [ "$RUNNING_CARLA" = "1" ]; then
  log_warn "检测到已有节点在运行，先关闭整条链路（含 Backend/Vehicle/Client-dev/CARLA）再启动"
  log_section "执行: $COMPOSE_DOWN down --remove-orphans（停止全栈，含 Backend dev）"
  $COMPOSE_DOWN down --remove-orphans 2>/dev/null || true
  log_ok "Compose 全栈已停止"
  if docker ps --format '{{.Names}}' 2>/dev/null | grep -qE "carla-server|remote-driving.*carla"; then
    docker stop carla-server 2>/dev/null || true
    docker rm -f carla-server 2>/dev/null || true
    log_ok "CARLA 容器已停止（若存在）"
  fi
  log_section "等待 5s 释放端口与资源"
  sleep 5
else
  log_ok "未检测到已运行的节点，直接启动"
fi
echo ""

# ---------- 阶段 1: 编译验证（Backend / Vehicle / Client-Dev），复用镜像缓存 ----------
if [ "${SKIP_COMPILE:-0}" = "1" ]; then
  log_section "阶段 1/3: 跳过编译验证（SKIP_COMPILE=1）"
else
  log_section "阶段 1/3: 编译验证所有节点（Backend / Vehicle / Client-Dev，使用 *:compile-cache 缓存）"
  if ! bash "$SCRIPT_DIR/parallel-compile-verify.sh"; then
    log_fail "编译验证未通过，请根据上方输出修复后重试；或 SKIP_COMPILE=1 跳过编译直接启动（不推荐）"
    exit 1
  fi
  log_ok "编译验证通过，继续启动节点"
fi
echo ""

log_section "阶段 2/3: 启动所有节点（Compose + CARLA+Bridge 容器 + 车端等）"
echo ""

# 由宿主机当前 DISPLAY 作为唯一真相；CARLA 与远驾客户端必须共用同一 DISPLAY
if [ -z "$DISPLAY" ]; then
  export DISPLAY=:0
  echo -e "${GREEN}[OK] DISPLAY 未设置，默认使用 :0（单显示器场景）${NC}"
else
  echo -e "${GREEN}[OK] 使用宿主机 DISPLAY=$DISPLAY（CARLA 与客户端共用同一显示器）${NC}"
fi
# 校验 X11 socket 存在（DISPLAY=:1 则需 /tmp/.X11-unix/X1）
X_NUM="${DISPLAY#*:}"; X_NUM="${X_NUM%%.*}"
if [ -n "$X_NUM" ] && [ ! -S "/tmp/.X11-unix/X${X_NUM}" ]; then
  log_warn "X11 socket /tmp/.X11-unix/X${X_NUM} 不存在；CARLA 窗口可能无法显示。请在有图形桌面的终端运行本脚本（echo \$DISPLAY 应为 :0 或 :1）"
fi
# xhost 与确认 DISPLAY；供 CARLA 仿真窗口与客户端显示
if [ -f "$SCRIPT_DIR/setup-host-for-client.sh" ]; then
  . "$SCRIPT_DIR/setup-host-for-client.sh" 2>/dev/null || true
fi
# 显式传递 CARLA_SHOW_WINDOW=1，确保 compose 启动 CARLA 时显示窗口
export CARLA_SHOW_WINDOW="${CARLA_SHOW_WINDOW:-1}"

# 启动所有节点（全在容器内）；CARLA 与远驾客户端将使用同一 DISPLAY
NO_CLIENT=1 bash "$SCRIPT_DIR/start-all-nodes.sh"
echo ""

# ---------- 阶段 3: 判断各环节节点启动是否正常（并行检查，再按顺序输出结果） ----------
log_section "阶段 3/3: 检查各环节节点启动是否正常（并行检查）"
echo ""

# 稍等容器状态稳定后再检查，避免 vehicle 等刚启动时被误判为未运行
# Backend 已在 start-all-nodes 阶段 2 等待就绪，此处无需额外等待
sleep 5

VERIFY_DIR="${TMPDIR:-/tmp}/rd-verify-$$"
mkdir -p "$VERIFY_DIR"
cleanup_verify() { rm -rf "$VERIFY_DIR"; }
trap cleanup_verify EXIT

FAILED=0
# 并行：Compose 服务状态（服务名与 docker-compose.yml 一致）；vehicle 在 SKIP_VEHICLE=1 时可选
# 与 start-all-nodes 一致，使用 COMPOSE_BACKEND 确保 backend 端口 8081 正确
COMPOSE_PS="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml"
for svc in teleop-postgres teleop-mosquitto keycloak zlmediakit backend client-dev; do
  ( $COMPOSE_PS ps "$svc" 2>/dev/null | grep -q "Up"; echo $? > "$VERIFY_DIR/$svc" ) &
done
if [ "${SKIP_VEHICLE:-0}" != "1" ]; then
  ( $COMPOSE_PS ps vehicle 2>/dev/null | grep -q "Up"; echo $? > "$VERIFY_DIR/vehicle" ) &
else
  echo "0" > "$VERIFY_DIR/vehicle"
fi
# 并行：Backend(8081) / Keycloak(8080) / ZLM(80) / CARLA
( curl -sf http://127.0.0.1:8081/health >/dev/null 2>&1; echo $? > "$VERIFY_DIR/backend_health" ) &
( curl -sf http://127.0.0.1:8080/health/ready >/dev/null 2>&1; echo $? > "$VERIFY_DIR/keycloak_health" ) &
( curl -sf http://127.0.0.1:80/index/api/getServerConfig >/dev/null 2>&1; echo $? > "$VERIFY_DIR/zlm" ) &
if [ "${SKIP_CARLA:-0}" != "1" ]; then
  ( docker ps --format '{{.Names}}' 2>/dev/null | grep -q "carla-server"; echo $? > "$VERIFY_DIR/carla" ) &
else
  echo "0" > "$VERIFY_DIR/carla"
fi
wait

# 按顺序输出结果
for svc in teleop-postgres teleop-mosquitto keycloak zlmediakit backend client-dev; do
  if [ "$(cat "$VERIFY_DIR/$svc" 2>/dev/null)" = "0" ]; then
    log_ok "$svc 已启动"
  else
    log_fail "$svc 未运行"
    FAILED=1
  fi
done
if [ "${SKIP_VEHICLE:-0}" = "1" ]; then
  log_ok "vehicle 已跳过（SKIP_VEHICLE=1）"
else
  if [ "$(cat "$VERIFY_DIR/vehicle" 2>/dev/null)" = "0" ]; then
    log_ok "vehicle 已启动"
  else
    log_fail "vehicle 未运行"
    FAILED=1
  fi
fi
if [ "$(cat "$VERIFY_DIR/backend_health" 2>/dev/null)" = "0" ]; then
  log_ok "Backend(8081) 健康检查通过"
else
  log_fail "Backend(8081) 未就绪"
  FAILED=1
fi
if [ "$(cat "$VERIFY_DIR/keycloak_health" 2>/dev/null)" = "0" ]; then
  log_ok "Keycloak(8080) 健康检查通过"
else
  log_fail "Keycloak(8080) 未就绪"
  FAILED=1
fi
if [ "$(cat "$VERIFY_DIR/zlm" 2>/dev/null)" = "0" ]; then
  log_ok "ZLM(80) 可访问"
else
  log_fail "ZLM(80) 未就绪"
  FAILED=1
fi
if [ "${SKIP_CARLA:-0}" != "1" ]; then
  if [ "$(cat "$VERIFY_DIR/carla" 2>/dev/null)" = "0" ]; then
    log_ok "CARLA 容器(carla-server) 已启动"
    # 验证 CARLA 窗口显示配置（DISPLAY、X11、entrypoint 模式）；失败则标记并给出修复指引
    if [ -f "$SCRIPT_DIR/verify-carla-window-display.sh" ]; then
      if bash "$SCRIPT_DIR/verify-carla-window-display.sh" 2>&1; then
        log_ok "CARLA 窗口显示配置验证通过"
      else
        log_warn "CARLA 窗口显示验证未通过；若无仿真窗口请执行："
        echo -e "  ${YELLOW}1) 宿主机执行: xhost +local:docker${NC}"
        echo -e "  ${YELLOW}2) 确认 DISPLAY 与宿主机一致（echo \$DISPLAY，多为 :0 或 :1）${NC}"
        echo -e "  ${YELLOW}3) 停止 CARLA 后重跑: docker stop carla-server; bash scripts/start-all-nodes-and-verify.sh${NC}"
        FAILED=1
      fi
    fi
  else
    log_warn "CARLA 容器未运行（SKIP_CARLA=1 可跳过）"
    FAILED=1
  fi
fi
echo ""

# 2.5 管理页可访问性检查：Backend 使用 dev 时端口 8081
log_section "检查管理页 http://localhost:8081/admin/add-vehicle"
ADMIN_CODE=$(curl -sf -o /dev/null -w "%{http_code}" http://127.0.0.1:8081/admin/add-vehicle 2>/dev/null || echo "000")
if [ "$ADMIN_CODE" = "200" ]; then
  log_ok "管理页可访问 (HTTP 200)"
else
  log_fail "管理页当前返回 HTTP ${ADMIN_CODE}（期望 200），可能为旧 backend 镜像无该路由"
  log_section "尝试用 dev compose 重建 backend 并重启"
  if docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml build backend 2>&1 | sed 's/^/    /'; then
    log_section "重启 backend 使新镜像生效"
    docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml up -d backend 2>&1 | sed 's/^/    /'
    sleep 8
    ADMIN_CODE2=$(curl -sf -o /dev/null -w "%{http_code}" http://127.0.0.1:8081/admin/add-vehicle 2>/dev/null || echo "000")
    if [ "$ADMIN_CODE2" = "200" ]; then
      log_ok "管理页已可访问 (HTTP 200)"
    else
      log_fail "管理页仍返回 HTTP ${ADMIN_CODE2}，请依据下方 backend 日志排查"
      log_section "Backend 最近日志（依据日志判断：未匹配=无路由需重建；add-vehicle.html 可读=否=未挂载 static）"
      docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml logs --tail=25 backend 2>/dev/null | sed 's/^/    /' || docker logs remote-driving-backend-1 --tail=25 2>/dev/null | sed 's/^/    /'
    fi
  else
    log_fail "backend 构建失败，请检查上方构建输出；管理页暂不可用"
  fi
fi
echo ""

if [ $FAILED -eq 1 ]; then
  log_warn "部分节点未就绪，请根据上方排查；可稍后重跑本脚本或手动检查: $COMPOSE ps"
fi
echo ""

# 3. 启动远驾客户端（容器内）：自动弹出或仅打印命令
if [ "${NO_CLIENT:-0}" != "1" ]; then
  echo -e "${CYAN}========== 远驾客户端：请在界面中操作并完成功能验证 ==========${NC}"
  echo ""
  # 固化运行环境：宿主机允许 Docker 连接 X11，避免客户端无界面（仅需执行一次，见 docs/RUN_ENVIRONMENT.md）
  if [ -f "$SCRIPT_DIR/setup-host-for-client.sh" ]; then
    bash "$SCRIPT_DIR/setup-host-for-client.sh" 2>/dev/null || true
    echo ""
  fi

  AUTO_START_CLIENT="${AUTO_START_CLIENT:-1}"
  # 启动时显示登录界面；日志通过 CLIENT_LOG_FILE 写入容器内文件，闪退后可查看
  # 容器内无宿主机 NVIDIA 驱动时强制软件渲染，避免 libGL failed to load driver: nvidia-drm 导致无法启动
  # 注意：构建在子 shell 中 cd 后退出会丢失 cwd，故最后必须显式 cd /tmp/client-build 再执行二进制
  # FORCE_REBUILD_CLIENT=1 时清理构建目录，确保 mqttcontroller 等修改被重新编译
  CLIENT_RUN="(cd /tmp/client-build 2>/dev/null && true) || (mkdir -p /tmp/client-build); if [ \"\${FORCE_REBUILD_CLIENT:-0}\" = \"1\" ]; then rm -rf /tmp/client-build/CMakeCache.txt /tmp/client-build/CMakeFiles /tmp/client-build/*.o /tmp/client-build/Makefile 2>/dev/null; fi; cd /tmp/client-build && (test -f CMakeCache.txt || cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug) && make -j4 && ./RemoteDrivingClient --reset-login"
  CLIENT_LOG_PATH="/workspace/logs/client-${TELEOP_LOG_DATE}.log"
  # 与宿主机 DISPLAY 一致才能连接 X11（宿主机 :1 则用 :1）；可显式覆盖：CLIENT_DISPLAY=:0
  DISPLAY_VAL="${CLIENT_DISPLAY:-${DISPLAY:-:0}}"
  # 容器内：Backend 用 backend:8080，MQTT 用 teleop-mosquitto:1883（与 compose 服务名一致）
  # GL 栈由客户端 main 内 ClientDisplayRuntimePolicy 自动选择（glxinfo / nvidia-smi），不再强制 LIBGL_ALWAYS_SOFTWARE
  CLIENT_ENV="-e DISPLAY=$DISPLAY_VAL -e DEFAULT_SERVER_URL=http://backend:8080 -e ZLM_VIDEO_URL=http://zlmediakit:80 -e MQTT_BROKER_URL=mqtt://teleop-mosquitto:1883 -e CLIENT_LOG_FILE=$CLIENT_LOG_PATH"
  # 在 bash -c 内显式 export DISPLAY，确保子进程一定使用指定显示（覆盖容器默认）
  CLIENT_RUN_WITH_DISPLAY="export DISPLAY=$DISPLAY_VAL; $CLIENT_RUN"

  if [ "$AUTO_START_CLIENT" = "1" ] && [ -n "$DISPLAY_VAL" ]; then
    log_section "在 client-dev 容器内启动远驾客户端（界面将弹出，日志写入容器内文件）"
    # 先结束容器内已有客户端进程，避免误判“已运行”且与本次编译输出顺序错乱
    $COMPOSE exec -T client-dev pkill -f RemoteDrivingClient 2>/dev/null || true
    sleep 1
    # 后台运行并同时把编译/客户端输出打到终端和日志（2>&1 | tee）；编译进度会实时出现在下方
    $COMPOSE exec -T $CLIENT_ENV client-dev bash -c "$CLIENT_RUN_WITH_DISPLAY 2>&1 | tee $CLIENT_LOG_PATH" &
    CLIENT_EXEC_PID=$!
    log_ok "已启动编译/客户端（输出将实时显示在下方），等待编译完成且进程就绪。"
    echo -e "  ${CYAN}编译进度与客户端日志同时写入容器内: $CLIENT_LOG_PATH（宿主机: ${PROJECT_ROOT}/logs/client-${TELEOP_LOG_DATE}.log）${NC}"
    log_section "等待编译/启动完成（最多 240s，每 5s 检查；下方会持续输出编译进度）"
    CLIENT_WAIT_MAX=240
    CLIENT_WAIT_INTERVAL=5
    ELAPSED=0
    CLIENT_UP=0
    while [ $ELAPSED -lt $CLIENT_WAIT_MAX ]; do
      if $COMPOSE exec -T client-dev bash -c "pgrep -f RemoteDrivingClient >/dev/null 2>&1"; then
        CLIENT_UP=1
        break
      fi
      # 区分：仍在编译（cmake/make）则必须等编译完成；否则为等待客户端进程启动
      if $COMPOSE exec -T client-dev bash -c "pgrep -f 'cmake' >/dev/null 2>&1 || pgrep -f ' make ' >/dev/null 2>&1 || pgrep -x 'make' >/dev/null 2>&1"; then
        echo "  [LOG] 等待编译完成（编译并行进行中）... 已等待 ${ELAPSED}s / ${CLIENT_WAIT_MAX}s"
      else
        echo "  [LOG] 编译已完成，等待客户端启动... 已等待 ${ELAPSED}s / ${CLIENT_WAIT_MAX}s"
      fi
      sleep $CLIENT_WAIT_INTERVAL
      ELAPSED=$((ELAPSED + CLIENT_WAIT_INTERVAL))
    done
    if [ "$CLIENT_UP" = "1" ]; then
      log_ok "客户端进程已运行（容器内 DISPLAY=$DISPLAY_VAL）。"
        if $COMPOSE exec -T client-dev bash -c "grep -iE 'cannot open display|unable to open display|failed to connect.*display|could not connect to display' $CLIENT_LOG_PATH 2>/dev/null" | head -1 | grep -q .; then
          log_warn "日志中检测到无法连接 DISPLAY。请执行: xhost +local:docker；确保运行脚本的终端 DISPLAY 与显示一致（:0 或 :1），必要时 export DISPLAY=:1 后重跑"
      else
        echo -e "  ${CYAN}若窗口未弹出：执行 xhost +local:docker；DISPLAY 需与宿主机一致（当前容器 DISPLAY=$DISPLAY_VAL）${NC}"
      fi
    else
      log_warn "超时未检测到客户端进程（可能编译失败或启动后闪退），请查看下方日志并建议手动运行以查看实时报错。"
      echo -e "  ${CYAN}容器内客户端日志最后 50 行：${NC}"
      $COMPOSE exec -T client-dev tail -n 50 "$CLIENT_LOG_PATH" 2>/dev/null | sed 's/^/    /' || echo "    （无日志或文件不存在）"
      echo ""
      echo -e "  ${YELLOW}请按顺序尝试：${NC}"
      echo "    1) 宿主机执行: xhost +local:docker；DISPLAY 与当前终端一致（echo \$DISPLAY，多为 :0 或 :1）"
      echo "    2) 若报 Qt platform plugin xcb / libxcb-cursor0：需重建 client-dev 镜像后重跑：bash scripts/build-client-dev-full-image.sh"
      echo "    3) 手动启动（可看到编译/运行报错）："
      echo "  $COMPOSE exec -it $CLIENT_ENV client-dev bash -c '$CLIENT_RUN_WITH_DISPLAY 2>&1 | tee -a $CLIENT_LOG_PATH'"
    fi
  else
    echo -e "${GREEN}启动远驾客户端（容器内，日志同时打终端并写入文件）：${NC}"
    echo "  $COMPOSE exec -it $CLIENT_ENV client-dev bash -c '$CLIENT_RUN_WITH_DISPLAY 2>&1 | tee -a $CLIENT_LOG_PATH'"
  fi
  echo ""
  echo -e "${GREEN}在客户端界面中：${NC}"
  echo "  1) 登录：服务器地址已默认为 http://backend:8080（容器内），使用 e2e-test / e2e-test-password 或 123/123"
  echo "  2) 选车：选择「carla-sim-001」进入仿真，或「E2ETESTVIN0000001」进入车端推流"
  echo "  3) 连接车端：点击顶栏「连接车端」，等待约 25s 出现四路画面（CARLA 仿真需 Bridge 先推流）"
  echo "  4) 远驾接管：点击「远驾接管」按钮，启用对仿真/车端车辆的控制"
  echo "  5) 控制车辆：键盘（或方向盘/踏板）控制油门、刹车、转向，在界面查看实时速度与底盘数据"
  echo ""
  echo -e "${CYAN}增加车辆：浏览器打开 http://localhost:8081/admin/add-vehicle（需 JWT，详见 docs/ADD_VEHICLE_GUIDE.md）${NC}"
  echo ""
  if [ "${CARLA_BRIDGE_IN_CONTAINER}" = "1" ]; then
    echo -e "${CYAN}CARLA/Bridge 日志: $COMPOSE_CARLA logs -f carla${NC}"
    echo -e "${CYAN}若客户端 stream not found：先跑 ./scripts/verify-carla-stream-chain.sh；再查: docker logs carla-server 2>&1 | grep -E '视频源|start_stream|已置 streaming|startPushers|spawn_cameras|推流目标'${NC}"
    echo -e "${CYAN}CARLA 默认显示仿真窗口；无头模式：CARLA_SHOW_WINDOW=0 后重跑${NC}"
  else
    echo -e "${CYAN}Bridge 日志: tail -f $PROJECT_ROOT/carla-bridge.log${NC}"
  fi
  echo ""
fi
