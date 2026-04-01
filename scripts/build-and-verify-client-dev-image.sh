#!/bin/bash
# 完整构建和验证 client-dev Docker 镜像
# 用途：确保镜像包含所有运行时依赖，避免容器重启时出现问题
#
# 用法：bash scripts/build-and-verify-client-dev-image.sh [镜像 tag]
# 默认 tag：remote-driving-client-dev:full

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

IMAGE_TAG="${1:-remote-driving-client-dev:full}"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo "=========================================="
echo "构建和验证 client-dev Docker 镜像"
echo "=========================================="
echo "镜像: $IMAGE_TAG"
echo ""

# 1. 构建镜像
echo -e "${CYAN}[1/3] 构建镜像...${NC}"
if docker compose -f docker-compose.yml -f docker-compose.client-dev.yml build client-dev 2>&1 | tee /tmp/build-client-dev.log; then
    echo -e "${GREEN}✓${NC} 镜像构建成功"
else
    echo -e "${RED}✗${NC} 镜像构建失败"
    echo "查看构建日志: cat /tmp/build-client-dev.log"
    exit 1
fi

# 2. 验证镜像
echo ""
echo -e "${CYAN}[2/3] 验证镜像完整性...${NC}"
if bash "$SCRIPT_DIR/verify-client-dev-image.sh" "$IMAGE_TAG"; then
    echo -e "${GREEN}✓${NC} 镜像验证通过"
else
    echo -e "${RED}✗${NC} 镜像验证失败"
    exit 1
fi

# 3. 测试运行
echo ""
echo -e "${CYAN}[3/3] 测试镜像运行...${NC}"
CONTAINER_NAME="client-dev-test-$$"
cleanup() {
    docker rm -f "$CONTAINER_NAME" 2>/dev/null || true
}
trap cleanup EXIT

# 启动测试容器
if docker run -d --name "$CONTAINER_NAME" "$IMAGE_TAG" sleep 30; then
    echo -e "${GREEN}✓${NC} 容器启动成功"
    
    # 检查中文字体
    if docker exec "$CONTAINER_NAME" bash -c 'fc-list | grep -qi "wqy\|wenquanyi"' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} 中文字体可用"
    else
        echo -e "${YELLOW}⊘${NC} 中文字体不可用（可能需要运行 fc-cache）"
    fi
    
    # 检查 FFmpeg
    if docker exec "$CONTAINER_NAME" bash -c 'pkg-config --modversion libavcodec >/dev/null 2>&1' 2>/dev/null; then
        version=$(docker exec "$CONTAINER_NAME" bash -c 'pkg-config --modversion libavcodec' 2>/dev/null)
        echo -e "${GREEN}✓${NC} FFmpeg 可用 (libavcodec: $version)"
    else
        echo -e "${RED}✗${NC} FFmpeg 不可用"
    fi
    
    # 检查 Qt6
    if docker exec "$CONTAINER_NAME" bash -c '[ -d /opt/Qt/6.8.0/gcc_64 ]' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Qt6 可用"
    else
        echo -e "${RED}✗${NC} Qt6 不可用"
    fi
    
    # 检查预编译客户端（如果存在）
    if docker exec "$CONTAINER_NAME" bash -c '[ -x /tmp/client-build/RemoteDrivingClient ]' 2>/dev/null; then
        echo -e "${GREEN}✓${NC} 预编译客户端存在"
    else
        echo -e "${YELLOW}⊘${NC} 预编译客户端不存在（将在运行时编译）"
    fi
    
    echo -e "${GREEN}✓${NC} 镜像运行测试通过"
else
    echo -e "${RED}✗${NC} 容器启动失败"
    exit 1
fi

echo ""
echo "=========================================="
echo -e "${GREEN}✓${NC} 镜像构建和验证完成"
echo "=========================================="
echo "镜像: $IMAGE_TAG"
echo ""
echo "可以使用以下命令启动："
echo "  docker compose -f docker-compose.yml -f docker-compose.client-dev.yml up -d client-dev"
echo "  或"
echo "  bash scripts/start-full-chain.sh manual"
echo ""
