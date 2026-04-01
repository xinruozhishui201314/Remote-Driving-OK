#!/bin/bash
# 远程驾驶界面快速验证脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=== 远程驾驶界面验证 ===${NC}"
echo ""

# 检查服务状态
echo -e "${YELLOW}[1/5] 检查服务状态...${NC}"
if ! docker compose ps | grep -q "client-dev.*Up"; then
    echo -e "${RED}✗ client-dev 容器未运行${NC}"
    echo "请先启动: docker compose up -d client-dev"
    exit 1
fi
echo -e "${GREEN}✓ client-dev 容器运行中${NC}"
echo ""

# 检查编译状态
echo -e "${YELLOW}[2/5] 检查客户端编译状态...${NC}"
if ! docker compose exec client-dev bash -c "test -f /tmp/client-build/RemoteDrivingClient" 2>/dev/null; then
    echo -e "${YELLOW}客户端未编译，开始编译...${NC}"
    docker compose exec client-dev bash -c "cd /tmp/client-build && make -j4" 2>&1 | tail -5
    if ! docker compose exec client-dev bash -c "test -f /tmp/client-build/RemoteDrivingClient" 2>/dev/null; then
        echo -e "${RED}✗ 编译失败${NC}"
        exit 1
    fi
fi
echo -e "${GREEN}✓ 客户端已编译${NC}"
echo ""

# 清除登录状态
echo -e "${YELLOW}[3/5] 清除登录状态...${NC}"
docker compose exec client-dev bash -c "
    find ~/.config -name '*RemoteDriving*' -type f 2>/dev/null | xargs rm -f || true
    find ~/.config -name '*remote-driving*' -type f 2>/dev/null | xargs rm -f || true
    echo '登录状态已清除'
" 2>/dev/null || true
echo -e "${GREEN}✓ 登录状态已清除${NC}"
echo ""

# 显示验证检查点
echo -e "${YELLOW}[4/5] 验证检查点${NC}"
echo "=========================================="
echo ""
echo -e "${BLUE}【步骤 1】登录界面${NC}"
echo "  ✓ 应该看到登录对话框"
echo "  ✓ 输入用户名: ${GREEN}123${NC}"
echo "  ✓ 输入密码: ${GREEN}123${NC}"
echo "  ✓ 点击登录按钮"
echo ""
echo -e "${BLUE}【步骤 2】车辆选择界面${NC}"
echo "  ✓ 应该看到车辆选择对话框"
echo "  ✓ 选择车辆（如：123456789）"
echo "  ✓ 点击创建会话按钮"
echo "  ✓ 点击确认并进入驾驶按钮"
echo ""
echo -e "${BLUE}【步骤 3】验证主驾驶界面布局${NC}"
echo "  ✓ 顶部：模式切换栏（前进模式/倒车模式按钮、时间、温度）"
echo "  ✓ 左侧面板（25%）："
echo "    - 左侧视图（中文标签）"
echo "    - 右侧视图/PIP（中文标签）"
echo "    - 控制按钮网格（12个按钮，中文标签）"
echo "  ✓ 中央面板（50%）："
echo "    - 主视频视图（前方摄像头/倒车模式，中文标签）"
echo "    - 仪表盘区域："
echo "      * 左侧：水箱水位、垃圾箱填充（中文标签）"
echo "      * 中央：车速、档位（中文标签）"
echo "      * 右侧：左侧状态、右侧状态、清扫进度（中文标签）"
echo "  ✓ 右侧面板（25%）："
echo "    - 顶部工具栏（亮度、警告、音频、设置）"
echo "    - 导航地图（后方视图，中文标签）"
echo "    - 系统警报（系统警报 1、系统警报 2，中文标签）"
echo ""
echo -e "${BLUE}【步骤 4】验证模式切换${NC}"
echo "  ✓ 点击倒车模式按钮"
echo "    - 主视频视图标签变为'倒车模式'"
echo "    - 显示红色网格线"
echo "    - 档位自动切换为'R'（红色）"
echo "  ✓ 点击前进模式按钮"
echo "    - 主视频视图标签变为'前方摄像头'"
echo "    - 不显示网格线"
echo "    - 档位自动切换为'D'（蓝色）"
echo ""
echo "=========================================="
echo ""

# 启动客户端
echo -e "${YELLOW}[5/5] 启动客户端进行验证...${NC}"
echo ""
echo -e "${GREEN}请按照上述检查点进行手动验证${NC}"
echo ""
echo "启动命令："
echo "  bash scripts/run-client-ui.sh --reset-login"
echo ""
echo "或者直接运行："
echo "  docker compose exec -e DISPLAY=\$DISPLAY client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login'"
echo ""
echo "验证完成后，请记录结果到："
echo "  docs/CLIENT_DRIVING_INTERFACE_VERIFICATION.md"
echo ""

# 询问是否立即启动
read -p "是否立即启动客户端进行验证？(y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo -e "${GREEN}启动客户端...${NC}"
    echo ""
    echo "提示："
    echo "  - 如果看不到窗口，检查 X11 转发设置"
    echo "  - 使用 Ctrl+C 停止客户端"
    echo ""
    bash scripts/run-client-ui.sh --reset-login
else
    echo ""
    echo "请稍后手动启动客户端进行验证"
fi
