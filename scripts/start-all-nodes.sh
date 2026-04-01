#!/usr/bin/env bash
# 一键启动所有节点：Compose 全链路 + CARLA 仿真（含场景/地图）+ CARLA Bridge。
#
# 用法：
#   ./scripts/start-all-nodes.sh              # 启动全部（含 CARLA Town01 + bridge 后台）
#   CARLA_MAP=Town02 ./scripts/start-all-nodes.sh   # 使用 Town02 场景
#   SKIP_CARLA=1 ./scripts/start-all-nodes.sh       # 不启动 CARLA 与 bridge（仅 Compose）
#   SKIP_BRIDGE=1 ./scripts/start-all-nodes.sh     # 启动 CARLA 但不启动 bridge（需手动运行）
#
# 环境变量：
#   CARLA_MAP      CARLA 场景/地图（如 Town01、Town02），默认 Town01
#   CARLA_IMAGE    仅当 CARLA_BRIDGE_IN_CONTAINER=0 时使用，默认 carlasim/carla:latest
#   CARLA_BRIDGE_IN_CONTAINER  1=在 CARLA 容器内运行 Bridge（推荐，默认）；0=宿主机运行 Bridge
#   SKIP_CARLA     1=不启动 CARLA 与 bridge
#   SKIP_BRIDGE    1=仅当宿主机 Bridge 时有效；容器内 Bridge 时无效
#   SKIP_VEHICLE   1=不启动 vehicle（无 Vehicle-side 源码或仅测 Backend/Client 时使用）
#   NO_CLIENT     1=不打印启动客户端命令
# Backend 使用 dev compose 挂载源码，与 parallel-compile-verify 同款镜像，宿主机端口 8081（与 Client 默认一致）。
# 容器内 Bridge 时：直接使用 carlasim/carla:latest，挂载 entrypoint + carla-bridge + carla-src，无需构建自定义镜像。

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml"
# Backend 使用 dev compose：挂载源码，使用 parallel-compile-verify 同款镜像，端口 8081（与 Client 默认一致）
COMPOSE_BACKEND="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml"
COMPOSE_CARLA="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml"
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

CARLA_MAP="${CARLA_MAP:-Town01}"
CARLA_IMAGE="${CARLA_IMAGE:-carlasim/carla:latest}"
CARLA_CONTAINER_NAME="${CARLA_CONTAINER_NAME:-carla-server}"
CARLA_BRIDGE_IN_CONTAINER="${CARLA_BRIDGE_IN_CONTAINER:-1}"
BRIDGE_LOG="${PROJECT_ROOT}/carla-bridge.log"

# 将 Town01 转为 UE 路径 /Game/Maps/Town01
carla_map_to_ue_path() {
  local m="$1"
  if [ -z "$m" ]; then
    echo ""
    return
  fi
  if [[ "$m" == /* ]]; then
    echo "$m"
    return
  fi
  echo "/Game/Maps/$m"
}

log_section() { echo -e "${CYAN}[LOG] $*${NC}"; }
log_ok()     { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_warn()   { echo -e "  ${YELLOW}[WARN] $*${NC}"; }
log_fail()   { echo -e "  ${YELLOW}[FAIL] $*${NC}"; }

echo -e "${CYAN}========== 一键启动所有节点 ==========${NC}"
log_section "工作目录: $PROJECT_ROOT"
echo ""

# 确保 teleop-network 存在（vehicle.dev 与 carla 使用此网络）
docker network create teleop-network 2>/dev/null || true

# ---------- 1. 启动 Compose 全链路 ----------
# 服务名与 docker-compose.yml 一致：teleop-postgres, keycloak, teleop-mosquitto, zlmediakit, backend, vehicle, client-dev
echo -e "${CYAN}[1/4] 启动 Compose（Postgres / Keycloak / Mosquitto / ZLM / Backend / Vehicle / Client-dev）${NC}"
log_section "执行: $COMPOSE up -d teleop-postgres teleop-mosquitto zlmediakit"
$COMPOSE up -d --remove-orphans teleop-postgres teleop-mosquitto zlmediakit 2>/dev/null || true
sleep 4
log_section "执行: $COMPOSE up -d keycloak（使用 teleop 库，无需单独创建 keycloak 库）"
$COMPOSE up -d --remove-orphans keycloak 2>/dev/null || true
sleep 3
# migrations + seed 由 docker-compose 的 db-init 服务自动执行，无需手动运行 ensure-seed-data
# 强制删除旧 backend 容器，确保 dev 配置（8081:8080、static 挂载）生效
docker rm -f remote-driving-backend-1 2>/dev/null || true
log_section "执行: $COMPOSE_BACKEND up -d backend（使用 dev 挂载源码，端口 8081:8080）"
$COMPOSE_BACKEND up -d --force-recreate --remove-orphans backend 2>/dev/null || true
sleep 2
# 必须用 COMPOSE_BACKEND（含 dev），否则 backend 会被 2-file 配置覆盖为 8000，导致 8081 不可用
# Vehicle 可选：无 Vehicle-side 源码或不需要车端时可 SKIP_VEHICLE=1 跳过
if [ "${SKIP_VEHICLE:-0}" = "1" ]; then
  log_section "跳过 vehicle（SKIP_VEHICLE=1），仅启动 client-dev"
  if ! docker image inspect remote-driving-client-dev:full >/dev/null 2>&1; then
    log_warn "未找到镜像 remote-driving-client-dev:full，请先构建： bash scripts/build-client-dev-full-image.sh"
  fi
  $COMPOSE_BACKEND up -d --no-build --remove-orphans client-dev 2>/dev/null || true
else
  log_section "执行: $COMPOSE_BACKEND up -d --no-build vehicle client-dev（使用 dev 保持 backend 8081 配置）"
  if ! docker image inspect remote-driving-client-dev:full >/dev/null 2>&1; then
    log_warn "未找到镜像 remote-driving-client-dev:full，请先构建： bash scripts/build-client-dev-full-image.sh"
  fi
  $COMPOSE_BACKEND up -d --no-build --remove-orphans vehicle client-dev 2>/dev/null || true
fi
log_section "Compose 服务状态:"
$COMPOSE_BACKEND ps 2>/dev/null | sed 's/^/    /' || true
log_ok "Compose 服务已 up"
echo ""

# ---------- 2. 等待基础服务就绪 ----------
# Keycloak 首次启动约 60~120s；Backend 使用 dev 时需容器内编译（首次约 30~90s）
echo -e "${CYAN}[2/4] 等待基础服务就绪（最多 120s，Keycloak/Backend 首次启动需时）${NC}"
for i in $(seq 1 60); do
  if curl -sf http://127.0.0.1:8080/health/ready >/dev/null 2>&1 && \
     curl -sf http://127.0.0.1:8081/health >/dev/null 2>&1 && \
     curl -sf http://127.0.0.1:80/index/api/getServerConfig >/dev/null 2>&1; then
    log_ok "Keycloak(8080)、Backend(8081)、ZLM(80) 已就绪（第 ${i} 次探测）"
    break
  fi
  [ $i -eq 60 ] && log_fail "超时后部分服务未就绪；排查: curl -s http://127.0.0.1:8080/health/ready; curl -s http://127.0.0.1:8081/health; $COMPOSE_BACKEND logs keycloak backend | tail -30"
  sleep 2
done
echo ""

# ---------- 3. 启动 CARLA（含场景）；4. Bridge 在容器内或宿主机 ----------
# 容器内 Bridge 时使用「已构建镜像」remote-driving/carla-with-bridge:latest；默认 USE_PYTHON_BRIDGE=1（CARLA 视频）
if [ "${SKIP_CARLA:-0}" != "1" ]; then
  if [ "$CARLA_BRIDGE_IN_CONTAINER" = "1" ]; then
    export USE_PYTHON_BRIDGE="${USE_PYTHON_BRIDGE:-1}"
    echo -e "${CYAN}[3/4] 启动 CARLA + Bridge（USE_PYTHON_BRIDGE=${USE_PYTHON_BRIDGE}，1=CARLA 视频 / 0=testsrc，场景: ${CARLA_MAP}）${NC}"
    export CARLA_MAP
    if ! docker image inspect remote-driving/carla-with-bridge:latest >/dev/null 2>&1; then
      CARLA_TAR="${CARLA_IMAGE_TAR:-$PROJECT_ROOT/deploy/carla/carla-with-bridge-latest.tar}"
      if [ -f "$CARLA_TAR" ]; then
        log_section "未找到本地镜像，从已保存文件加载: $CARLA_TAR"
        if docker load -i "$CARLA_TAR" 2>/dev/null; then
          log_ok "已加载镜像 remote-driving/carla-with-bridge:latest"
        else
          log_section "加载失败，改为构建 CARLA 镜像（约需数分钟）..."
          $COMPOSE_CARLA build carla 2>&1 | tail -20
        fi
      else
        log_section "未找到已构建镜像且无保存文件，先构建 CARLA 镜像（约需数分钟）..."
        $COMPOSE_CARLA build carla 2>&1 | tail -20
      fi
    fi
    if docker image inspect remote-driving/carla-with-bridge:latest >/dev/null 2>&1; then
      log_section "使用已构建镜像 remote-driving/carla-with-bridge:latest，启动容器"
      # 显式导出供 compose 替换 ${DISPLAY} ${CARLA_SHOW_WINDOW}，确保 CARLA 显示仿真窗口
      export DISPLAY="${DISPLAY:-:0}"
      export CARLA_SHOW_WINDOW="${CARLA_SHOW_WINDOW:-1}"
      log_section "CARLA 窗口: DISPLAY=$DISPLAY CARLA_SHOW_WINDOW=$CARLA_SHOW_WINDOW（宿主机需 xhost +local:docker）"
      CARLA_OUTPUT="$($COMPOSE_CARLA up -d carla 2>&1)" || true
      if docker ps --format '{{.Names}}' | grep -q "carla-server"; then
        log_ok "CARLA 已启动，容器: carla-server"
        log_section "等待 8s 后抓取 Bridge 启动日志（用于确认视频源与 MQTT）..."
        sleep 8
        BRIDGE_SUMMARY="$(docker logs carla-server 2>&1 | tail -120 | grep -E "视频源|USE_PYTHON_BRIDGE|启动 CARLA Bridge|环节: MQTT|环节: 已订阅|Bridge\] 视频源|Python Bridge 主流程|start_stream|startPushers|spawn_cameras" | tail -25)"
        if [ -n "$BRIDGE_SUMMARY" ]; then
          echo -e "${CYAN}  [Bridge 日志摘要]${NC}"
          echo "$BRIDGE_SUMMARY" | sed 's/^/    /'
        else
          log_warn "未抓到 Bridge 关键字；完整日志: docker logs carla-server 2>&1 | tail -80"
        fi
      else
        log_fail "CARLA 容器启动失败"
        if echo "$CARLA_OUTPUT" | grep -qi "nvidia\|runtime"; then
          log_section "宿主机缺少 NVIDIA Container Toolkit，容器无法使用 GPU。请安装后重启 Docker："
          echo "    sudo ./scripts/install-nvidia-container-toolkit.sh"
          echo "  文档: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html"
        fi
        log_section "完整输出: $COMPOSE_CARLA up -d carla 2>&1"
        echo "$CARLA_OUTPUT" | tail -15 | sed 's/^/    /' || true
      fi
    else
      log_fail "CARLA 镜像构建失败或未就绪"
      log_section "请先单独构建: ./scripts/build-carla-image.sh"
    fi
  else
    # 旧方式：仅启动 CARLA 容器（docker run），Bridge 在宿主机运行
    echo -e "${CYAN}[3/4] 启动 CARLA 仿真（场景: ${CARLA_MAP}）${NC}"
    UE_MAP=$(carla_map_to_ue_path "$CARLA_MAP")
    if docker ps --format '{{.Names}}' | grep -q "^${CARLA_CONTAINER_NAME}$"; then
      log_section "容器 ${CARLA_CONTAINER_NAME} 已在运行，跳过启动"
    else
      if docker ps -a --format '{{.Names}}' | grep -q "^${CARLA_CONTAINER_NAME}$"; then
        log_section "启动已有容器 ${CARLA_CONTAINER_NAME}"
        docker start "$CARLA_CONTAINER_NAME" 2>/dev/null && log_ok "已启动已有容器 ${CARLA_CONTAINER_NAME}" || true
      else
        CARLA_CMD="bash CarlaUE4.sh -RenderOffScreen -nosound"
        [ -n "$UE_MAP" ] && CARLA_CMD="bash CarlaUE4.sh $UE_MAP -RenderOffScreen -nosound"
        if docker run -d --name "$CARLA_CONTAINER_NAME" \
          -p 2000-2002:2000-2002 \
          --runtime=nvidia \
          -e NVIDIA_VISIBLE_DEVICES="${NVIDIA_VISIBLE_DEVICES:-0}" \
          -e NVIDIA_DRIVER_CAPABILITIES=all \
          "$CARLA_IMAGE" \
          $CARLA_CMD 2>/dev/null; then
          :
        else
          log_section "尝试无地图参数启动 CARLA"
          docker rm -f "$CARLA_CONTAINER_NAME" 2>/dev/null || true
          docker run -d --name "$CARLA_CONTAINER_NAME" \
            -p 2000-2002:2000-2002 \
            --runtime=nvidia \
            -e NVIDIA_VISIBLE_DEVICES="${NVIDIA_VISIBLE_DEVICES:-0}" \
            -e NVIDIA_DRIVER_CAPABILITIES=all \
            "$CARLA_IMAGE" \
            bash CarlaUE4.sh -RenderOffScreen -nosound 2>/dev/null || true
        fi
        if docker ps --format '{{.Names}}' | grep -q "^${CARLA_CONTAINER_NAME}$"; then
          log_ok "CARLA 已启动（地图: ${UE_MAP:-默认}）"
        else
          log_fail "CARLA 启动失败（可能缺 nvidia runtime 或镜像无 CarlaUE4.sh）"
          log_section "排查: 安装 nvidia-container-toolkit 后重试，或 SKIP_CARLA=1 跳过仿真"
        fi
      fi
    fi
    echo ""
    # ---------- 4. 宿主机 Bridge ----------
    if [ "${SKIP_BRIDGE:-0}" != "1" ]; then
      echo -e "${CYAN}[4/4] 启动 CARLA Bridge（宿主机后台，日志: ${BRIDGE_LOG}）${NC}"
      (
        export CARLA_HOST="${CARLA_HOST:-127.0.0.1}"
        export MQTT_BROKER="${MQTT_BROKER:-127.0.0.1}"
        export ZLM_HOST="${ZLM_HOST:-127.0.0.1}"
        export ZLM_RTMP_PORT="${ZLM_RTMP_PORT:-1935}"
        [ -n "$CARLA_MAP" ] && export CARLA_MAP
        cd "$PROJECT_ROOT/carla-bridge"
        nohup python3 carla_bridge.py >> "$BRIDGE_LOG" 2>&1 &
        echo $! > "$PROJECT_ROOT/carla-bridge.pid"
      )
      sleep 2
      if [ -f "$PROJECT_ROOT/carla-bridge.pid" ] && kill -0 "$(cat "$PROJECT_ROOT/carla-bridge.pid")" 2>/dev/null; then
        log_ok "CARLA Bridge 已在宿主机后台运行"
      else
        log_fail "Bridge 可能未启动成功"
        tail -15 "$BRIDGE_LOG" 2>/dev/null | sed 's/^/    /' || echo "    (无日志)"
      fi
    else
      echo -e "${CYAN}[4/4] 跳过 CARLA Bridge（SKIP_BRIDGE=1）${NC}"
    fi
  fi
  echo ""
else
  echo -e "${CYAN}[3/4] 跳过 CARLA（SKIP_CARLA=1）${NC}"
  [ "$CARLA_BRIDGE_IN_CONTAINER" = "0" ] && echo -e "${CYAN}[4/4] 跳过 CARLA Bridge${NC}"
  echo ""
fi

# ---------- 汇总 ----------
echo -e "${CYAN}========== 所有节点启动完成 ==========${NC}"
log_section "各环节日志见上方 [LOG]/[OK]/[WARN]/[FAIL]，便于定位问题"
echo ""
echo "  已启动: Postgres, Keycloak, Coturn, ZLMediaKit, Backend, Mosquitto, Vehicle, Client-dev"
[ "${SKIP_CARLA:-0}" != "1" ] && echo "          CARLA（场景: ${CARLA_MAP}）$([ "$CARLA_BRIDGE_IN_CONTAINER" = "1" ] && echo "，Bridge 在容器内" || echo "，CARLA Bridge（若未跳过）")"
echo ""
echo "  验证整链: ./scripts/verify-full-chain-with-carla.sh"
[ "$CARLA_BRIDGE_IN_CONTAINER" = "1" ] && echo "  查看 CARLA/Bridge 日志: $COMPOSE_CARLA logs -f carla" || echo "  查看 Bridge 日志: tail -f $BRIDGE_LOG"
[ "$CARLA_BRIDGE_IN_CONTAINER" = "0" ] && echo "  停止宿主机 Bridge: kill \$(cat $PROJECT_ROOT/carla-bridge.pid 2>/dev/null) 2>/dev/null"
echo ""
if [ "${NO_CLIENT:-0}" != "1" ]; then
  echo -e "${GREEN}启动远程驾驶客户端：${NC}"
  echo "  $COMPOSE exec -it -e ZLM_VIDEO_URL=http://zlmediakit:80 -e MQTT_BROKER_URL=mqtt://mosquitto:1883 client-dev bash -c 'cd /tmp/client-build 2>/dev/null || (mkdir -p /tmp/client-build && cd /tmp/client-build && cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug && make -j4) && ./RemoteDrivingClient'"
  echo ""
  echo "  登录后选车 E2ETESTVIN0000001（车端）或 carla-sim-001（仿真），点击「连接车端」。"
fi
echo ""
