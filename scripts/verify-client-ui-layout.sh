#!/usr/bin/env bash
# 验证客户端 UI 布局：右视图、高精地图可见
# 用法：bash scripts/verify-client-ui-layout.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo "========== 客户端 UI 布局验证（右视图 + 高精地图）=========="
echo ""

PASS=0
FAIL=0

# 1. 右视图
if grep -q 'title: "右视图"' client/qml/DrivingInterface.qml; then
    echo "  ✓ 右视图 VideoPanel 存在"
    ((PASS++)) || true
else
    echo "  ✗ 右视图缺失"
    ((FAIL++)) || true
fi

# 2. 高精地图
if grep -q 'text: "高精地图"' client/qml/DrivingInterface.qml; then
    echo "  ✓ 高精地图面板存在"
    ((PASS++)) || true
else
    echo "  ✗ 高精地图缺失"
    ((FAIL++)) || true
fi

# 3. 右列 minimumWidth
if grep -q 'Layout.minimumWidth: 220' client/qml/DrivingInterface.qml; then
    echo "  ✓ 右列有 minimumWidth: 220"
    ((PASS++)) || true
else
    echo "  ✗ 右列无 minimumWidth: 220"
    ((FAIL++)) || true
fi

# 4. 右视图 minimumHeight
if grep -q 'Layout.minimumHeight: 100' client/qml/DrivingInterface.qml; then
    echo "  ✓ 右视图有 minimumHeight: 100"
    ((PASS++)) || true
else
    echo "  ✗ 右视图无 minimumHeight"
    ((FAIL++)) || true
fi

# 5. 中列无 fillWidth（避免挤压右列）
if ! grep -A2 '中列：主视图' client/qml/DrivingInterface.qml | grep -q 'Layout.fillWidth: true'; then
    echo "  ✓ 中列未使用 fillWidth（不挤压右列）"
    ((PASS++)) || true
else
    echo "  ✗ 中列使用 fillWidth 可能挤压右列"
    ((FAIL++)) || true
fi

# 6. 编译验证
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
fi

echo ""
echo "  通过: $PASS  失败: $FAIL"
if [ "$FAIL" -eq 0 ] && [ "$PASS" -ge 4 ]; then
    echo -e "${GREEN}========== 验证通过 ==========${NC}"
    exit 0
else
    echo -e "${RED}========== 验证未通过 ==========${NC}"
    exit 1
fi
