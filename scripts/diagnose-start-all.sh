#!/usr/bin/env bash
# 快速诊断 start-all-nodes-and-verify.sh 失败原因
# 用法：bash scripts/diagnose-start-all.sh
# 输出：失败阶段、可能原因、建议命令

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}========== start-all-nodes-and-verify 诊断 ==========${NC}"
echo ""

ISSUES=()

# 1. Client-Dev 镜像
if ! docker image inspect remote-driving-client-dev:full >/dev/null 2>&1; then
    ISSUES+=("Client-Dev 镜像不存在 (remote-driving-client-dev:full)")
    ISSUES+=("  → 执行: bash scripts/build-client-dev-full-image.sh")
else
    echo -e "${GREEN}[OK]${NC} Client-Dev 镜像存在"
fi

# 2. libdatachannel（build-client-dev-full-image 依赖）
if [ ! -d "$PROJECT_ROOT/client/deps/libdatachannel-install" ] || [ -z "$(ls -A "$PROJECT_ROOT/client/deps/libdatachannel-install" 2>/dev/null)" ]; then
    ISSUES+=("libdatachannel 未安装 (client/deps/libdatachannel-install)")
    ISSUES+=("  → 执行: bash scripts/install-libdatachannel-for-client.sh")
else
    echo -e "${GREEN}[OK]${NC} libdatachannel 已安装"
fi

# 3. DISPLAY
if [ -z "$DISPLAY" ]; then
    ISSUES+=("DISPLAY 未设置（客户端/CARLA 窗口无法显示）")
    ISSUES+=("  → 在有图形桌面的终端运行；或 export DISPLAY=:0")
else
    echo -e "${GREEN}[OK]${NC} DISPLAY=$DISPLAY"
fi

# 4. X11 socket
X_NUM="${DISPLAY#*:}"; X_NUM="${X_NUM%%.*}"
if [ -n "$X_NUM" ] && [ ! -S "/tmp/.X11-unix/X${X_NUM}" ]; then
    ISSUES+=("X11 socket /tmp/.X11-unix/X${X_NUM} 不存在")
    ISSUES+=("  → 确保在有图形桌面的环境中运行")
else
    echo -e "${GREEN}[OK]${NC} X11 socket 存在"
fi

# 5. 端口（仅信息，不阻塞）
echo -e "${GREEN}[OK]${NC} 端口检查（若有冲突，start-all 会提示）"

# 6. teleop-network
if ! docker network inspect teleop-network >/dev/null 2>&1; then
    echo -e "${YELLOW}[INFO]${NC} teleop-network 不存在（脚本会自动创建）"
else
    echo -e "${GREEN}[OK]${NC} teleop-network 存在"
fi

# 7. 最近编译结果
RECENT=$(ls -td ${TMPDIR:-/tmp}/compile-verify-* 2>/dev/null | head -1)
if [ -n "$RECENT" ] && [ -d "$RECENT" ]; then
    for f in backend vehicle client-dev; do
        if [ -f "$RECENT/${f}.status" ]; then
            st=$(cat "$RECENT/${f}.status" 2>/dev/null)
            if [ "$st" = "1" ]; then
                ISSUES+=("最近一次 ${f} 编译失败，日志: $RECENT/${f}.log")
            fi
        fi
    done
fi

# 8. Compose 服务状态
COMPOSE_PS="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml"
if $COMPOSE_PS ps 2>/dev/null | grep -q "Up"; then
    echo -e "${GREEN}[OK]${NC} 已有 Compose 服务在运行"
else
    echo -e "${YELLOW}[INFO]${NC} 当前无 Compose 服务运行（正常，脚本会启动）"
fi

echo ""
if [ ${#ISSUES[@]} -gt 0 ]; then
    echo -e "${RED}========== 发现以下问题 ==========${NC}"
    for line in "${ISSUES[@]}"; do
        echo -e "  $line"
    done
    echo ""
    echo -e "${YELLOW}建议：${NC}"
    echo "  1. 按上方提示逐项修复"
    echo "  2. 若 Client-Dev 镜像缺失，先执行: bash scripts/build-client-dev-full-image.sh"
    echo "  3. 修复后重跑: bash scripts/start-all-nodes-and-verify.sh"
    echo ""
    echo "  快速跳过编译（镜像已就绪时）: SKIP_COMPILE=1 bash scripts/start-all-nodes-and-verify.sh"
    exit 1
else
    echo -e "${GREEN}========== 前置检查通过 ==========${NC}"
    echo "  可直接执行: bash scripts/start-all-nodes-and-verify.sh"
    echo "  若仍失败，请将完整终端输出保存，并参考 docs/TROUBLESHOOTING_RUNBOOK.md 第 11 节"
    exit 0
fi
