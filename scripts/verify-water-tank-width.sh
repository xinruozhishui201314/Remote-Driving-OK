#!/bin/bash
# 验证水箱和垃圾箱宽度调整

set -e

CYAN='\033[0;36m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${CYAN}========== 验证水箱和垃圾箱宽度调整 ==========${NC}"
echo ""

# 1. 检查 QML 文件中的宽度设置
echo -e "${CYAN}[1/3] 检查 QML 文件中的宽度设置...${NC}"
if grep -q "Layout.preferredWidth: 70" client/qml/DrivingInterface.qml; then
    echo -e "${GREEN}✓ QML 文件中找到 Layout.preferredWidth: 70${NC}"
    grep -n "Layout.preferredWidth: 70" client/qml/DrivingInterface.qml | head -1
else
    echo -e "${RED}✗ QML 文件中未找到 Layout.preferredWidth: 70${NC}"
    exit 1
fi
echo ""

# 2. 检查 main.cpp 中的验证逻辑
echo -e "${CYAN}[2/3] 检查 main.cpp 中的验证逻辑...${NC}"
if grep -q "Layout.preferredWidth: 70" client/src/main.cpp; then
    echo -e "${GREEN}✓ main.cpp 中已更新验证逻辑为 70${NC}"
    grep -n "Layout.preferredWidth: 70" client/src/main.cpp | head -1
else
    echo -e "${RED}✗ main.cpp 中未找到 Layout.preferredWidth: 70${NC}"
    exit 1
fi
echo ""

# 3. 检查 start-full-chain.sh 中的验证逻辑
echo -e "${CYAN}[3/3] 检查 start-full-chain.sh 中的验证逻辑...${NC}"
if grep -q "Layout.preferredWidth: 70" scripts/start-full-chain.sh; then
    echo -e "${GREEN}✓ start-full-chain.sh 中已更新验证逻辑为 70${NC}"
    grep -n "Layout.preferredWidth: 70" scripts/start-full-chain.sh | head -1
else
    echo -e "${RED}✗ start-full-chain.sh 中未找到 Layout.preferredWidth: 70${NC}"
    exit 1
fi
echo ""

echo -e "${GREEN}========== 验证完成 ==========${NC}"
echo -e "${GREEN}✓ 所有文件已正确更新为 70px 宽度${NC}"
echo ""
echo -e "${YELLOW}提示: 运行 'bash scripts/start-full-chain.sh manual' 来编译并启动客户端，查看实际效果${NC}"
