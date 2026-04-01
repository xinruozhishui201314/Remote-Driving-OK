#!/bin/bash
# 验证 client-dev Docker 镜像的完整性
# 用途：确保镜像包含所有运行时依赖，避免容器重启时出现问题

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

IMAGE_TAG="${1:-remote-driving-client-dev:full}"

echo "=========================================="
echo "验证 client-dev Docker 镜像完整性"
echo "=========================================="
echo "镜像: $IMAGE_TAG"
echo ""

# 检查镜像是否存在
if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
    echo -e "${RED}✗${NC} 镜像不存在: $IMAGE_TAG"
    echo "  请先构建镜像:"
    echo "    bash scripts/build-client-dev-full-image.sh"
    echo "    或"
    echo "    docker compose -f docker-compose.yml -f docker-compose.client-dev.yml build client-dev"
    exit 1
fi

echo -e "${GREEN}✓${NC} 镜像存在"
echo ""

# 启动临时容器进行验证
CONTAINER_NAME="client-dev-verify-$$"
cleanup() {
    docker rm -f "$CONTAINER_NAME" 2>/dev/null || true
}
trap cleanup EXIT

echo "启动临时容器进行验证..."
docker run -d --name "$CONTAINER_NAME" "$IMAGE_TAG" sleep 60

# 验证项
PASSED=0
FAILED=0

# 1. 检查中文字体
echo ""
echo "[1] 检查中文字体 (fonts-wqy-zenhei)..."
if docker exec "$CONTAINER_NAME" bash -c 'dpkg -l fonts-wqy-zenhei 2>/dev/null | grep -q ^ii' 2>/dev/null; then
    echo -e "  ${GREEN}✓${NC} 中文字体已安装"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗${NC} 中文字体未安装"
    FAILED=$((FAILED + 1))
fi

# 2. 检查字体缓存
if docker exec "$CONTAINER_NAME" bash -c 'fc-list | grep -q "WenQuanYi\|wqy-zenhei" 2>/dev/null' 2>/dev/null; then
    echo -e "  ${GREEN}✓${NC} 字体缓存已更新"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${YELLOW}⊘${NC} 字体缓存未更新（可能需要运行 fc-cache）"
fi

# 3. 检查 FFmpeg 开发库
echo ""
echo "[2] 检查 FFmpeg 开发库..."
FFMPEG_LIBS=("libavcodec-dev" "libavutil-dev" "libswscale-dev" "libavformat-dev")
for lib in "${FFMPEG_LIBS[@]}"; do
    if docker exec "$CONTAINER_NAME" bash -c "dpkg -l $lib 2>/dev/null | grep -q ^ii" 2>/dev/null; then
        echo -e "  ${GREEN}✓${NC} $lib 已安装"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}✗${NC} $lib 未安装"
        FAILED=$((FAILED + 1))
    fi
done

# 4. 检查 pkg-config
echo ""
echo "[3] 检查构建工具..."
BUILD_TOOLS=("pkg-config" "cmake" "make" "g++")
for tool in "${BUILD_TOOLS[@]}"; do
    if docker exec "$CONTAINER_NAME" bash -c "command -v $tool >/dev/null 2>&1" 2>/dev/null; then
        echo -e "  ${GREEN}✓${NC} $tool 可用"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}✗${NC} $tool 不可用"
        FAILED=$((FAILED + 1))
    fi
done

# 5. 检查 libdatachannel
echo ""
echo "[4] 检查 libdatachannel..."
if docker exec "$CONTAINER_NAME" bash -c '[ -d /opt/libdatachannel ] && [ -f /opt/libdatachannel/lib/libdatachannel.so ]' 2>/dev/null; then
    echo -e "  ${GREEN}✓${NC} libdatachannel 已安装 (/opt/libdatachannel)"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${YELLOW}⊘${NC} libdatachannel 未安装（可选，但推荐安装）"
fi

# 6. 检查 Qt6
echo ""
echo "[5] 检查 Qt6..."
if docker exec "$CONTAINER_NAME" bash -c '[ -d /opt/Qt/6.8.0/gcc_64 ]' 2>/dev/null; then
    echo -e "  ${GREEN}✓${NC} Qt6 已安装 (/opt/Qt/6.8.0/gcc_64)"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗${NC} Qt6 未安装"
    FAILED=$((FAILED + 1))
fi

# 7. 检查预编译的客户端（如果存在）
echo ""
echo "[6] 检查预编译的客户端..."
if docker exec "$CONTAINER_NAME" bash -c '[ -x /tmp/client-build/RemoteDrivingClient ]' 2>/dev/null; then
    echo -e "  ${GREEN}✓${NC} 预编译客户端存在 (/tmp/client-build/RemoteDrivingClient)"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${YELLOW}⊘${NC} 预编译客户端不存在（将在运行时编译）"
fi

# 8. 检查环境变量
echo ""
echo "[7] 检查环境变量..."
ENV_VARS=("CMAKE_PREFIX_PATH" "LD_LIBRARY_PATH" "PATH")
for var in "${ENV_VARS[@]}"; do
    if docker exec "$CONTAINER_NAME" bash -c "[ -n \"\$$var\" ]" 2>/dev/null; then
        value=$(docker exec "$CONTAINER_NAME" bash -c "echo \$$var" 2>/dev/null | head -1)
        echo -e "  ${GREEN}✓${NC} $var=$value"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${YELLOW}⊘${NC} $var 未设置（将在运行时设置）"
    fi
done

# 总结
echo ""
echo "=========================================="
echo "验证结果"
echo "=========================================="
echo -e "通过: ${GREEN}$PASSED${NC}"
echo -e "失败: ${RED}$FAILED${NC}"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}✓${NC} 镜像验证通过，所有必需依赖已安装"
    echo ""
    echo "镜像可以安全使用，容器重启不会影响运行环境。"
    exit 0
else
    echo -e "${RED}✗${NC} 镜像验证失败，存在缺失的依赖"
    echo ""
    echo "请重新构建镜像："
    echo "  bash scripts/build-client-dev-full-image.sh"
    echo "  或"
    echo "  docker compose -f docker-compose.yml -f docker-compose.client-dev.yml build client-dev"
    exit 1
fi
