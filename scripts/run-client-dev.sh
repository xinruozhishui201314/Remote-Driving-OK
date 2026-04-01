#!/bin/bash
# 客户端开发环境快速启动脚本
# 在容器内编译并运行客户端

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=== 客户端开发环境启动 ===${NC}"
echo ""

# 检查客户端容器
if ! docker compose ps client-dev | grep -q "Up"; then
    echo -e "${YELLOW}启动客户端容器...${NC}"
    docker compose up -d client-dev
    sleep 3
fi

# 编译客户端（使用临时目录避免 volume 权限问题）
echo -e "${YELLOW}编译客户端...${NC}"
docker compose exec client-dev bash -c "
    BUILD_DIR=\${BUILD_DIR:-/tmp/client-build}
    mkdir -p \$BUILD_DIR
    cd \$BUILD_DIR
    
    # CMake 配置
    if [ ! -f CMakeCache.txt ]; then
        echo '配置 CMake...'
        cmake /workspace/client \
            -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel \
            -DCMAKE_BUILD_TYPE=Debug
    fi
    
    # 编译
    echo '编译中...'
    make -j4
    
    # 检查编译结果
    if [ -f RemoteDrivingClient ]; then
        echo '✓ 编译成功'
        ls -lh RemoteDrivingClient
    else
        echo '✗ 编译失败'
        exit 1
    fi
" 2>&1 | tail -40

# 检查编译结果
if docker compose exec client-dev bash -c "test -f /tmp/client-build/RemoteDrivingClient" 2>/dev/null; then
    echo ""
    echo -e "${GREEN}✓ 客户端编译成功${NC}"
    echo ""
    echo -e "${BLUE}启动客户端...${NC}"
    echo ""
    echo "请确保已设置 X11 权限："
    echo "  xhost +local:docker"
    echo ""
    echo "运行客户端："
    echo "  docker compose exec -e DISPLAY=\$DISPLAY client-dev bash -c 'cd /tmp/client-build && /opt/Qt/6.8.0/gcc_64/bin/qmlscene /workspace/client/qml/main.qml || ./RemoteDrivingClient'"
    echo ""
else
    echo -e "${RED}✗ 编译失败，请检查错误信息${NC}"
    exit 1
fi
