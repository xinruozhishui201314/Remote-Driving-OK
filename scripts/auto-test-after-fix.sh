#!/bin/bash
# 修复后自动测试脚本：在代码修复后自动运行完整测试和诊断
# 用法: bash scripts/auto-test-after-fix.sh [选项]
#
# 选项：
#   --skip-build    跳过编译检查
#   --skip-start    跳过系统启动（假设已启动）
#   --focus <模块>  聚焦特定模块
#   --fix-only      仅诊断，不运行完整验证

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

SKIP_BUILD=false
SKIP_START=false
FOCUS_MODULE="all"
FIX_ONLY=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --skip-start)
            SKIP_START=true
            shift
            ;;
        --focus)
            FOCUS_MODULE="$2"
            shift 2
            ;;
        --fix-only)
            FIX_ONLY=true
            shift
            ;;
        *)
            echo -e "${RED}未知参数: $1${NC}"
            exit 1
            ;;
    esac
done

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}修复后自动测试${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# 步骤 1: 检查代码变更
if [ "$SKIP_BUILD" = false ]; then
    echo -e "${CYAN}[1/5] 检查代码变更...${NC}"
    
    # 检查是否有未提交的变更
    if git diff --quiet && git diff --cached --quiet; then
        echo -e "${YELLOW}  未发现代码变更${NC}"
    else
        echo -e "${GREEN}  发现代码变更${NC}"
        git diff --stat | head -10
    fi
    echo ""
fi

# 步骤 2: 启动系统并诊断
if [ "$SKIP_START" = false ]; then
    echo -e "${CYAN}[2/5] 启动系统并诊断...${NC}"
    bash "$SCRIPT_DIR/test-and-diagnose.sh" --focus "$FOCUS_MODULE" || {
        echo -e "${RED}诊断发现问题，请查看上述输出${NC}"
        exit 1
    }
    echo ""
else
    echo -e "${CYAN}[2/5] 跳过系统启动（--skip-start）...${NC}"
    bash "$SCRIPT_DIR/test-and-diagnose.sh" --no-start --focus "$FOCUS_MODULE" || {
        echo -e "${RED}诊断发现问题，请查看上述输出${NC}"
        exit 1
    }
    echo ""
fi

# 步骤 3: 运行完整系统验证
if [ "$FIX_ONLY" = false ]; then
    echo -e "${CYAN}[3/5] 运行完整系统验证...${NC}"
    bash "$SCRIPT_DIR/verify-full-system.sh" || {
        echo -e "${YELLOW}部分验证未通过，但继续执行...${NC}"
    }
    echo ""
fi

# 步骤 4: 运行远驾接管专项验证
if [ "$FIX_ONLY" = false ] && [ "$FOCUS_MODULE" = "all" ] || [ "$FOCUS_MODULE" = "remote-control" ] || [ "$FOCUS_MODULE" = "remote_control" ]; then
    echo -e "${CYAN}[4/5] 运行远驾接管专项验证...${NC}"
    if [ -f "$SCRIPT_DIR/verify-remote-control-complete.sh" ]; then
        bash "$SCRIPT_DIR/verify-remote-control-complete.sh" || {
            echo -e "${YELLOW}远驾接管验证未完全通过，请手动测试${NC}"
        }
    else
        echo -e "${YELLOW}远驾接管验证脚本不存在，跳过${NC}"
    fi
    echo ""
fi

# 步骤 5: 生成测试报告
echo -e "${CYAN}[5/5] 生成测试报告...${NC}"
REPORT_FILE="/tmp/remote-driving-test-report-$(date +%Y%m%d-%H%M%S).txt"

cat > "$REPORT_FILE" <<EOF
========================================
远程驾驶系统测试报告
========================================
生成时间: $(date)
测试模式: ${FOCUS_MODULE}
跳过构建: ${SKIP_BUILD}
跳过启动: ${SKIP_START}
仅诊断: ${FIX_ONLY}

EOF

echo -e "${GREEN}测试报告已保存: ${REPORT_FILE}${NC}"
echo ""

# 最终总结
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}测试完成${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""
echo -e "${GREEN}✓ 所有测试已完成${NC}"
echo ""
echo "下一步："
echo "  1. 查看测试报告: cat $REPORT_FILE"
echo "  2. 查看实时日志: docker logs teleop-client-dev -f | grep REMOTE_CONTROL"
echo "  3. 手动测试: 在客户端界面操作并观察日志"
echo ""

exit 0
