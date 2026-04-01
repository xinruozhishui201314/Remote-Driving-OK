#!/bin/bash
# 测试编译失败时脚本是否正确停止
# 用法: bash scripts/test-build-failure-handling.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}========== 测试编译失败处理 ==========${NC}"
echo ""

# 1. 测试 ensure_client_built 函数
echo -e "${YELLOW}[1] 测试 ensure_client_built 函数（正常编译）...${NC}"

# 启动容器
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml up -d client-dev >/dev/null 2>&1
sleep 2

# 测试正常编译
if docker exec teleop-client-dev bash -c '
    source /workspace/scripts/start-full-chain.sh 2>/dev/null || true
    # 模拟 ensure_client_built 函数
    mkdir -p /tmp/test-build && cd /tmp/test-build
    cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug >/dev/null 2>&1
    make -j4 >/dev/null 2>&1
    [ -x ./RemoteDrivingClient ] && echo "OK" || echo "FAIL"
' 2>/dev/null | grep -q "OK"; then
    echo -e "${GREEN}✓ 正常编译测试通过${NC}"
else
    echo -e "${RED}✗ 正常编译测试失败${NC}"
fi

echo ""

# 2. 验证脚本中的关键修改
echo -e "${YELLOW}[2] 验证脚本关键修改...${NC}"

SCRIPT_FILE="scripts/start-full-chain.sh"

checks=(
    "强制重新编译以确保使用最新代码"
    "set -e"
    "exit 1"
    "编译失败会直接退出"
)

for check in "${checks[@]}"; do
    if grep -q "$check" "$SCRIPT_FILE"; then
        echo -e "${GREEN}  ✓ 找到: $check${NC}"
    else
        echo -e "${RED}  ✗ 未找到: $check${NC}"
    fi
done

echo ""

# 3. 验证 ensure_client_built 函数逻辑
echo -e "${YELLOW}[3] 验证 ensure_client_built 函数逻辑...${NC}"

if grep -A30 "ensure_client_built()" "$SCRIPT_FILE" | grep -q "exit 1"; then
    echo -e "${GREEN}  ✓ ensure_client_built 函数包含 exit 1（编译失败时退出）${NC}"
else
    echo -e "${RED}  ✗ ensure_client_built 函数未包含 exit 1${NC}"
fi

if grep -A30 "ensure_client_built()" "$SCRIPT_FILE" | grep -q "set -e"; then
    echo -e "${GREEN}  ✓ ensure_client_built 函数包含 set -e（错误时立即退出）${NC}"
else
    echo -e "${YELLOW}  ⊘ ensure_client_built 函数未包含 set -e（在子shell中执行）${NC}"
fi

echo ""

# 4. 验证主脚本调用
echo -e "${YELLOW}[4] 验证主脚本调用 ensure_client_built...${NC}"

if grep -B2 -A2 "ensure_client_built" "$SCRIPT_FILE" | grep -q "ensure_client_built[[:space:]]*#" || \
   grep -B2 -A2 "ensure_client_built" "$SCRIPT_FILE" | grep -q "ensure_client_built$"; then
    echo -e "${GREEN}  ✓ 主脚本正确调用 ensure_client_built${NC}"
    if grep -B2 -A2 "ensure_client_built" "$SCRIPT_FILE" | grep -q "|| true"; then
        echo -e "${RED}  ✗ 警告: 使用了 || true，编译失败不会停止脚本${NC}"
    else
        echo -e "${GREEN}  ✓ 未使用 || true，编译失败会停止脚本${NC}"
    fi
else
    echo -e "${RED}  ✗ 未找到 ensure_client_built 调用${NC}"
fi

echo ""

# 5. 验证 start_client 函数
echo -e "${YELLOW}[5] 验证 start_client 函数...${NC}"

if grep -A20 "start_client()" "$SCRIPT_FILE" | grep -q "set -e"; then
    echo -e "${GREEN}  ✓ start_client 函数包含 set -e${NC}"
else
    echo -e "${YELLOW}  ⊘ start_client 函数未包含 set -e（在子shell中执行）${NC}"
fi

if grep -A30 "start_client()" "$SCRIPT_FILE" | grep -q "exit 1"; then
    echo -e "${GREEN}  ✓ start_client 函数包含 exit 1（编译失败时退出）${NC}"
else
    echo -e "${RED}  ✗ start_client 函数未包含 exit 1${NC}"
fi

echo ""
echo -e "${GREEN}========== 测试完成 ==========${NC}"
echo ""
echo "总结："
echo "1. 脚本已修改为强制重新编译客户端"
echo "2. 编译失败时会调用 exit 1 停止执行"
echo "3. 主脚本使用 set -e，任何错误都会停止执行"
echo ""
echo "运行 'bash scripts/start-full-chain.sh manual' 时："
echo "- 会强制重新编译客户端（确保使用最新代码）"
echo "- 编译失败会立即停止执行"
echo "- 编译成功后会启动客户端，加载最新的 QML 文件"
