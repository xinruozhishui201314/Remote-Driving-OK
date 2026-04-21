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

log() { printf '%s\n' "$*" >&2; }

# 1. 允许 Docker 连接 X11（必须，否则容器内 Qt/CARLA 无法弹出窗口）
#    +local:docker 允许 docker组；+local: 允许本机所有连接；+SI:localuser:root 允许容器内 root
if command -v xhost >/dev/null 2>&1; then
  log "正在设置 X11 访问权限..."
  xhost +local:docker >/dev/null 2>&1 || true
  xhost +local: >/dev/null 2>&1 || true
  xhost +SI:localuser:root >/dev/null 2>&1 || true
  echo -e "${GREEN}[OK] xhost 已执行${NC}"
else
  echo -e "${YELLOW}[WARN] 未找到 xhost${NC}"
fi

# 1.5 检查并安装/配置 NVIDIA 环境
log "正在检查 NVIDIA 运行环境..."
if ! command -v nvidia-smi >/dev/null 2>&1; then
  echo -e "${RED}[ERROR] 未检测到 NVIDIA 驱动，请先安装驱动。${NC}"
else
  echo -e "${GREEN}[OK] 检测到 NVIDIA 驱动: $(nvidia-smi -L | head -n 1)${NC}"
  
  if ! command -v nvidia-ctk >/dev/null 2>&1; then
    echo -e "${YELLOW}[INFO] 未检测到 nvidia-container-toolkit，尝试安装...${NC}"
    # 这里提供安装指令，因为脚本可能没 sudo 权限，或者用户想确认
    echo -e "请执行以下命令完成安装:"
    echo -e "curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | sudo gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg"
    echo -e "curl -s -L https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list | sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' | sudo tee /etc/apt/sources.list.d/nvidia-container-toolkit.list"
    echo -e "sudo apt-get update && sudo apt-get install -y nvidia-container-toolkit"
  else
    echo -e "${GREEN}[OK] 检测到 nvidia-container-toolkit${NC}"
    if ! docker info 2>/dev/null | grep -i "Runtimes:" | grep -qi "nvidia"; then
      echo -e "${YELLOW}[INFO] Docker 未配置 nvidia runtime，正在配置...${NC}"
      sudo nvidia-ctk runtime configure --runtime=docker
      sudo systemctl restart docker
      echo -e "${GREEN}[OK] Docker runtime 已配置并重启${NC}"
    else
      echo -e "${GREEN}[OK] Docker 已配置 nvidia runtime${NC}"
    fi
  fi
fi

# 2. 检查/设置 DISPLAY（未设置时用 :0，便于 compose 与脚本使用；客户端在容器内默认使用 :0 见 start-all-nodes-and-verify.sh）
if [ -z "$DISPLAY" ]; then
  export DISPLAY=:0
  echo -e "${GREEN}[OK] DISPLAY 已设置: DISPLAY=:0${NC}"
else
  echo -e "${GREEN}[OK] DISPLAY=$DISPLAY${NC}"
fi

# 3. X11 cookie（compose 绑定 NVIDIA overlay 等需要 XAUTHORITY_HOST_PATH；GDM 常无 ~/.Xauthority）
if [ -z "${XAUTHORITY_HOST_PATH:-}" ]; then
  _xprobe=""
  for _c in "${HOME}/.Xauthority" "/run/user/$(id -u)/gdm/Xauthority"; do
    if [ -f "$_c" ] && [ -r "$_c" ] && [ -s "$_c" ]; then
      _xprobe="$_c"
      break
    fi
  done
  if [ -z "$_xprobe" ] && [ -n "${XDG_RUNTIME_DIR:-}" ]; then
    _c="${XDG_RUNTIME_DIR}/gdm/Xauthority"
    if [ -f "$_c" ] && [ -r "$_c" ] && [ -s "$_c" ]; then
      _xprobe="$_c"
    fi
  fi
  if [ -n "$_xprobe" ]; then
    export XAUTHORITY_HOST_PATH="$(cd "$(dirname "$_xprobe")" && pwd)/$(basename "$_xprobe")"
    echo -e "${GREEN}[OK] 已自动配置 XAUTHORITY_HOST_PATH=${XAUTHORITY_HOST_PATH}${NC}"
  else
    echo -e "${YELLOW}[WARN] 未找到可读非空的 X11 cookie（已尝试 ~/.Xauthority 与 /run/user/<uid>/gdm/Xauthority 等）。使用 NVIDIA GL overlay 前请登录图形会话或手动 export XAUTHORITY_HOST_PATH=...${NC}"
  fi
else
  echo -e "${GREEN}[OK] 已使用环境变量 XAUTHORITY_HOST_PATH=${XAUTHORITY_HOST_PATH}${NC}"
fi

echo -e "${CYAN}详见: docs/RUN_ENVIRONMENT.md${NC}"
