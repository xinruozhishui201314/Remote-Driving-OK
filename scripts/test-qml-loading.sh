#!/bin/bash
# 自动化测试 QML 文件加载和修改验证
# 用法: bash scripts/test-qml-loading.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml"
LOG_FILE="/tmp/qml-loading-test.log"

echo -e "${CYAN}========== QML 文件加载测试 ==========${NC}"
echo ""

# 1. 检查 QML 文件修改
echo -e "${YELLOW}[1/5] 检查 QML 文件修改...${NC}"
QML_FILE="client/qml/DrivingInterface.qml"

if [ ! -f "$QML_FILE" ]; then
    echo -e "${RED}❌ QML 文件不存在: $QML_FILE${NC}"
    exit 1
fi

echo -e "${GREEN}✓ QML 文件存在${NC}"

# 验证关键修改
echo "检查关键修改..."
MODIFICATIONS=0

if grep -q "Layout.preferredWidth: 35" "$QML_FILE"; then
    echo -e "${GREEN}  ✓ Layout.preferredWidth: 35 (水箱/垃圾箱宽度)${NC}"
    MODIFICATIONS=$((MODIFICATIONS + 1))
else
    echo -e "${RED}  ✗ 未找到 Layout.preferredWidth: 35${NC}"
fi

if grep -q "spacing: 16" "$QML_FILE"; then
    echo -e "${GREEN}  ✓ spacing: 16 (按钮间距)${NC}"
    MODIFICATIONS=$((MODIFICATIONS + 1))
else
    echo -e "${RED}  ✗ 未找到 spacing: 16${NC}"
fi

if grep -q "width: 50; height: 42" "$QML_FILE"; then
    echo -e "${GREEN}  ✓ width: 50; height: 42 (急停按钮)${NC}"
    MODIFICATIONS=$((MODIFICATIONS + 1))
else
    echo -e "${RED}  ✗ 未找到 width: 50; height: 42${NC}"
fi

if grep -q "width: 90; height: 40" "$QML_FILE"; then
    echo -e "${GREEN}  ✓ width: 90; height: 40 (目标速度输入框)${NC}"
    MODIFICATIONS=$((MODIFICATIONS + 1))
else
    echo -e "${RED}  ✗ 未找到 width: 90; height: 40${NC}"
fi

if grep -q "width: parent.width.*\*.*0.5" "$QML_FILE"; then
    echo -e "${GREEN}  ✓ 进度条长度缩短 (* 0.5)${NC}"
    MODIFICATIONS=$((MODIFICATIONS + 1))
else
    echo -e "${RED}  ✗ 未找到进度条长度缩短${NC}"
fi

echo ""
echo "找到 $MODIFICATIONS/5 处关键修改"
echo ""

# 2. 检查容器内文件
echo -e "${YELLOW}[2/5] 检查容器内 QML 文件...${NC}"
CONTAINER_QML="/workspace/client/qml/DrivingInterface.qml"

if docker exec teleop-client-dev test -f "$CONTAINER_QML" 2>/dev/null; then
    echo -e "${GREEN}✓ 容器内 QML 文件存在${NC}"
    
    # 检查文件修改时间
    CONTAINER_MODIFY=$(docker exec teleop-client-dev stat -c %y "$CONTAINER_QML" 2>/dev/null | cut -d'.' -f1)
    HOST_MODIFY=$(stat -c %y "$QML_FILE" 2>/dev/null | cut -d'.' -f1)
    
    echo "  容器内修改时间: $CONTAINER_MODIFY"
    echo "  宿主机修改时间: $HOST_MODIFY"
    
    if [ "$CONTAINER_MODIFY" != "$HOST_MODIFY" ]; then
        echo -e "${YELLOW}  ⚠ 文件修改时间不一致，可能需要同步${NC}"
    else
        echo -e "${GREEN}  ✓ 文件修改时间一致${NC}"
    fi
else
    echo -e "${RED}❌ 容器内 QML 文件不存在${NC}"
fi
echo ""

# 3. 编译客户端（确保使用最新代码）
echo -e "${YELLOW}[3/5] 编译客户端（确保使用最新代码）...${NC}"
echo "  这可能需要几分钟..."

# 尝试清理 build 目录
docker exec teleop-client-dev bash -c "cd /workspace/client && pkill -9 RemoteDrivingClient 2>/dev/null || true; sleep 1; rm -rf build/.qt 2>/dev/null || true" || true

# 编译客户端
docker exec teleop-client-dev bash -c "cd /workspace/client && bash build.sh 2>&1" | tail -40

if docker exec teleop-client-dev test -x /workspace/client/build/RemoteDrivingClient 2>/dev/null; then
    echo -e "${GREEN}✓ 客户端编译成功${NC}"
else
    echo -e "${YELLOW}⚠ 使用 /tmp/client-build 目录编译...${NC}"
    docker exec teleop-client-dev bash -c "mkdir -p /tmp/client-build && cd /tmp/client-build && cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug && make -j4 2>&1" | tail -20
    if docker exec teleop-client-dev test -x /tmp/client-build/RemoteDrivingClient 2>/dev/null; then
        echo -e "${GREEN}✓ 客户端在 /tmp/client-build 编译成功${NC}"
    else
        echo -e "${RED}❌ 客户端编译失败${NC}"
        exit 1
    fi
fi
echo ""

# 4. 启动客户端并捕获日志
echo -e "${YELLOW}[4/5] 启动客户端并捕获日志（10秒）...${NC}"
echo "日志将保存到: $LOG_FILE"

# 停止可能正在运行的客户端
docker stop teleop-client-dev 2>/dev/null || true
sleep 2

# 启动客户端并捕获日志
timeout 15 docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml run --rm \
    -e DISPLAY="$DISPLAY" \
    -e QT_QPA_PLATFORM=xcb \
    -e QT_LOGGING_RULES="qt.qpa.*=false" \
    -e CLIENT_RESET_LOGIN=1 \
    client-dev bash -c '
        cd /workspace/client
        if [ -x /tmp/client-build/RemoteDrivingClient ]; then
            echo "[TEST] 使用 /tmp/client-build 中的客户端"
            echo "[TEST] 当前工作目录: $(pwd)"
            echo "[TEST] QML 文件路径: $(ls -la qml/DrivingInterface.qml 2>&1)"
            cd /tmp/client-build
            timeout 10 ./RemoteDrivingClient --reset-login 2>&1 || true
        elif [ -x build/RemoteDrivingClient ]; then
            echo "[TEST] 使用已编译的客户端: build/RemoteDrivingClient"
            echo "[TEST] 当前工作目录: $(pwd)"
            echo "[TEST] QML 文件路径: $(ls -la qml/DrivingInterface.qml 2>&1)"
            timeout 10 ./build/RemoteDrivingClient --reset-login 2>&1 || true
        else
            echo "[TEST] ❌ 客户端未找到"
            echo "[TEST] 检查 build 目录:"
            ls -la build/ 2>&1 || echo "build 目录不存在"
            echo "[TEST] 检查 /tmp/client-build 目录:"
            ls -la /tmp/client-build/ 2>&1 || echo "/tmp/client-build 目录不存在"
            exit 1
        fi
    ' 2>&1 | tee "$LOG_FILE" || true

echo ""

# 5. 分析日志
echo -e "${YELLOW}[5/5] 分析日志...${NC}"
echo ""

if [ ! -f "$LOG_FILE" ]; then
    echo -e "${RED}❌ 日志文件不存在${NC}"
    exit 1
fi

# 检查 QML 加载日志
echo "=== QML 文件加载信息 ==="
if grep -q "\[QML_LOAD\]" "$LOG_FILE"; then
    grep "\[QML_LOAD\]" "$LOG_FILE" | head -30
    echo ""
    
    # 检查关键验证点
    VERIFICATION_PASSED=0
    
    if grep -q "\[QML_LOAD\] ✓ 找到 Layout.preferredWidth: 35" "$LOG_FILE"; then
        echo -e "${GREEN}✓ 验证通过: Layout.preferredWidth: 35${NC}"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "${RED}✗ 验证失败: Layout.preferredWidth: 35${NC}"
    fi
    
    if grep -q "\[QML_LOAD\] ✓ 找到 spacing: 16" "$LOG_FILE"; then
        echo -e "${GREEN}✓ 验证通过: spacing: 16${NC}"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "${RED}✗ 验证失败: spacing: 16${NC}"
    fi
    
    if grep -q "\[QML_LOAD\] ✓ 找到 width: 50; height: 42" "$LOG_FILE"; then
        echo -e "${GREEN}✓ 验证通过: width: 50; height: 42${NC}"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "${RED}✗ 验证失败: width: 50; height: 42${NC}"
    fi
    
    if grep -q "\[QML_LOAD\] ✓ 找到 width: 90; height: 40" "$LOG_FILE"; then
        echo -e "${GREEN}✓ 验证通过: width: 90; height: 40${NC}"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "${RED}✗ 验证失败: width: 90; height: 40${NC}"
    fi
    
    if grep -q "\[QML_LOAD\] ✓ 找到进度条长度缩短" "$LOG_FILE"; then
        echo -e "${GREEN}✓ 验证通过: 进度条长度缩短${NC}"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "${RED}✗ 验证失败: 进度条长度缩短${NC}"
    fi
    
    echo ""
    echo "验证结果: $VERIFICATION_PASSED/5 通过"
    
    if [ $VERIFICATION_PASSED -eq 5 ]; then
        echo -e "${GREEN}========== 所有验证通过！修改已生效 ==========${NC}"
        exit 0
    else
        echo -e "${RED}========== 部分验证失败，需要检查 ==========${NC}"
        exit 1
    fi
else
    echo -e "${RED}❌ 未找到 QML_LOAD 日志，客户端可能未正常启动${NC}"
    echo "最后50行日志:"
    tail -50 "$LOG_FILE"
    exit 1
fi
