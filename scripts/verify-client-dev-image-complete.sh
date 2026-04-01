#!/bin/bash
# 验证 client-dev 镜像是否包含所有运行时依赖
# 用途：确保镜像完备，启动容器后无需安装额外库

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

IMAGE_TAG="${1:-remote-driving-client-dev:full}"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

echo "=========================================="
echo "验证 client-dev 镜像完备性"
echo "=========================================="
echo "镜像: $IMAGE_TAG"
echo ""

# 检查镜像是否存在
if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
    echo -e "${RED}✗${NC} 镜像不存在: $IMAGE_TAG"
    exit 1
fi

CONTAINER_NAME="verify-client-dev-$$"
cleanup() {
    docker rm -f "$CONTAINER_NAME" 2>/dev/null || true
}
trap cleanup EXIT

# 启动测试容器
if ! docker run -d --name "$CONTAINER_NAME" "$IMAGE_TAG" sleep 60; then
    echo -e "${RED}✗${NC} 无法启动测试容器"
    exit 1
fi

echo -e "${CYAN}检查运行时依赖...${NC}"
echo ""

ALL_OK=true

# 1. 检查 mosquitto-clients
echo -n "  [1/6] mosquitto-clients: "
if docker exec "$CONTAINER_NAME" bash -c "command -v mosquitto_pub >/dev/null 2>&1" 2>/dev/null; then
    echo -e "${GREEN}✓${NC} 已安装"
else
    echo -e "${RED}✗${NC} 未安装"
    ALL_OK=false
fi

# 2. 检查中文字体
echo -n "  [2/6] 中文字体 (fonts-wqy-zenhei): "
if docker exec "$CONTAINER_NAME" bash -c "fc-list | grep -qi 'wqy\|wenquanyi' >/dev/null 2>&1" 2>/dev/null; then
    echo -e "${GREEN}✓${NC} 已安装"
else
    echo -e "${RED}✗${NC} 未安装"
    ALL_OK=false
fi

# 3. 检查 FFmpeg 开发库
echo -n "  [3/6] FFmpeg 开发库: "
if docker exec "$CONTAINER_NAME" bash -c "pkg-config --modversion libavcodec >/dev/null 2>&1" 2>/dev/null; then
    VERSION=$(docker exec "$CONTAINER_NAME" bash -c "pkg-config --modversion libavcodec" 2>/dev/null)
    echo -e "${GREEN}✓${NC} 已安装 (libavcodec: $VERSION)"
else
    echo -e "${RED}✗${NC} 未安装"
    ALL_OK=false
fi

# 4. 检查 Qt6
echo -n "  [4/6] Qt6: "
if docker exec "$CONTAINER_NAME" bash -c "[ -d /opt/Qt/6.8.0/gcc_64 ]" 2>/dev/null; then
    echo -e "${GREEN}✓${NC} 已安装"
else
    echo -e "${RED}✗${NC} 未安装"
    ALL_OK=false
fi

# 5. 检查 libdatachannel
echo -n "  [5/6] libdatachannel: "
if docker exec "$CONTAINER_NAME" bash -c "[ -d /opt/libdatachannel ]" 2>/dev/null; then
    echo -e "${GREEN}✓${NC} 已安装"
else
    echo -e "${RED}✗${NC} 未安装"
    ALL_OK=false
fi

# 6. 检查编译工具
echo -n "  [6/6] 编译工具 (cmake, make, g++): "
if docker exec "$CONTAINER_NAME" bash -c "command -v cmake >/dev/null 2>&1 && command -v make >/dev/null 2>&1 && command -v g++ >/dev/null 2>&1" 2>/dev/null; then
    echo -e "${GREEN}✓${NC} 已安装"
else
    echo -e "${RED}✗${NC} 未安装"
    ALL_OK=false
fi

echo ""
echo "=========================================="
if [ "$ALL_OK" = true ]; then
    echo -e "${GREEN}✓${NC} 镜像完备性验证通过"
    echo ""
    echo "所有运行时依赖已安装，容器启动后可直接使用，无需安装额外库。"
    exit 0
else
    echo -e "${RED}✗${NC} 镜像完备性验证失败"
    echo ""
    echo "部分依赖缺失，请重新构建镜像。"
    exit 1
fi
