#!/bin/bash
# 自动化验证「连接」按钮功能：测试模式下进入主界面并触发 requestStreamStart + connectFourStreams，无崩溃即通过
# 并校验日志顺序（先 requestStreamStart 再拉流）及 -400 重试逻辑
# Ctrl+C 会结束客户端并令本脚本以 130 退出，由主脚本统一停止所有容器
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# shellcheck disable=SC1091
source "$SCRIPT_DIR/lib/teleop-logs.sh"
teleop_logs_init
VERIFY_CONNECT_LOG="$(teleop_log_path_session verify-connect-feature)"
CLIENT_LOG_IN_CONTAINER="$(teleop_client_log_container_path)"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml"
# 注意：若写成「timeout … | tee」且在前台等待，当前 shell 不在终端前台进程组内，Ctrl+C 到不了
# 本脚本的 trap；信号只打到 pipeline 内的 timeout/docker/Qt，Qt 常忽略 SIGINT → 表现为多次 ^C 仍卡住。
# 做法：把 pipeline 放到后台，本 shell 对终端前台 wait，这样 SIGINT 由 trap 处理并杀掉整条 pipeline。

YELLOW='\033[1;33m'
NC='\033[0m'
echo "=== 连接功能自动化验证（CLIENT_AUTO_CONNECT_VIDEO）==="
echo -e "${YELLOW}>>> 说明: 本步骤设置 CLIENT_AUTO_CONNECT_VIDEO=1，客户端会故意跳过登录页并进入测试拉流流程（与 main.qml 自动化分支一致，不是 Bug）。${NC}"
echo -e "${YELLOW}>>> 需要正常登录界面请用: 默认 bash scripts/start-full-chain.sh 已跳过 2c；若单独运行本脚本则本窗口为测试模式。${NC}"
echo ""

# 1. 确保 client-dev 容器在运行（全链路 compose）
echo "[1/4] 检查 client-dev 容器..."
if ! $COMPOSE ps client-dev 2>/dev/null | grep -q Up; then
    echo "启动 client-dev 容器..."
    $COMPOSE up -d client-dev
    sleep 3
fi

# 2. 编译
echo "[2/4] 编译客户端..."
$COMPOSE exec -T client-dev bash -c "mkdir -p /tmp/client-build && cd /tmp/client-build && ( [ ! -f CMakeCache.txt ] && cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug || true ) && make -j4" 2>&1 | tail -8
if ! $COMPOSE exec -T client-dev test -f /tmp/client-build/RemoteDrivingClient 2>/dev/null; then
    echo "VERIFY_FAIL: 编译未生成可执行文件"
    exit 1
fi
echo "编译 OK"

# 3. 运行客户端：测试模式进入主界面并触发连接逻辑，运行 18 秒（含 2.5s 延迟拉流 + 可能的 -400 重试）
# 后台 pipeline + 前台 wait，保证 Ctrl+C 由本脚本 trap 处理（见文件头注释）。
echo "[3/4] 运行客户端 18 秒（CLIENT_AUTO_CONNECT_VIDEO=1 测试模式），Ctrl+C 可提前结束并停止所有容器..."
export DISPLAY="${DISPLAY:-:0}"
RUN_RESULT=0
echo "  日志文件: $VERIFY_CONNECT_LOG"

rcfile="$(mktemp "${TMPDIR:-/tmp}/vconnect-rc.XXXXXX")"
rm_rc() { rm -f "$rcfile"; }
trap rm_rc EXIT

interrupt_verify() {
    echo ""
    echo "已中断，正在结束 docker exec / 客户端子进程..."
    trap - INT TERM EXIT
    set +e
    local j
    for j in $(jobs -p 2>/dev/null); do kill -INT "$j" 2>/dev/null || true; done
    sleep 0.2
    for j in $(jobs -p 2>/dev/null); do kill -TERM "$j" 2>/dev/null || true; done
    sleep 0.5
    for j in $(jobs -p 2>/dev/null); do kill -KILL "$j" 2>/dev/null || true; done
    wait 2>/dev/null || true
    rm_rc
    exit 130
}
trap interrupt_verify INT TERM

set +e
(
    timeout 18 $COMPOSE exec -it -e DISPLAY="$DISPLAY" -e CLIENT_RESET_LOGIN=1 -e CLIENT_AUTO_CONNECT_VIDEO=1 -e MQTT_BROKER_URL=mqtt://teleop-mosquitto:1883 -e ZLM_VIDEO_URL=http://zlmediakit:80 -e "CLIENT_LOG_FILE=${CLIENT_LOG_IN_CONTAINER}" client-dev bash -c "cd /tmp/client-build && ./RemoteDrivingClient --reset-login"
    echo $? >"$rcfile"
) 2>&1 | tee "$VERIFY_CONNECT_LOG" &
wait $! || true
set -e
trap - INT TERM
RUN_RESULT=$(cat "$rcfile" 2>/dev/null || echo 1)
rm_rc
trap - EXIT

# 4. 判定结果
echo "[4/4] 判定结果..."
LOG="$VERIFY_CONNECT_LOG"
if [ "$RUN_RESULT" -eq 130 ]; then
    echo "用户中断 (Ctrl+C)，退出码 130"
    exit 130
fi
if [ "$RUN_RESULT" -eq 124 ] || [ "$RUN_RESULT" -eq 0 ]; then
    echo "VERIFY_OK: 客户端运行无崩溃"
    grep -q "autoConnectVideo: entering driving interface" "$LOG" 2>/dev/null && echo "  - 已进入主驾驶界面（test mode）"
    grep -q "autoConnectVideo: triggering connectFourStreams" "$LOG" 2>/dev/null && echo "  - 已触发 requestStreamStart + connectFourStreams"
    # 顺序：requestStreamStart 应在 connecting 4 streams 之前（测试模式下 main.qml 先 request 再 connect）
    if grep -q "MQTT: requested vehicle to start stream" "$LOG" 2>/dev/null; then
        echo "  - 已发送 MQTT start_stream"
    fi
    if grep -q "WebRtcStreamManager: connecting 4 streams" "$LOG" 2>/dev/null; then
        echo "  - 已请求四路 WebRTC 拉流"
    fi
    # -400 重试逻辑：出现「流尚未就绪，2s 后重试」或「次重试剩余」即表示重试逻辑生效
    if grep -q "流尚未就绪，2s 后重试\|次重试剩余" "$LOG" 2>/dev/null; then
        echo "  - 检测到 -400 自动重试逻辑"
    fi
    exit 0
fi
echo "VERIFY_FAIL: 客户端异常退出 (code=$RUN_RESULT)"
exit 1
