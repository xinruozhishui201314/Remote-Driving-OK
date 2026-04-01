#!/bin/bash
# 外部编译 + 生产模式运行方案
# 在主机上编译代码，然后将二进制文件挂载到生产模式容器中运行
# 优点：避免每次修改代码都重建镜像，编译速度快

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

BACKEND_DIR="$PROJECT_ROOT/backend"
BUILD_DIR="$BACKEND_DIR/build"
BINARY_PATH="$BUILD_DIR/bin/teleop_backend"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}=== Backend 外部编译 + 运行模式 ===${NC}"
echo "在主机上编译，使用生产模式镜像运行"
echo ""

# 检查编译工具
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}错误：未找到 cmake，请安装：${NC}"
    echo "  Ubuntu/Debian: sudo apt-get install cmake build-essential libpq-dev"
    echo "  macOS: brew install cmake postgresql"
    exit 1
fi

# 创建构建目录
mkdir -p "$BUILD_DIR"

# 编译
echo -e "${GREEN}编译 Backend...${NC}"
cd "$BUILD_DIR"

if [ ! -f CMakeCache.txt ] || [ "$BACKEND_DIR/CMakeLists.txt" -nt CMakeCache.txt ]; then
    echo "配置 CMake..."
    cmake "$BACKEND_DIR" -DCMAKE_BUILD_TYPE=Release
fi

echo "构建..."
cmake --build . --target teleop_backend

if [ ! -f "$BINARY_PATH" ]; then
    echo -e "${RED}编译失败：找不到可执行文件${NC}"
    exit 1
fi

# 确保二进制文件有执行权限
chmod +x "$BINARY_PATH"

echo -e "${GREEN}编译完成：$BINARY_PATH${NC}"
echo ""

# 停止现有容器
cd "$PROJECT_ROOT"
if docker compose ps backend 2>/dev/null | grep -q "Up"; then
    echo "停止现有 Backend 容器..."
    docker compose stop backend
fi

# 使用生产模式启动，但挂载编译好的二进制文件
echo -e "${GREEN}启动 Backend（使用编译好的二进制）...${NC}"

# 创建临时 docker-compose override
TMP_COMPOSE="/tmp/docker-compose.backend-dev-$$.yml"
cat > "$TMP_COMPOSE" <<EOF
services:
  backend:
    volumes:
      - $BINARY_PATH:/app/teleop_backend:rw
    entrypoint: ["sh", "-c"]  # 使用 sh 执行
    command: ["chmod +x /app/teleop_backend && /app/teleop_backend"]
EOF

cd "$PROJECT_ROOT"
docker compose -f docker-compose.yml -f "$TMP_COMPOSE" up -d backend

# 清理临时文件
rm -f "$TMP_COMPOSE"

echo ""
echo -e "${GREEN}Backend 已启动${NC}"
echo "查看日志: docker compose logs -f backend"
echo ""
echo "修改代码后，重新运行此脚本即可重新编译并重启"
