#!/usr/bin/env bash
# 宿主机一次性设置：允许 Docker 容器内客户端显示 GUI，避免「客户端启动不了/无界面」。
# 用法：bash scripts/setup-host-for-client.sh
# 建议：首次运行或客户端无窗口时执行一次；start-all-nodes-and-verify.sh 启动客户端前也会自动调用。
# 详见：docs/RUN_ENVIRONMENT.md

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# 1. 允许 Docker 连接 X11（必须，否则容器内 Qt/CARLA 无法弹出窗口）
#    +local:docker 允许 docker 组；+local: 允许本机所有连接；+SI:localuser:root 允许容器内 root
if command -v xhost >/dev/null 2>&1; then
  xhost +local:docker 2>/dev/null || true
  xhost +local: 2>/dev/null || true
  xhost +SI:localuser:root 2>/dev/null || true
  echo -e "${GREEN}[OK] xhost 已执行（+local:docker +local: +SI:localuser:root），CARLA 与客户端可显示窗口${NC}"
else
  echo -e "${YELLOW}[WARN] 未找到 xhost，请在有 X11 的宿主机上执行: xhost +local:docker; xhost +local:; xhost +SI:localuser:root${NC}"
fi

# 2. 检查/设置 DISPLAY（未设置时用 :0，便于 compose 与脚本使用；客户端在容器内默认使用 :0 见 start-all-nodes-and-verify.sh）
if [ -z "$DISPLAY" ]; then
  export DISPLAY=:0
  echo -e "${GREEN}[OK] DISPLAY 已设置: DISPLAY=:0${NC}"
else
  echo -e "${GREEN}[OK] DISPLAY=$DISPLAY${NC}"
fi

echo -e "${CYAN}详见: docs/RUN_ENVIRONMENT.md${NC}"
