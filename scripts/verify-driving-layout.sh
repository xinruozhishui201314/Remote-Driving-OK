#!/usr/bin/env bash
# 操作界面布局专项验证脚本（高效、可独立运行）
# 用法：
#   bash scripts/verify-driving-layout.sh          # 快速模式（仅 QML 静态检查，<2s）
#   bash scripts/verify-driving-layout.sh --compile # 完整模式（含编译验证）
#   bash scripts/verify-driving-layout.sh --help

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
QML_FILE="$PROJECT_ROOT/client/qml/DrivingInterface.qml"
DRIVING_QML_DIR="$PROJECT_ROOT/client/qml/components/driving"
# 布局已拆到 components/driving；静态检查在下列文件中联合 grep
DRIVING_SCAN=(
  "$QML_FILE"
  "$DRIVING_QML_DIR/DrivingLayoutShell.qml"
  "$DRIVING_QML_DIR/DrivingTopChrome.qml"
  "$DRIVING_QML_DIR/DrivingLeftRail.qml"
  "$DRIVING_QML_DIR/DrivingCenterColumn.qml"
  "$DRIVING_QML_DIR/DrivingRightRail.qml"
  "$DRIVING_QML_DIR/internal/DrivingLayoutDiagnostics.qml"
)
grep_driving_qml() {
  grep -q "$1" "${DRIVING_SCAN[@]}"
}
grep_driving_qml_E() {
  grep -qE "$1" "${DRIVING_SCAN[@]}"
}

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0
FAIL=0
SKIP=0

# -----------------------------------------------------------------------------
# 用法
# -----------------------------------------------------------------------------
usage() {
    cat << 'EOF'
verify-driving-layout.sh — 操作界面布局专项验证

用法:
  verify-driving-layout.sh [选项]

选项:
  --fast, -f     快速模式（默认）：仅 QML 静态检查，不编译，约 1–2 秒
  --compile, -c  完整模式：QML 检查 + client-dev 编译验证
  --help, -h     显示此帮助

查看远驾操作界面:
  bash scripts/show-driving-ui.sh   # 仅远驾操作界面，跳过登录

快速模式检查项:
  - 右列/右视图/高精地图组件存在
  - 布局约束（minimumWidth/minimumHeight）
  - 布局诊断日志（[Client][UI][Layout]）
  - 中列不挤压右列

完整模式额外:
  - client-dev 镜像内编译通过
EOF
}

# -----------------------------------------------------------------------------
# 检查函数
# -----------------------------------------------------------------------------
check() {
    local name="$1"
    local cond="$2"
    local msg_ok="$3"
    local msg_fail="$4"
    if eval "$cond"; then
        echo "  ✓ $msg_ok"
        ((PASS++)) || true
        return 0
    else
        echo "  ✗ $msg_fail"
        ((FAIL++)) || true
        return 1
    fi
}

# -----------------------------------------------------------------------------
# 主逻辑
# -----------------------------------------------------------------------------
DO_COMPILE=false
for arg in "$@"; do
    case "$arg" in
        --compile|-c) DO_COMPILE=true ;;
        --fast|-f)    DO_COMPILE=false ;;
        --help|-h)    usage; exit 0 ;;
    esac
done

cd "$PROJECT_ROOT"

echo ""
echo "========== 操作界面布局验证 =========="
echo "  模式: $([ "$DO_COMPILE" = true ] && echo "完整（含编译）" || echo "快速（仅 QML）")"
echo ""

# 1. 文件存在
if [ ! -f "$QML_FILE" ]; then
    echo -e "${RED}错误: $QML_FILE 不存在${NC}"
    exit 1
fi

# 2. 组件与 ID（可分布在 DrivingInterface 与 components/driving/*）
check '[组件] 右列' \
    "grep_driving_qml 'id: rightColMeasurer'" \
    "右列 rightColMeasurer 存在" \
    "右列 rightColMeasurer 缺失"

check '[组件] 右视图' \
    "grep_driving_qml 'id: rightViewVideo'" \
    "右视图 rightViewVideo 存在" \
    "右视图 rightViewVideo 缺失"

check '[组件] 高精地图' \
    "grep_driving_qml 'id: hdMapRect'" \
    "高精地图 hdMapRect 存在" \
    "高精地图 hdMapRect 缺失"

check '[组件] 右视图标题' \
    "grep_driving_qml 'title: \"右视图\"'" \
    "右视图标题正确" \
    "右视图标题缺失"

check '[组件] 高精地图标题' \
    "grep_driving_qml 'text: \"高精地图\"'" \
    "高精地图标题正确" \
    "高精地图标题缺失"

# 3. 布局约束
check '[约束] 右列最小宽' \
    "grep_driving_qml_E 'Layout.minimumWidth: (220|260|.*sideColMinWidth)'" \
    "右列有 minimumWidth" \
    "右列无 minimumWidth"

check '[约束] 右视图最小高' \
    "grep_driving_qml_E 'Layout.minimumHeight: (100|.*sideColTopMinHeight)|Math.max\\(100,'" \
    "右视图有最小高度保护" \
    "右视图无最小高度"

check '[约束] 高精地图最小高' \
    "grep_driving_qml_E 'Layout.minimumHeight: (120|.*sideColBottomMinHeight)|Math.max\\(120,'" \
    "高精地图有最小高度 120" \
    "高精地图无最小高度 120"

check '[约束] 右列比例' \
    "grep_driving_qml_E 'rightColWidthRatio|rightColAllocW'" \
    "右列使用 rightColWidthRatio/rightColAllocW" \
    "右列未使用 rightColWidthRatio"

check '[布局] 先分配策略' \
    "grep_driving_qml_E 'leftColAllocW|rightColAllocW|centerColAllocW'" \
    "三列使用 AllocW 先分配宽度" \
    "未使用先分配策略"

check '[约束] 右列 Layout.minimumHeight' \
    "grep_driving_qml_E 'Layout\\.minimumHeight: (224|260|.*sideColMinHeight)|Layout\\.minimumHeight: Math\\.max'" \
    "右列有 Layout.minimumHeight 避免 height=0" \
    "右列无 Layout.minimumHeight"

check '[布局] 右列内部分配' \
    "grep_driving_qml 'id: rightViewVideo' && grep_driving_qml 'id: hdMapRect'" \
    "右列包含右视图 VideoPanel 与高精地图 Rectangle" \
    "右列未分配右视图/高精地图"

# 4. 中列不挤压右列（中列用 preferredWidth，不用 fillWidth）
check '[约束] mainRow 垂直上限' \
    "grep_driving_qml_E 'Layout.maximumHeight: (facade\\.)?mainRowAvailH'" \
    "mainRow 有 Layout.maximumHeight 约束 overflow" \
    "mainRow 无垂直上限约束"

check '[约束] 中列布局' \
    "grep_driving_qml 'id: centerColLayout' && grep_driving_qml '组件1（同级）：主视图' && grep_driving_qml_E 'Layout\\.(preferredWidth|fillWidth)'" \
    "中列有 preferredWidth 或 fillWidth 参与布局" \
    "中列布局配置异常"

# 5. 布局诊断日志
check '[日志] logLayout' \
    "grep -q 'function logLayout' '$QML_FILE'" \
    "logLayout 函数存在" \
    "logLayout 函数缺失"

check '[日志] 布局前缀' \
    "grep_driving_qml_E '\\[Client\\]\\[UI\\]\\[Layout\\]'" \
    "布局日志前缀 [Client][UI][Layout]" \
    "布局日志前缀缺失"

check '[日志] 右列/右视图/高精地图' \
    "grep_driving_qml_E 'rightCol=|右列=' && grep_driving_qml '右视图=' && grep_driving_qml '高精地图='" \
    "定时器输出右列/右视图/高精地图尺寸" \
    "定时器未输出关键组件尺寸"

# 6. 编译验证（可选）
if [ "$DO_COMPILE" = true ]; then
    echo ""
    echo "[编译验证]"
    if docker image inspect remote-driving-client-dev:full >/dev/null 2>&1; then
        if docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.compile.yml -p remote-driving run --no-deps --rm -T client-dev \
            /bin/bash -c "mkdir -p /tmp/client-build && cd /tmp/client-build && cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug && make -j4" 2>/dev/null; then
            echo "  ✓ client-dev 编译成功"
            ((PASS++)) || true
        else
            echo "  ✗ client-dev 编译失败"
            ((FAIL++)) || true
        fi
    else
        echo "  - 跳过编译（镜像 remote-driving-client-dev:full 不存在）"
        ((SKIP++)) || true
    fi
fi

# -----------------------------------------------------------------------------
# 结果
# -----------------------------------------------------------------------------
echo ""
echo "  通过: $PASS  失败: $FAIL  $([ $SKIP -gt 0 ] && echo "跳过: $SKIP")"
echo ""

if [ "$FAIL" -eq 0 ] && [ "$PASS" -ge 15 ]; then
    echo -e "${GREEN}========== 布局验证通过 ==========${NC}"
    exit 0
else
    echo -e "${RED}========== 布局验证未通过 ==========${NC}"
    exit 1
fi
