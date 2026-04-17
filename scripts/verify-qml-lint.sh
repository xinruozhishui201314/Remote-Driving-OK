#!/usr/bin/env bash
# QML 语法静态检查脚本 (qmllint)
# 目的：在运行时崩溃前捕捉 QML 语法、类型不匹配和枚举错误。

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
QML_DIR="$PROJECT_ROOT/client/qml"
echo -e "  QML 目录: $QML_DIR"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}========== QML 语法与类型检查 (qmllint) ==========${NC}"

# 检查 qmllint 是否可用（优先在 client-dev 容器内运行）
RUN_IN_CONTAINER=0
if docker ps --format '{{.Names}}' | grep -q "^teleop-client-dev$" 2>/dev/null; then
    RUN_IN_CONTAINER=1
    echo -e "${GREEN}✓ 检测到 teleop-client-dev 容器正在运行，将在容器内执行 qmllint${NC}"
else
    echo -e "${YELLOW}⚠ teleop-client-dev 容器未运行，将尝试在宿主机寻找 qmllint${NC}"
fi

QMLLINT_BIN="/opt/Qt/6.8.0/gcc_64/bin/qmllint"

check_qml_file() {
    local file="$1"
    local rel_path="${file#$PROJECT_ROOT/}"
    
    if [ "$RUN_IN_CONTAINER" -eq 1 ]; then
        # 映射宿主机路径到容器路径 /workspace/...
        local container_file="/workspace/${rel_path}"
        # docker exec 没有 -T 参数，只有 -i 和 -t
        if docker exec teleop-client-dev "$QMLLINT_BIN" "$container_file" > /tmp/qmllint_out.log 2>&1; then
            echo -e "  ${GREEN}✓${NC} $rel_path"
            return 0
        else
            echo -e "  ${RED}✗${NC} $rel_path"
            cat /tmp/qmllint_out.log | sed 's/^/    /'
            return 1
        fi
    else
        # 尝试在宿主机运行（如果安装了 qt6-declarative-dev 或类似包）
        if command -v qmllint &> /dev/null; then
            if qmllint "$file" > /tmp/qmllint_out.log 2>&1; then
                echo -e "  ${GREEN}✓${NC} $rel_path"
                return 0
            else
                echo -e "  ${RED}✗${NC} $rel_path"
                cat /tmp/qmllint_out.log | sed 's/^/    /'
                return 1
            fi
        else
            echo -e "${YELLOW}⚠ 警告: 未找到 qmllint 且 teleop-client-dev 容器未运行，跳过静态检查${NC}"
            return 0
        fi
    fi
}

FAILED_FILES=0
TOTAL_FILES=0

# 查找所有 QML 文件
ALL_QML_FILES=$(find "$QML_DIR" -name "*.qml" | sort)

if [ -z "$ALL_QML_FILES" ]; then
    echo -e "${YELLOW}⚠ 警告: 未找到任何 QML 文件于 $QML_DIR${NC}"
    exit 0
fi

for f in $ALL_QML_FILES; do
    ((TOTAL_FILES++)) || true
    if ! check_qml_file "$f"; then
        ((FAILED_FILES++)) || true
    fi
done

echo ""
if [ "$FAILED_FILES" -eq 0 ]; then
    echo -e "${GREEN}✓ QML 检查完成，共 $TOTAL_FILES 个文件，全部通过。${NC}"
    exit 0
else
    echo -e "${RED}✗ QML 检查失败，共 $TOTAL_FILES 个文件，其中 $FAILED_FILES 个文件存在错误。${NC}"
    exit 1
fi
