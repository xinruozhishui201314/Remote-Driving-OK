#!/bin/bash
# 修复客户端黑屏问题
# 诊断并解决客户端窗口无法显示的问题

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml"
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}========== 客户端黑屏问题诊断与修复 ==========${NC}"
echo ""

# 1. 检查 DISPLAY 环境变量
echo -e "${CYAN}[1/6] 检查 DISPLAY 环境变量${NC}"
if [ -z "$DISPLAY" ]; then
    export DISPLAY=:0
    echo -e "${YELLOW}  DISPLAY 未设置，已设置为: $DISPLAY${NC}"
else
    echo -e "${GREEN}  ✓ DISPLAY=$DISPLAY${NC}"
fi

# 2. 检查 X11 权限
echo -e "${CYAN}[2/6] 检查 X11 权限${NC}"
if command -v xhost &>/dev/null; then
    if xhost 2>&1 | grep -q "LOCAL:"; then
        echo -e "${GREEN}  ✓ X11 权限已设置${NC}"
    else
        echo -e "${YELLOW}  设置 X11 权限...${NC}"
        xhost +local:docker 2>/dev/null || true
        echo -e "${GREEN}  ✓ X11 权限已设置${NC}"
    fi
else
    echo -e "${RED}  ✗ xhost 命令未找到${NC}"
fi

# 3. 检查 X11 socket
echo -e "${CYAN}[3/6] 检查 X11 socket${NC}"
if [ -S "/tmp/.X11-unix/X${DISPLAY#*:}" ]; then
    echo -e "${GREEN}  ✓ X11 socket 存在: /tmp/.X11-unix/X${DISPLAY#*:}${NC}"
else
    echo -e "${RED}  ✗ X11 socket 不存在: /tmp/.X11-unix/X${DISPLAY#*:}${NC}"
    echo -e "${YELLOW}  提示: 请确保 X11 服务器正在运行${NC}"
fi

# 4. 检查容器内 DISPLAY
echo -e "${CYAN}[4/6] 检查容器内 DISPLAY 和 X11${NC}"
CONTAINER_DISPLAY=$(docker exec teleop-client-dev bash -c 'echo $DISPLAY' 2>/dev/null || echo "")
if [ -n "$CONTAINER_DISPLAY" ]; then
    echo -e "${GREEN}  ✓ 容器内 DISPLAY=$CONTAINER_DISPLAY${NC}"
else
    echo -e "${YELLOW}  ⚠ 容器内 DISPLAY 未设置${NC}"
fi

X11_CHECK=$(docker exec teleop-client-dev bash -c 'ls -la /tmp/.X11-unix/ 2>&1' | grep -c "X0" || echo "0")
if [ "$X11_CHECK" -gt 0 ]; then
    echo -e "${GREEN}  ✓ 容器内可访问 X11 socket${NC}"
else
    echo -e "${RED}  ✗ 容器内无法访问 X11 socket${NC}"
    echo -e "${YELLOW}  提示: 检查 docker-compose.yml 中的 volumes 配置${NC}"
fi

# 5. 检查 Qt 平台插件
echo -e "${CYAN}[5/6] 检查 Qt 平台插件${NC}"
QT_PLUGINS=$(docker exec teleop-client-dev bash -c 'ls /opt/Qt/6.8.0/gcc_64/plugins/platforms/ 2>&1' | grep -c "libqxcb.so" || echo "0")
if [ "$QT_PLUGINS" -gt 0 ]; then
    echo -e "${GREEN}  ✓ Qt xcb 平台插件存在${NC}"
else
    echo -e "${RED}  ✗ Qt xcb 平台插件未找到${NC}"
fi

# 6. 检查窗口大小设置
echo -e "${CYAN}[6/6] 检查窗口配置${NC}"
WINDOW_SIZE=$(grep -A 2 "width:" /home/wqs/bigdata/Remote-Driving/client/qml/main.qml 2>/dev/null | head -2 | grep -oE "[0-9]+" | head -2 || echo "")
if [ -n "$WINDOW_SIZE" ]; then
    echo -e "${GREEN}  ✓ 窗口大小配置: $(echo $WINDOW_SIZE | tr '\n' 'x')${NC}"
    WIDTH=$(echo $WINDOW_SIZE | head -1)
    if [ "$WIDTH" -gt 1920 ]; then
        echo -e "${YELLOW}  ⚠ 窗口宽度 ($WIDTH) 可能超出屏幕分辨率${NC}"
    fi
fi

echo ""
echo -e "${CYAN}========== 修复建议 ==========${NC}"
echo ""

# 生成修复命令
cat << 'EOF'
修复步骤：

1. 确保 X11 服务器运行：
   echo $DISPLAY  # 应该显示 :0 或类似值

2. 设置 X11 权限：
   xhost +local:docker

3. 使用修复后的启动命令：
   export DISPLAY=:0
   export QT_QPA_PLATFORM=xcb
   docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec -it \
     -e DISPLAY=$DISPLAY \
     -e QT_QPA_PLATFORM=xcb \
     -e QT_LOGGING_RULES="qt.qpa.*=false" \
     client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login'

4. 如果窗口仍然黑屏，尝试：
   - 检查窗口是否在屏幕外（Alt+F7 移动窗口）
   - 检查是否有其他窗口遮挡
   - 尝试降低窗口分辨率（修改 client/qml/main.qml 中的 width/height）

5. 查看详细日志：
   docker logs teleop-client-dev 2>&1 | tail -50
EOF

echo ""
echo -e "${CYAN}========== 立即尝试修复启动 ==========${NC}"
read -p "是否立即尝试修复启动客户端？(y/n) " -n 1 -r
echo ""
if [[ $REPLY =~ ^[Yy]$ ]]; then
    export DISPLAY="${DISPLAY:-:0}"
    export QT_QPA_PLATFORM=xcb
    
    if command -v xhost &>/dev/null; then
        xhost +local:docker 2>/dev/null || true
    fi
    
    echo -e "${GREEN}启动客户端（带诊断环境变量）...${NC}"
    echo ""
    echo -e "${YELLOW}提示: 如果窗口仍然黑屏，请：${NC}"
    echo "  1. 按 Alt+F7 尝试移动窗口"
    echo "  2. 检查任务栏是否有窗口图标"
    echo "  3. 查看终端输出的错误信息"
    echo ""
    
    $COMPOSE exec -it \
        -e DISPLAY="$DISPLAY" \
        -e QT_QPA_PLATFORM=xcb \
        -e QT_LOGGING_RULES="qt.qpa.*=false" \
        -e ZLM_VIDEO_URL=http://zlmediakit:80 \
        -e MQTT_BROKER_URL=mqtt://teleop-mqtt:1883 \
        -e CLIENT_RESET_LOGIN=1 \
        client-dev bash -c '
            cd /tmp/client-build
            if [ ! -x ./RemoteDrivingClient ]; then
                echo "错误: 客户端未编译"
                exit 1
            fi
            echo "DISPLAY=$DISPLAY"
            echo "QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
            echo "启动客户端..."
            exec ./RemoteDrivingClient --reset-login
        '
else
    echo -e "${YELLOW}已跳过自动启动${NC}"
fi
