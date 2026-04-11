#!/bin/bash
# 客户端 UI 自动化验证：编译 + 启动运行一段时间，无崩溃则通过
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

echo "=== 客户端 UI 自动化验证 ==="

# 1. 确保容器在运行
echo "[1/3] 检查 client-dev 容器..."
if ! docker compose ps client-dev 2>/dev/null | grep -q Up; then
    echo "启动 client-dev 容器..."
    docker compose up -d client-dev
    sleep 3
fi

# 2. 编译
echo "[2/3] 编译客户端..."
docker compose exec -T client-dev bash -c "mkdir -p /tmp/client-build && cd /tmp/client-build && ( [ ! -f CMakeCache.txt ] && cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug || true ) && make -j4" 2>&1 | tail -8
if ! docker compose exec -T client-dev test -f /tmp/client-build/RemoteDrivingClient 2>/dev/null; then
    echo "VERIFY_FAIL: 编译未生成可执行文件"
    exit 1
fi
echo "编译 OK"

# 3. 运行客户端若干秒，无崩溃即通过（从登录界面开始，不加载已保存登录状态）
# 使用 -T 避免分配 TTY，避免客户端关闭后终端卡住；timeout 放在容器内执行，客户端退出后整条链路立即结束
echo "[3/3] 运行客户端 6 秒验证（--reset-login 从登录界面启动）..."
export DISPLAY="${DISPLAY:-:0}"
RUN_RESULT=0
docker compose exec -T -e DISPLAY="$DISPLAY" -e CLIENT_RESET_LOGIN=1 -e CLIENT_STARTUP_TCP_GATE=0 client-dev bash -c "cd /tmp/client-build && timeout 6 ./RemoteDrivingClient --reset-login" 2>&1 || RUN_RESULT=$?

if [ "$RUN_RESULT" -eq 124 ]; then
    echo "VERIFY_OK: 客户端运行 6 秒无崩溃，界面加载正常"
    echo "  正常使用（不自动退出、可随意操作界面）请运行: bash scripts/run.sh  或  make run"
    exit 0
fi
if [ "$RUN_RESULT" -eq 0 ]; then
    echo "VERIFY_OK: 客户端正常退出"
    echo "  正常使用（不自动退出、可随意操作界面）请运行: bash scripts/run.sh  或  make run"
    exit 0
fi
echo "VERIFY_FAIL: 客户端异常退出 (code=$RUN_RESULT)"
exit 1
