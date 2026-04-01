#!/bin/bash
# 完整验证 QML 修改是否生效
# 用法: bash scripts/verify-qml-changes.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}========== QML 修改验证报告 ==========${NC}"
echo ""

# 1. 检查文件修改
echo -e "${YELLOW}[1] 检查 QML 文件修改...${NC}"
QML_FILE="client/qml/DrivingInterface.qml"
ALL_MODS_OK=true

check_modification() {
    local pattern="$1"
    local name="$2"
    if grep -q "$pattern" "$QML_FILE"; then
        echo -e "${GREEN}  ✓ $name${NC}"
        return 0
    else
        echo -e "${RED}  ✗ $name (未找到)${NC}"
        return 1
    fi
}

check_modification "Layout.preferredWidth: 35" "水箱/垃圾箱宽度: 35" || ALL_MODS_OK=false
check_modification "spacing: 16" "按钮间距: 16" || ALL_MODS_OK=false
check_modification "width: 50; height: 42" "急停按钮: 50x42" || ALL_MODS_OK=false
check_modification "width: 90; height: 40" "目标速度输入框: 90x40" || ALL_MODS_OK=false
check_modification "width: parent.width.*\*.*0.5" "进度条长度缩短 (* 0.5)" || ALL_MODS_OK=false
check_modification "anchors.leftMargin: 5" "左右边距: 5" || ALL_MODS_OK=false
check_modification "spacing: 3" "元素间距: 3" || ALL_MODS_OK=false

echo ""

# 2. 编译客户端
echo -e "${YELLOW}[2] 编译客户端（使用最新代码）...${NC}"
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml up -d client-dev >/dev/null 2>&1
sleep 2

docker exec teleop-client-dev bash -c "
    mkdir -p /tmp/client-build && cd /tmp/client-build
    if [ ! -f CMakeCache.txt ]; then
        cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug >/dev/null 2>&1
    fi
    make -j4 2>&1 | tail -5
" || {
    echo -e "${RED}❌ 编译失败${NC}"
    exit 1
}

echo -e "${GREEN}✓ 编译成功${NC}"
echo ""

# 3. 运行客户端并验证
echo -e "${YELLOW}[3] 运行客户端并验证修改...${NC}"
LOG_FILE="/tmp/qml-verify-$(date +%s).log"

timeout 12 docker exec teleop-client-dev bash -c "
    cd /tmp/client-build
    ./RemoteDrivingClient --reset-login 2>&1
" > "$LOG_FILE" 2>&1 || true

# 分析日志
echo "=== QML 加载验证结果 ==="
if grep -q "\[QML_LOAD\] ✓ 找到 Layout.preferredWidth: 35" "$LOG_FILE"; then
    echo -e "${GREEN}✓ 验证通过: Layout.preferredWidth: 35${NC}"
else
    echo -e "${RED}✗ 验证失败: Layout.preferredWidth: 35${NC}"
    ALL_MODS_OK=false
fi

if grep -q "\[QML_LOAD\] ✓ 找到 spacing: 16" "$LOG_FILE"; then
    echo -e "${GREEN}✓ 验证通过: spacing: 16${NC}"
else
    echo -e "${RED}✗ 验证失败: spacing: 16${NC}"
    ALL_MODS_OK=false
fi

if grep -q "\[QML_LOAD\] ✓ 找到 width: 50; height: 42" "$LOG_FILE"; then
    echo -e "${GREEN}✓ 验证通过: width: 50; height: 42${NC}"
else
    echo -e "${RED}✗ 验证失败: width: 50; height: 42${NC}"
    ALL_MODS_OK=false
fi

if grep -q "\[QML_LOAD\] ✓ 找到 width: 90; height: 40" "$LOG_FILE"; then
    echo -e "${GREEN}✓ 验证通过: width: 90; height: 40${NC}"
else
    echo -e "${RED}✗ 验证失败: width: 90; height: 40${NC}"
    ALL_MODS_OK=false
fi

if grep -q "\[QML_LOAD\] ✓ 找到进度条长度缩短" "$LOG_FILE"; then
    echo -e "${GREEN}✓ 验证通过: 进度条长度缩短${NC}"
else
    echo -e "${RED}✗ 验证失败: 进度条长度缩短${NC}"
    ALL_MODS_OK=false
fi

echo ""
echo "=== QML 文件加载信息 ==="
grep "\[QML_LOAD\]" "$LOG_FILE" | grep -E "找到 QML|绝对路径|修改时间|QML 目录|DrivingInterface" | head -10

echo ""
if [ "$ALL_MODS_OK" = true ]; then
    echo -e "${GREEN}========== 所有修改已验证生效！==========${NC}"
    echo ""
    echo "修改已生效的原因："
    echo "1. QML 文件是从文件系统动态加载的（不是嵌入到二进制文件中）"
    echo "2. 客户端已重新编译，包含最新的日志代码"
    echo "3. 所有5处关键修改都已验证存在于 QML 文件中"
    echo ""
    echo "如果 UI 上仍然看不到修改，可能的原因："
    echo "1. 使用了旧的客户端可执行文件（需要重新编译）"
    echo "2. QML 引擎缓存（已通过重新启动客户端解决）"
    echo "3. 需要完全重启客户端容器"
    echo ""
    echo "解决方案："
    echo "  运行: bash scripts/start-full-chain.sh manual"
    echo "  这会重新编译并启动客户端，确保使用最新的 QML 文件"
    exit 0
else
    echo -e "${RED}========== 部分验证失败 ==========${NC}"
    echo "请检查日志文件: $LOG_FILE"
    exit 1
fi
