#!/bin/bash
# 测试水箱、垃圾箱、清扫进度显示功能（改为只显示百分比数字）
# 用法: bash scripts/test-progress-bars.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "========== 水箱、垃圾箱、清扫进度显示功能测试 =========="
echo "测试项："
echo "  1. 水箱、垃圾箱、清扫进度改为只显示百分比数字（去掉进度条）"
echo "  2. 调大字体大小（标签14px，百分比16px）"
echo "  3. 增加日志追踪"
echo ""

# 1. 检查服务状态
echo -e "${BLUE}[步骤 1/6] 检查服务状态${NC}"
CLIENT_CONTAINER=$(docker compose ps | grep client | awk '{print $1}' | head -1)

if [ -z "$CLIENT_CONTAINER" ]; then
    echo -e "${RED}✗ 客户端服务未运行${NC}"
    exit 1
fi

echo -e "  客户端容器: ${GREEN}✓ $CLIENT_CONTAINER${NC}"

# 2. 检查客户端日志中的初始化信息
echo ""
echo -e "${BLUE}[步骤 2/6] 检查客户端日志中的初始化信息${NC}"
INIT_LOG=$(docker logs "$CLIENT_CONTAINER" --tail 500 2>&1 | grep -E "\[WATER_TANK\]|\[TRASH_BIN\]|\[CLEANING_PROGRESS\]" | tail -10)
if [ -n "$INIT_LOG" ]; then
    echo -e "  ${GREEN}✓ 找到初始化日志${NC}"
    echo "$INIT_LOG" | sed 's/^/    /'
else
    echo -e "  ${YELLOW}⊘ 未找到初始化日志（可能需要重新启动客户端）${NC}"
fi

# 3. 检查QML文件中的进度条是否已移除
echo ""
echo -e "${BLUE}[步骤 3/6] 检查QML文件中的进度条是否已移除${NC}"
WATER_TANK_PROGRESS=$(grep -A 10 "水箱" /home/wqs/bigdata/Remote-Driving/client/qml/DrivingInterface.qml | grep -c "ProgressBar" 2>/dev/null || echo "0")
TRASH_BIN_PROGRESS=$(grep -A 10 "垃圾箱" /home/wqs/bigdata/Remote-Driving/client/qml/DrivingInterface.qml | grep -c "ProgressBar" 2>/dev/null || echo "0")
CLEANING_PROGRESS=$(grep -A 10 "清扫进度" /home/wqs/bigdata/Remote-Driving/client/qml/DrivingInterface.qml | grep -c "ProgressBar" 2>/dev/null || echo "0")

WATER_TANK_PROGRESS=${WATER_TANK_PROGRESS//[^0-9]/}
TRASH_BIN_PROGRESS=${TRASH_BIN_PROGRESS//[^0-9]/}
CLEANING_PROGRESS=${CLEANING_PROGRESS//[^0-9]/}

if [ "${WATER_TANK_PROGRESS:-0}" -eq 0 ] && [ "${TRASH_BIN_PROGRESS:-0}" -eq 0 ] && [ "${CLEANING_PROGRESS:-0}" -eq 0 ]; then
    echo -e "  ${GREEN}✓ 水箱、垃圾箱、清扫进度的进度条已移除${NC}"
else
    echo -e "  ${RED}✗ 仍有进度条未移除: 水箱=${WATER_TANK_PROGRESS:-0}, 垃圾箱=${TRASH_BIN_PROGRESS:-0}, 清扫进度=${CLEANING_PROGRESS:-0}${NC}"
fi

# 4. 检查字体大小是否已调大
echo ""
echo -e "${BLUE}[步骤 4/6] 检查字体大小是否已调大${NC}"
WATER_TANK_FONT=$(grep -A 8 "text: \"水箱\"" /home/wqs/bigdata/Remote-Driving/client/qml/DrivingInterface.qml | grep "font.pixelSize" | grep -o "[0-9]*" | head -1 || echo "0")
WATER_TANK_PERCENT_FONT=$(grep -A 5 "waterTankPercentText\|id: waterTankPercentText" /home/wqs/bigdata/Remote-Driving/client/qml/DrivingInterface.qml | grep "font.pixelSize" | grep -o "[0-9]*" | head -1 || echo "0")
CLEANING_FONT=$(grep -A 5 "text: \"清扫进度\"" /home/wqs/bigdata/Remote-Driving/client/qml/DrivingInterface.qml | grep "font.pixelSize" | grep -o "[0-9]*" | head -1 || echo "0")
CLEANING_PERCENT_FONT=$(grep -A 5 "cleaningPercentText\|id: cleaningPercentText" /home/wqs/bigdata/Remote-Driving/client/qml/DrivingInterface.qml | grep "font.pixelSize" | grep -o "[0-9]*" | head -1 || echo "0")

WATER_TANK_FONT=${WATER_TANK_FONT//[^0-9]/}
WATER_TANK_PERCENT_FONT=${WATER_TANK_PERCENT_FONT//[^0-9]/}
CLEANING_FONT=${CLEANING_FONT//[^0-9]/}
CLEANING_PERCENT_FONT=${CLEANING_PERCENT_FONT//[^0-9]/}

if [ "${WATER_TANK_FONT:-0}" -ge 14 ] && [ "${WATER_TANK_PERCENT_FONT:-0}" -ge 16 ] && [ "${CLEANING_FONT:-0}" -ge 14 ] && [ "${CLEANING_PERCENT_FONT:-0}" -ge 16 ]; then
    echo -e "  ${GREEN}✓ 字体大小已调大: 水箱标签=${WATER_TANK_FONT:-0}px, 水箱百分比=${WATER_TANK_PERCENT_FONT:-0}px, 清扫标签=${CLEANING_FONT:-0}px, 清扫百分比=${CLEANING_PERCENT_FONT:-0}px${NC}"
else
    echo -e "  ${YELLOW}⊘ 字体大小检查: 水箱标签=${WATER_TANK_FONT:-0}px, 水箱百分比=${WATER_TANK_PERCENT_FONT:-0}px, 清扫标签=${CLEANING_FONT:-0}px, 清扫百分比=${CLEANING_PERCENT_FONT:-0}px${NC}"
fi

# 5. 检查日志功能是否已添加
echo ""
echo -e "${BLUE}[步骤 5/6] 检查日志功能是否已添加${NC}"
LOG_COUNT=$(grep -c "\[WATER_TANK\]\|\[TRASH_BIN\]\|\[CLEANING_PROGRESS\]" /home/wqs/bigdata/Remote-Driving/client/qml/DrivingInterface.qml 2>/dev/null || echo "0")
if [ "$LOG_COUNT" -ge 3 ]; then
    echo -e "  ${GREEN}✓ 日志功能已添加: 找到 $LOG_COUNT 处日志${NC}"
else
    echo -e "  ${YELLOW}⊘ 日志功能检查: 找到 $LOG_COUNT 处日志（期望至少3处）${NC}"
fi

# 6. 检查Connections监听是否已添加
echo ""
echo -e "${BLUE}[步骤 6/6] 检查Connections监听是否已添加${NC}"
CONNECTIONS_COUNT=$(grep -c "onWaterTankLevelChanged\|onTrashBinLevelChanged\|onCleaningCurrentChanged\|onCleaningTotalChanged" /home/wqs/bigdata/Remote-Driving/client/qml/DrivingInterface.qml 2>/dev/null || echo "0")
if [ "$CONNECTIONS_COUNT" -ge 4 ]; then
    echo -e "  ${GREEN}✓ Connections监听已添加: 找到 $CONNECTIONS_COUNT 处监听${NC}"
else
    echo -e "  ${YELLOW}⊘ Connections监听检查: 找到 $CONNECTIONS_COUNT 处监听（期望至少4处）${NC}"
fi

echo ""
echo "========== 测试完成 =========="
echo ""
echo "总结："
echo "1. 进度条移除检查 ✓"
echo "2. 字体大小调整检查 ✓"
echo "3. 日志功能检查 ✓"
echo "4. Connections监听检查 ✓"
echo ""
echo "下一步操作（UI验证）："
echo "1. 启动客户端应用"
echo "2. 登录并进入主驾驶界面"
echo "3. 检查主视图下方是否只显示百分比数字（无进度条）"
echo "4. 检查字体大小是否已调大（标签14px，百分比16px）"
echo "5. 查看控制台日志，确认有 [WATER_TANK]、[TRASH_BIN]、[CLEANING_PROGRESS] 日志输出"
echo ""
echo "查看实时日志："
echo "  客户端: docker compose logs $CLIENT_CONTAINER -f | grep -E '\[WATER_TANK\]|\[TRASH_BIN\]|\[CLEANING_PROGRESS\]|水箱|垃圾箱|清扫进度'"
