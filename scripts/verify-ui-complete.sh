#!/bin/bash
# 完整UI界面验证脚本（人工检查单 + 可选启动客户端）
# 验证流程：登录界面 → 选择车辆界面 → 远程驾驶操作界面
# 自动化分层与视频项见：docs/CLIENT_UI_FEATURE_COVERAGE_MATRIX.md
# L1 一键：./scripts/verify-client-ui-and-video-coverage.sh

set -e

echo "=========================================="
echo "远程驾驶客户端 UI 完整验证"
echo "=========================================="
echo ""

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# 检查服务状态
echo -e "${YELLOW}[1/6] 检查服务状态...${NC}"
if ! docker compose ps | grep -q "client-dev.*Up"; then
    echo -e "${RED}✗ client-dev 容器未运行${NC}"
    echo "请先启动: docker compose up -d client-dev"
    exit 1
fi
echo -e "${GREEN}✓ client-dev 容器运行中${NC}"
echo ""

# 清除登录状态（确保从登录界面开始）
echo -e "${YELLOW}[2/6] 清除登录状态（确保从登录界面开始）...${NC}"
docker compose exec client-dev bash -c "
    find ~/.config -name '*RemoteDriving*' -type f 2>/dev/null | xargs rm -f || true
    find ~/.config -name '*remote-driving*' -type f 2>/dev/null | xargs rm -f || true
    echo '登录状态已清除'
" || true
echo -e "${GREEN}✓ 登录状态已清除${NC}"
echo ""

# 编译客户端
echo -e "${YELLOW}[3/6] 编译客户端...${NC}"
docker compose exec client-dev bash -c "cd /tmp/client-build && make -j4" 2>&1 | tail -3
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo -e "${RED}✗ 编译失败${NC}"
    exit 1
fi
echo -e "${GREEN}✓ 编译成功${NC}"
echo ""

# 检查设计图片
echo -e "${YELLOW}[4/6] 检查UI设计图片...${NC}"
DESIGN_IMAGE="client/picture/远程驾驶客户端UI设计.png"
if [ -f "$DESIGN_IMAGE" ]; then
    echo -e "${GREEN}✓ 设计图片存在: $DESIGN_IMAGE${NC}"
    file "$DESIGN_IMAGE" | head -1
else
    echo -e "${YELLOW}⚠ 设计图片不存在: $DESIGN_IMAGE${NC}"
fi
echo ""

# 显示验证检查点
echo -e "${YELLOW}[5/6] UI验证检查点${NC}"
echo "=========================================="
echo ""
echo "【步骤 1】启动客户端，验证登录界面"
echo "  ✓ 应该看到登录对话框（居中显示）"
echo "  ✓ 包含：用户名输入框、密码输入框、服务器地址输入框"
echo "  ✓ 包含：登录按钮"
echo "  ✓ 界面美观（渐变背景、圆角、阴影效果）"
echo ""
echo "【步骤 2】输入账号密码登录"
echo "  测试账号：123 / 123"
echo "  服务器地址：http://localhost:8080（或实际后端地址）"
echo "  ✓ 点击登录按钮后，登录对话框应该关闭"
echo "  ✓ 应该自动打开车辆选择对话框"
echo ""
echo "【步骤 3】验证车辆选择界面"
echo "  ✓ 应该看到车辆选择对话框（居中显示）"
echo "  ✓ 包含：车辆列表（至少显示测试车辆：123456789）"
echo "  ✓ 包含：创建会话按钮、确认并进入驾驶按钮"
echo "  ✓ 界面美观（渐变背景、卡片式车辆列表）"
echo ""
echo "【步骤 4】选择车辆并进入驾驶界面"
echo "  ✓ 点击车辆列表中的车辆（如：123456789）"
echo "  ✓ 点击创建会话按钮"
echo "  ✓ 等待会话创建成功"
echo "  ✓ 点击确认并进入驾驶按钮"
echo "  ✓ 车辆选择对话框应该关闭"
echo ""
echo "【步骤 5】验证远程驾驶操作界面布局"
echo "  参考设计图片：$DESIGN_IMAGE"
echo "  ✓ 顶部：状态栏（StatusBar）"
echo "    - 连接状态指示器"
echo "    - 视频状态、控制状态、车辆状态"
echo "    - 速度、电池、时间等信息"
echo "  ✓ 左侧：视频显示区域（VideoView，约70%宽度）"
echo "    - 视频占位符（带边框、圆角）"
echo "    - 视频状态信息"
echo "    - 全屏按钮"
echo "  ✓ 右侧：控制面板（ControlPanel，约30%宽度）"
echo "    - 车辆控制标题"
echo "    - 连接状态卡片"
echo "    - 车辆状态卡片（速度、电池）"
echo "    - 控制组件："
echo "      * 方向盘（Steering）"
echo "      * 油门（Throttle）"
echo "      * 刹车（Brake）"
echo "      * 档位（Gear：R/N/D）"
echo "    - 紧急停止按钮"
echo "  ✓ 布局：左右分栏，中间有分隔线"
echo "  ✓ 界面美观（渐变背景、卡片式设计、图标）"
echo ""
echo "=========================================="
echo ""

# 启动客户端
echo -e "${YELLOW}[6/6] 启动客户端进行验证...${NC}"
echo ""
echo -e "${GREEN}请按照上述检查点进行手动验证${NC}"
echo ""
echo "启动命令："
echo "  docker compose exec client-dev bash -c 'cd /tmp/client-build && DISPLAY=\$DISPLAY ./RemoteDrivingClient'"
echo ""
echo "或者使用脚本："
echo "  bash scripts/run-client-ui.sh"
echo ""
echo "验证完成后，请记录结果到："
echo "  docs/CLIENT_UI_VERIFICATION_REPORT.md"
echo ""

# 询问是否立即启动
read -p "是否立即启动客户端进行验证？(y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "启动客户端..."
    echo ""
    echo "提示："
    echo "  - 如果看不到窗口，检查 X11 转发设置"
    echo "  - 使用 Ctrl+C 停止客户端"
    echo ""
    docker compose exec client-dev bash -c "cd /tmp/client-build && DISPLAY=\$DISPLAY ./RemoteDrivingClient" 2>&1 | tee /tmp/client-ui-verification.log
else
    echo "请稍后手动启动客户端进行验证"
fi
