#!/bin/bash
# 客户端中文字体安装脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=== 安装客户端中文字体 ===${NC}"
echo ""

# 检查容器是否运行
if ! docker compose ps client-dev | grep -q "Up"; then
    echo -e "${YELLOW}启动客户端容器...${NC}"
    docker compose up -d client-dev
    sleep 2
fi

# 安装中文字体
echo -e "${YELLOW}安装中文字体包...${NC}"
docker compose exec -u root client-dev bash -c "
    apt-get update -qq && \
    apt-get install -y -qq \
        fonts-noto-cjk \
        fonts-wqy-microhei \
        fontconfig \
    2>&1 | tail -10
"

# 更新字体缓存
echo -e "${YELLOW}更新字体缓存...${NC}"
docker compose exec -u root client-dev bash -c "fc-cache -fv 2>&1 | tail -5"

# 验证字体安装
echo ""
echo -e "${YELLOW}验证中文字体安装...${NC}"
CHINESE_FONTS=$(docker compose exec client-dev bash -c "fc-list :lang=zh 2>&1 | head -5" || echo "")

if [ -n "$CHINESE_FONTS" ]; then
    echo -e "${GREEN}✓ 中文字体安装成功${NC}"
    echo "$CHINESE_FONTS" | head -3
else
    echo -e "${YELLOW}⚠ 未检测到中文字体，但安装已完成${NC}"
fi

echo ""
echo -e "${GREEN}字体安装完成！${NC}"
echo ""
echo "现在可以重新启动客户端，中文字体应该可以正常显示了。"
echo "启动命令："
echo "  bash scripts/run-client-ui.sh"
