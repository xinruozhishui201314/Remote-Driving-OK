#!/bin/bash
# 客户端 UI 功能验证脚本
# 验证登录、VIN列表、会话创建等UI功能

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}客户端 UI 功能验证${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# 检查客户端容器
echo -e "${YELLOW}[1/5] 检查客户端容器状态${NC}"
if docker compose ps client-dev | grep -q "Up"; then
    echo -e "${GREEN}✓ 客户端容器运行中${NC}"
else
    echo -e "${YELLOW}启动客户端容器...${NC}"
    docker compose up -d client-dev
    sleep 3
fi

# 检查客户端是否已编译
echo -e "${YELLOW}[2/5] 检查客户端编译状态${NC}"
if docker compose exec client-dev bash -c "test -f /tmp/client-build/RemoteDrivingClient" 2>/dev/null; then
    echo -e "${GREEN}✓ 客户端已编译${NC}"
else
    echo -e "${YELLOW}编译客户端...${NC}"
    docker compose exec client-dev bash -c "
        mkdir -p /tmp/client-build
        cd /tmp/client-build
        if [ ! -f CMakeCache.txt ]; then
            cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug
        fi
        make -j4
    " 2>&1 | tail -30
    if docker compose exec client-dev bash -c "test -f /tmp/client-build/RemoteDrivingClient" 2>/dev/null; then
        echo -e "${GREEN}✓ 编译成功${NC}"
    else
        echo -e "${RED}✗ 编译失败${NC}"
        exit 1
    fi
fi

# 检查后端服务
echo -e "${YELLOW}[3/5] 检查后端服务状态${NC}"
if curl -s http://localhost:8081/health >/dev/null 2>&1; then
    echo -e "${GREEN}✓ 后端服务可访问${NC}"
else
    echo -e "${RED}✗ 后端服务不可访问${NC}"
    echo "  请确保后端服务已启动: docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d backend"
    exit 1
fi

# 检查 Keycloak 服务
echo -e "${YELLOW}[4/5] 检查 Keycloak 服务状态${NC}"
if curl -s http://localhost:8080/realms/teleop >/dev/null 2>&1; then
    echo -e "${GREEN}✓ Keycloak 服务可访问${NC}"
else
    echo -e "${RED}✗ Keycloak 服务不可访问${NC}"
    echo "  请确保 Keycloak 服务已启动: docker compose up -d keycloak"
    exit 1
fi

# 显示运行说明
echo ""
echo -e "${YELLOW}[5/5] UI 功能验证说明${NC}"
echo ""
echo -e "${GREEN}客户端已准备就绪，请按以下步骤进行 UI 验证：${NC}"
echo ""
echo "1. 启动客户端（使用 X11 转发）："
echo "   xhost +local:docker"
echo "   docker compose exec -e DISPLAY=\$DISPLAY client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient'"
echo ""
echo "2. 或者使用 qmlscene（如果可用）："
echo "   docker compose exec -e DISPLAY=\$DISPLAY client-dev bash -c 'cd /tmp/client-build && /opt/Qt/6.8.0/gcc_64/bin/qmlscene /workspace/client/qml/main.qml'"
echo ""
echo "3. UI 验证流程："
echo "   a) 登录界面："
echo "      - 用户名: e2e-test"
echo "      - 密码: e2e-test-password"
echo "      - 点击登录"
echo ""
echo "   b) 车辆选择界面："
echo "      - 应该能看到 VIN 列表（E2ETESTVIN0000001）"
echo "      - 点击'选择'按钮选择车辆"
echo "      - 点击'创建会话'按钮"
echo "      - 应该显示会话信息（Session ID、WHIP URL、WHEP URL、控制协议）"
echo ""
echo "   c) 确认并进入驾驶："
echo "      - 点击'确认并进入驾驶'按钮"
echo "      - 应该进入主驾驶界面"
echo ""
echo "4. 验证要点："
echo "   ✓ 登录功能正常"
echo "   ✓ VIN 列表正确显示"
echo "   ✓ 会话创建成功"
echo "   ✓ 会话信息正确显示"
echo "   ✓ 主界面正常显示"
echo ""

# 提供快速启动命令
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}快速启动命令${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "# 方式1：在容器内运行（需要 X11 支持）"
echo "xhost +local:docker"
echo "docker compose exec -e DISPLAY=\$DISPLAY client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient'"
echo ""
echo "# 方式2：检查编译状态"
echo "docker compose exec client-dev bash -c 'ls -lh /tmp/client-build/RemoteDrivingClient'"
echo ""
echo "# 方式3：查看客户端日志"
echo "docker compose logs client-dev"
echo ""
