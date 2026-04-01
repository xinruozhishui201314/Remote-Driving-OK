#!/bin/bash
# 客户端 UI 快速启动脚本
# 用法：
#   bash scripts/run-client-ui.sh                    # 保留登录状态（默认）
#   bash scripts/run-client-ui.sh --reset-login     # 清除登录状态后启动（通过代码实现）
#   bash scripts/run-client-ui.sh --clear-login     # 清除登录状态后启动（同 --reset-login）
#   bash scripts/run-client-ui.sh -r                 # 清除登录状态后启动（简写）
#   CLIENT_RESET_LOGIN=1 bash scripts/run-client-ui.sh  # 使用环境变量控制

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

# 解析命令行参数，传递给客户端程序
CLIENT_ARGS=()
if [ "$1" == "--reset-login" ] || [ "$1" == "--clear-login" ] || [ "$1" == "-r" ]; then
    CLIENT_ARGS+=("--reset-login")
    RESET_LOGIN=true
elif [ "$CLIENT_RESET_LOGIN" == "1" ] || [ "$CLIENT_RESET_LOGIN" == "true" ]; then
    RESET_LOGIN=true
fi

echo -e "${BLUE}=== 客户端 UI 快速启动 ===${NC}"
echo ""

# 同步启动 CARLA 仿真（若未运行）；SKIP_CARLA=1 可跳过
if [ -f "$SCRIPT_DIR/ensure-carla-running.sh" ]; then
  bash "$SCRIPT_DIR/ensure-carla-running.sh" 2>/dev/null || true
fi

# 若未启动 client-dev 容器则先启动
if ! docker compose ps client-dev 2>/dev/null | grep -q Up; then
  echo -e "${YELLOW}启动 client-dev 容器...${NC}"
  docker compose up -d client-dev
  sleep 3
fi

# 检查 X11 权限
if ! xhost 2>&1 | grep -q "LOCAL:"; then
    echo -e "${YELLOW}设置 X11 权限...${NC}"
    xhost +local:docker
    echo -e "${GREEN}✓ X11 权限已设置${NC}"
fi

# 检查 DISPLAY
if [ -z "$DISPLAY" ]; then
    export DISPLAY=:0
    echo -e "${YELLOW}设置 DISPLAY 环境变量: $DISPLAY${NC}"
fi

# 检查客户端是否已编译
if ! docker compose exec client-dev bash -c "test -f /tmp/client-build/RemoteDrivingClient" 2>/dev/null; then
    echo -e "${YELLOW}客户端未编译，开始编译...${NC}"
    docker compose exec client-dev bash -c "
        mkdir -p /tmp/client-build
        cd /tmp/client-build
        if [ ! -f CMakeCache.txt ]; then
            cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug
        fi
        make -j4
    " 2>&1 | tail -20
    
    if ! docker compose exec client-dev bash -c "test -f /tmp/client-build/RemoteDrivingClient" 2>/dev/null; then
        echo -e "${RED}✗ 编译失败${NC}"
        exit 1
    fi
    echo -e "${GREEN}✓ 编译成功${NC}"
fi

echo ""
echo -e "${GREEN}启动客户端...${NC}"
if [ "$RESET_LOGIN" == "true" ]; then
    echo -e "${YELLOW}提示：将清除登录状态（通过代码实现），将显示登录界面${NC}"
    # 设置环境变量传递给客户端
    export CLIENT_RESET_LOGIN=1
else
    echo -e "${BLUE}提示：保留上次登录状态（如需清除，使用 --reset-login 参数）${NC}"
fi
echo ""
echo -e "${BLUE}测试账号信息：${NC}"
echo "  用户名: ${GREEN}123${NC} 或 ${GREEN}e2e-test${NC}"
echo "  密码: ${GREEN}123${NC} 或 ${GREEN}e2e-test-password${NC}"
echo "  服务器: ${GREEN}http://localhost:8080${NC} 或 ${GREEN}http://localhost:8081${NC}"
echo ""
echo -e "${BLUE}使用说明：${NC}"
echo "  - 保留登录状态启动（默认）：${GREEN}bash scripts/run-client-ui.sh${NC}"
echo "  - 清除登录状态启动：${GREEN}bash scripts/run-client-ui.sh --reset-login${NC}"
echo ""

# 运行客户端，传递命令行参数和环境变量
if [ "$RESET_LOGIN" == "true" ]; then
    docker compose exec -e DISPLAY=$DISPLAY -e CLIENT_RESET_LOGIN=1 client-dev bash -c "cd /tmp/client-build && ./RemoteDrivingClient --reset-login"
else
    docker compose exec -e DISPLAY=$DISPLAY client-dev bash -c "cd /tmp/client-build && ./RemoteDrivingClient"
fi
