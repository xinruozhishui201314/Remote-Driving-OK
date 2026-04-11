#!/bin/bash
# 客户端软件渲染启动脚本
# 用于在没有 NVIDIA GPU 或 GPU 驱动不可用时启动客户端
#
# 使用方法:
#   bash scripts/run-client-with-software-rendering.sh
#   或者在容器内直接执行:
#   docker exec -e DISPLAY=:1 -e QT_QPA_PLATFORM=xcb -e CLIENT_ASSUME_SOFTWARE_GL=1 \
#       teleop-client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login'

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}========== 客户端软件渲染模式启动 ==========${NC}"
echo ""

# 设置 X11
if [ -z "$DISPLAY" ]; then
    if [ -S /tmp/.X11-unix/X1 ]; then
        export DISPLAY=:1
    else
        export DISPLAY=:0
    fi
fi

# X11 权限
if command -v xhost &>/dev/null; then
    xhost +local:docker 2>/dev/null || true
fi

echo -e "${GREEN}DISPLAY=$DISPLAY${NC}"
echo ""

# 检查容器
if ! docker ps --format '{{.Names}}' | grep -q "client-dev"; then
    echo -e "${YELLOW}client-dev 容器未运行，正在启动...${NC}"
    docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml up -d client-dev
    sleep 3
fi

# 检查编译
if ! docker exec teleop-client-dev test -x /tmp/client-build/RemoteDrivingClient 2>/dev/null; then
    echo -e "${YELLOW}客户端未编译，正在编译...${NC}"
    docker exec teleop-client-dev bash -c 'mkdir -p /tmp/client-build && cd /tmp/client-build && cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug && make -j4'
fi

echo -e "${GREEN}启动客户端（软件渲染模式）...${NC}"
echo ""
echo -e "${YELLOW}环境变量说明:${NC}"
echo "  - QT_QPA_PLATFORM=xcb           使用 XCB 平台插件"
echo "  - CLIENT_ASSUME_SOFTWARE_GL=1   强制软件光栅栈（LIBGL_ALWAYS_SOFTWARE + glx）"
echo "  - CLIENT_ALLOW_SOFTWARE_PRESENTATION=1  绕过 Linux+xcb 默认硬件呈现门禁（与本脚本配套）"
echo ""
echo -e "${YELLOW}如果客户端窗口没有显示，请尝试:${NC}"
echo "  1. 按 Alt+Tab 切换窗口"
echo "  2. 按 Alt+F7 移动窗口"
echo "  3. 检查任务栏"
echo ""

# 启动客户端
docker exec -it \
    -e DISPLAY="$DISPLAY" \
    -e QT_QPA_PLATFORM=xcb \
    -e CLIENT_ASSUME_SOFTWARE_GL=1 \
    -e CLIENT_ALLOW_SOFTWARE_PRESENTATION=1 \
    -e QT_LOGGING_RULES="qt.qpa.*=false" \
    -e ZLM_VIDEO_URL="${ZLM_VIDEO_URL:-http://zlmediakit:80}" \
    -e MQTT_BROKER_URL="${MQTT_BROKER_URL:-mqtt://teleop-mosquitto:1883}" \
    -e CLIENT_RESET_LOGIN=1 \
    -e CLIENT_AUTO_CONNECT_VIDEO=0 \
    teleop-client-dev bash -c '
        cd /tmp/client-build
        if [ ! -x ./RemoteDrivingClient ]; then
            echo "错误: 客户端未编译"
            exit 1
        fi
        echo "DISPLAY=$DISPLAY"
        echo "CLIENT_ASSUME_SOFTWARE_GL=$CLIENT_ASSUME_SOFTWARE_GL"
        exec ./RemoteDrivingClient --reset-login
    '
