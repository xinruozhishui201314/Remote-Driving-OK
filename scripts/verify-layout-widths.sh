#!/bin/bash
# 验证驾驶页底部控制条宽度：由 DrivingInterface.qml 根属性 dashboard*Width 集中定义，
# 各区块 Layout.preferredWidth 绑定到对应变量（非写死字面量）。

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

CYAN='\033[0;36m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

QML="$PROJECT_ROOT/client/qml/DrivingInterface.qml"

echo -e "${CYAN}========== 验证布局宽度（dashboard*Width） ==========${NC}"
echo ""

# 1. 根属性定义（与 QML 中 readonly property 保持一致，变更时请同步本脚本）
echo -e "${CYAN}[1/4] 检查 dashboard 宽度属性定义...${NC}"
if grep -q "readonly property int dashboardGearWidth: 104" "$QML" &&
   grep -q "readonly property int dashboardTankWidth: 148" "$QML" &&
   grep -q "readonly property int dashboardSpeedWidth: 164" "$QML"; then
    echo -e "${GREEN}✓ dashboardGearWidth=104, dashboardTankWidth=148, dashboardSpeedWidth=164${NC}"
else
    echo -e "${RED}✗ 未找到预期的 dashboard*Width 属性定义（或数值已改未同步脚本）${NC}"
    exit 1
fi
echo ""

# 2. 档位区块绑定
echo -e "${CYAN}[2/4] 检查档位显示区域绑定...${NC}"
if grep -A 4 "档位显示" "$QML" | grep -q "Layout.preferredWidth: dashboardGearWidth"; then
    echo -e "${GREEN}✓ 档位区域: Layout.preferredWidth → dashboardGearWidth${NC}"
else
    echo -e "${RED}✗ 档位区域未绑定 dashboardGearWidth${NC}"
    exit 1
fi
echo ""

# 3. 水箱 + 垃圾箱区块绑定
echo -e "${CYAN}[3/4] 检查水箱/垃圾箱区域绑定...${NC}"
if grep -A 6 "水箱 + 垃圾箱" "$QML" | grep -q "Layout.preferredWidth: dashboardTankWidth"; then
    echo -e "${GREEN}✓ 水箱+垃圾箱: Layout.preferredWidth → dashboardTankWidth${NC}"
else
    echo -e "${RED}✗ 水箱+垃圾箱区域未绑定 dashboardTankWidth${NC}"
    exit 1
fi
echo ""

# 4. 速度控制区块绑定
echo -e "${CYAN}[4/4] 检查速度控制区域绑定...${NC}"
if grep -A 6 "速度控制" "$QML" | grep -q "Layout.preferredWidth: dashboardSpeedWidth"; then
    echo -e "${GREEN}✓ 速度控制: Layout.preferredWidth → dashboardSpeedWidth${NC}"
else
    echo -e "${RED}✗ 速度控制区域未绑定 dashboardSpeedWidth${NC}"
    exit 1
fi
echo ""

# 5. 急停与目标车速布局（弱校验）
echo -e "${CYAN}[附加] 急停 / 目标车速容器...${NC}"
if grep -A 40 "速度控制" "$QML" | grep -q "Column"; then
    echo -e "${GREEN}✓ 速度控制片段内存在 Column 布局${NC}"
else
    echo -e "${YELLOW}⚠ 未在速度控制片段内确认 Column，请手动检查${NC}"
fi
echo ""

echo -e "${GREEN}========== 验证完成 ==========${NC}"
echo ""
echo -e "${YELLOW}当前像素值（根属性）:${NC}"
echo -e "  - 档位 dashboardGearWidth: 104"
echo -e "  - 水箱+垃圾箱 dashboardTankWidth: 148"
echo -e "  - 速度控制 dashboardSpeedWidth: 164"
echo ""
echo -e "${YELLOW}提示: 调整宽度请改 DrivingInterface.qml 顶部 readonly property，并同步本脚本中的数值检查。${NC}"
