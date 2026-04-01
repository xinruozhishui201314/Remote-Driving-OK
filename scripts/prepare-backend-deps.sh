#!/bin/bash
# 在宿主机上下载 Backend 依赖项，然后挂载到容器中使用
# 避免在容器内下载，加快编译速度

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

DEPS_DIR="$PROJECT_ROOT/backend/deps"
CPP_HTTPLIB_DIR="$DEPS_DIR/cpp-httplib"
NLOHMANN_JSON_DIR="$DEPS_DIR/nlohmann_json"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=== 准备 Backend 依赖项 ===${NC}"
echo ""

# 创建依赖项目录
mkdir -p "$DEPS_DIR"

# 下载 cpp-httplib
if [ ! -d "$CPP_HTTPLIB_DIR" ] || [ ! -f "$CPP_HTTPLIB_DIR/.git/config" ]; then
    echo -e "${YELLOW}下载 cpp-httplib (v0.14.3)...${NC}"
    if [ -d "$CPP_HTTPLIB_DIR" ]; then
        rm -rf "$CPP_HTTPLIB_DIR"
    fi
    git clone --depth 1 --branch v0.14.3 https://github.com/yhirose/cpp-httplib.git "$CPP_HTTPLIB_DIR"
    echo -e "${GREEN}✓ cpp-httplib 下载完成${NC}"
else
    echo -e "${GREEN}✓ cpp-httplib 已存在，跳过下载${NC}"
    # 检查版本
    cd "$CPP_HTTPLIB_DIR"
    CURRENT_TAG=$(git describe --tags --exact-match 2>/dev/null || git rev-parse --short HEAD)
    echo "  当前版本: $CURRENT_TAG"
    cd "$PROJECT_ROOT"
fi

# 下载 nlohmann_json
if [ ! -d "$NLOHMANN_JSON_DIR" ] || [ ! -f "$NLOHMANN_JSON_DIR/.git/config" ]; then
    echo -e "${YELLOW}下载 nlohmann_json (v3.11.3)...${NC}"
    if [ -d "$NLOHMANN_JSON_DIR" ]; then
        rm -rf "$NLOHMANN_JSON_DIR"
    fi
    git clone --depth 1 --branch v3.11.3 https://github.com/nlohmann/json.git "$NLOHMANN_JSON_DIR"
    echo -e "${GREEN}✓ nlohmann_json 下载完成${NC}"
else
    echo -e "${GREEN}✓ nlohmann_json 已存在，跳过下载${NC}"
    # 检查版本
    cd "$NLOHMANN_JSON_DIR"
    CURRENT_TAG=$(git describe --tags --exact-match 2>/dev/null || git rev-parse --short HEAD)
    echo "  当前版本: $CURRENT_TAG"
    cd "$PROJECT_ROOT"
fi

echo ""
echo -e "${GREEN}=== 依赖项准备完成 ===${NC}"
echo ""
echo "依赖项位置："
echo "  - cpp-httplib: $CPP_HTTPLIB_DIR"
echo "  - nlohmann_json: $NLOHMANN_JSON_DIR"
echo ""
echo "现在可以启动开发环境："
echo "  docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d backend"
