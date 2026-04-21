#!/usr/bin/env bash
# 严格预检：TELEOP_CLIENT_NVIDIA_GL=1 使用 docker-compose.client-nvidia-gl*.yml 之前必须满足的条件。
#
# 检查项（失败非 0 退出）：
#   1) Host NVIDIA 驱动：nvidia-smi 是否可见 GPU
#   2) Host NVIDIA Container Toolkit：nvidia-ctk 是否安装，docker --gpus all 是否可用
#   3) X11 cookie：解析为绝对路径、存在、可读、非空文件
#   4) DISPLAY：非空（可用 TELEOP_CLIENT_NVIDIA_GL_SKIP_DISPLAY_CHECK=1 跳过，仅无头自动化）
#   5) Docker 可将 GPU 交给容器：docker run --rm --gpus all …（可用 TELEOP_CLIENT_NVIDIA_GL_SKIP_DOCKER_GPU_TEST=1 跳过）
#   6) 与主链路相同的 compose 合并能通过 `docker compose … config -q`
#
# 自动修复项（需要 sudo 时会提示）：
#   - 检查并建议安装 nvidia-container-toolkit
#   - 配置 docker nvidia runtime
#   - 设置 xhost 权限
#
# 用法：
#   bash scripts/verify-client-nvidia-gl-prereqs.sh
#   eval "$(bash scripts/verify-client-nvidia-gl-prereqs.sh --emit-export)"   # 导出 XAUTHORITY_HOST_PATH 与 TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE
#
# 解析顺序（cookie 文件）：
#   已设置 XAUTHORITY_HOST_PATH → 非空则只用该路径（无效则失败，不自动改）
#   否则：XAUTHORITY（若为绝对路径且为可读非空文件）→ ~/.Xauthority →
#   常见 GDM：/run/user/$UID/gdm/Xauthority → $XDG_RUNTIME_DIR/gdm/Xauthority
#   均无效时回退为 ~/.Xauthority（由校验步骤报出明确错误）
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

log() { printf '%s\n' "$*" >&2; }
die() { log "[$0] FATAL: $*"; exit 1; }

COMPOSE_BASE=(
  docker compose
  -f "$PROJECT_ROOT/docker-compose.yml"
  -f "$PROJECT_ROOT/docker-compose.vehicle.dev.yml"
  -f "$PROJECT_ROOT/docker-compose.dev.yml"
)

resolve_xauthority_path() {
  local p c dir base candidates
  if [[ -n "${XAUTHORITY_HOST_PATH:-}" ]]; then
    p="${XAUTHORITY_HOST_PATH}"
  elif [[ -n "${XAUTHORITY:-}" && "${XAUTHORITY}" == /* && -f "${XAUTHORITY}" ]]; then
    p="${XAUTHORITY}"
  else
    p=""
    candidates=(
      "${HOME}/.Xauthority"
      "/run/user/$(id -u)/gdm/Xauthority"
    )
    if [[ -n "${XDG_RUNTIME_DIR:-}" ]]; then
      candidates+=("${XDG_RUNTIME_DIR}/gdm/Xauthority")
    fi
    for c in "${candidates[@]}"; do
      if [[ -f "$c" ]] && [[ -r "$c" ]] && [[ -s "$c" ]]; then
        p="$c"
        log "[$0] INFO 未显式设置 XAUTHORITY_HOST_PATH，已自动选用有效 X11 cookie: ${p}"
        break
      fi
    done
    [[ -z "${p}" ]] && p="${HOME}/.Xauthority"
  fi
  if [[ "${p}" != /* ]]; then
    die "X authority 路径须为绝对路径，当前: ${p}（请设置 XAUTHORITY_HOST_PATH）"
  fi
  dir="$(cd "$(dirname "$p")" && pwd)"
  base="$(basename "$p")"
  printf '%s/%s' "$dir" "$base"
}

validate_xauthority_file() {
  local path="$1"
  [[ -n "${path}" ]] || die "X authority 路径为空"
  [[ -e "${path}" ]] || die "X authority 不存在: ${path}（设置 XAUTHORITY_HOST_PATH 指向有效 cookie，或先登录图形会话生成 ~/.Xauthority）"
  [[ -f "${path}" ]] || die "X authority 不是常规文件: ${path}"
  [[ -r "${path}" ]] || die "X authority 不可读: ${path}"
  local sz
  sz="$(wc -c <"${path}" | tr -d ' ')"
  [[ "${sz}" -gt 0 ]] || die "X authority 为空文件: ${path}"
  log "[$0] OK XAUTHORITY_HOST_PATH -> ${path} (${sz} bytes)"
}

validate_display() {
  if [[ "${TELEOP_CLIENT_NVIDIA_GL_SKIP_DISPLAY_CHECK:-0}" == "1" ]]; then
    log "[$0] WARN 已跳过 DISPLAY 检查（TELEOP_CLIENT_NVIDIA_GL_SKIP_DISPLAY_CHECK=1）"
    return 0
  fi
  [[ -n "${DISPLAY:-}" ]] || die "DISPLAY 未设置，容器无法连接宿主 X11。请 export DISPLAY=:0（或当前会话值），或设 TELEOP_CLIENT_NVIDIA_GL_SKIP_DISPLAY_CHECK=1"
  log "[$0] OK DISPLAY=${DISPLAY}"
}

validate_host_nvidia_driver() {
  log "[$0] 正在检查宿主机 NVIDIA 驱动..."
  if ! command -v nvidia-smi >/dev/null 2>&1; then
    die "宿主机未检测到 nvidia-smi。请先安装 NVIDIA 驱动 (如 sudo apt install nvidia-driver-535)。"
  fi
  if ! nvidia-smi -L >/dev/null 2>&1; then
    die "nvidia-smi 运行失败，可能驱动未加载或硬件异常。请检查: dmesg | grep -i nvidia"
  fi
  local gpu_info
  gpu_info=$(nvidia-smi -L | head -n 1)
  log "[$0] OK 宿主机 NVIDIA 驱动已就绪: ${gpu_info}"
}

validate_nvidia_toolkit_and_remediate() {
  log "[$0] 正在检查 NVIDIA Container Toolkit..."
  local has_toolkit=1
  if ! command -v nvidia-ctk >/dev/null 2>&1; then
    has_toolkit=0
  fi

  if [[ "${has_toolkit}" -eq 0 ]]; then
    log "[$0] WARN 未检测到 nvidia-container-toolkit。"
    log "[$0]      请执行以下命令安装 (Ubuntu/Debian):"
    log "          curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | sudo gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg"
    log "          curl -s -L https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list | sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' | sudo tee /etc/apt/sources.list.d/nvidia-container-toolkit.list"
    log "          sudo apt-get update && sudo apt-get install -y nvidia-container-toolkit"
    die "缺失核心组件: nvidia-container-toolkit"
  fi

  # 检查是否配置了 docker runtime
  if ! docker info 2>/dev/null | grep -i "Runtimes:" | grep -qi "nvidia"; then
    log "[$0] WARN Docker 未注册 nvidia runtime。"
    log "[$0]      正在尝试自动配置 (可能需要 sudo)..."
    if command -v sudo >/dev/null 2>&1; then
      sudo nvidia-ctk runtime configure --runtime=docker
      sudo systemctl restart docker
      log "[$0] OK 已自动配置并重启 Docker。请稍候..."
      sleep 2
    else
      die "请手动执行: sudo nvidia-ctk runtime configure --runtime=docker && sudo systemctl restart docker"
    fi
  fi
  log "[$0] OK NVIDIA Container Toolkit 已正确配置"
}

validate_x11_permissions() {
  if command -v xhost >/dev/null 2>&1; then
    log "[$0] 正在自动设置 X11 访问权限 (xhost +local:docker)..."
    xhost +local:docker >/dev/null 2>&1 || true
    xhost +local:root >/dev/null 2>&1 || true
    xhost +si:localuser:root >/dev/null 2>&1 || true
  fi
}

validate_docker_gpu() {
  if [[ "${TELEOP_CLIENT_NVIDIA_GL_SKIP_DOCKER_GPU_TEST:-0}" == "1" ]]; then
    log "[$0] WARN 已跳过 docker --gpus 探测（TELEOP_CLIENT_NVIDIA_GL_SKIP_DOCKER_GPU_TEST=1）"
    return 0
  fi
  command -v docker >/dev/null 2>&1 || die "未找到 docker 命令"
  local out ec
  local ec=0
  log "[$0] 正在探测 Docker GPU 穿透能力 (docker run --rm --gpus all)..."
  out="$(docker run --rm --gpus all remote-driving-client-dev:full nvidia-smi -L 2>&1)" || ec=$?
  if [[ "${ec}" -ne 0 ]]; then
    log "${out}"
    log "[$0] ERROR: docker --gpus all 运行失败。这通常意味着 nvidia-container-toolkit 虽然安装了但未正确配置或 Docker 未重启。"
    die "docker GPU 穿透失败。请运行: sudo nvidia-ctk runtime configure --runtime=docker && sudo systemctl restart docker"
  fi
  log "[$0] OK Docker 容器内已可见 GPU: $(echo "${out}" | head -n 1)"

  # 额外检查 nvidia runtime 是否注册（CARLA 仿真器 docker-compose.carla.yml 强依赖此名称）
  if ! docker info 2>/dev/null | grep -i "Runtimes:" | grep -qi "nvidia"; then
    log "[$0] WARN Docker 未注册 nvidia runtime (仅发现: $(docker info 2>/dev/null | grep -i "Runtimes:" | sed 's/.*Runtimes://'))"
    log "[$0]      这不影响客户端 GPU，但会导致 CARLA 容器启动失败。修复: sudo nvidia-ctk runtime configure --runtime=docker && sudo systemctl restart docker"
  else
    log "[$0] OK Docker 已注册 nvidia runtime"
  fi
}

pick_nvidia_compose_file() {
  local try_user="${TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE:-}"
  if [[ -n "${try_user}" ]]; then
    if [[ "${try_user}" == /* ]]; then
      die "TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE 须为相对仓库根的路径（例如 docker-compose.client-nvidia-gl.yml），禁止绝对路径"
    fi
    local fp="${PROJECT_ROOT}/${try_user}"
    [[ -f "${fp}" ]] || die "TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE 不是仓库内可读文件: ${try_user}"
    if ! "${COMPOSE_BASE[@]}" -f "${fp}" config -q 2>"/tmp/teleop-nv-compose.err.$$"; then
      log "docker compose config 失败（使用 TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE=${try_user}）："
      cat "/tmp/teleop-nv-compose.err.$$" >&2 || true
      rm -f "/tmp/teleop-nv-compose.err.$$"
      die "请修正 compose 或 Docker/Compose 版本后重试"
    fi
    rm -f "/tmp/teleop-nv-compose.err.$$"
    printf '%s' "${try_user}"
    return 0
  fi

  local candidates=(
    "docker-compose.client-nvidia-gl.yml"
    "docker-compose.client-nvidia-gl.deploy.yml"
  )
  local f fp err
  for f in "${candidates[@]}"; do
    fp="${PROJECT_ROOT}/${f}"
    [[ -f "${fp}" ]] || continue
    err="/tmp/teleop-nv-compose.err.$$"
    if "${COMPOSE_BASE[@]}" -f "${fp}" config -q 2>"${err}"; then
      rm -f "${err}"
      log "[$0] OK compose 合并通过: ${f}"
      printf '%s' "${f}"
      return 0
    fi
    log "[$0] compose 合并未通过: ${f}（将尝试下一候选）"
    cat "${err}" >&2 || true
    rm -f "${err}"
  done
  die "docker-compose.client-nvidia-gl.yml 与 .deploy.yml 均无法通过「docker compose config」。请升级 Docker Engine + Compose v2，或设置 TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE 指向可用片段"
}

main() {
  cd "${PROJECT_ROOT}"

  local mode="${1:-}"

  validate_host_nvidia_driver
  validate_nvidia_toolkit_and_remediate
  validate_x11_permissions

  local x_path
  x_path="$(resolve_xauthority_path)"
  x_path="$(cd "$(dirname "${x_path}")" && pwd)/$(basename "${x_path}")"
  validate_xauthority_file "${x_path}"
  export XAUTHORITY_HOST_PATH="${x_path}"

  validate_display
  validate_docker_gpu

  local picked
  picked="$(pick_nvidia_compose_file)"
  export TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE="${picked}"

  if [[ "${mode}" == "--emit-export" ]]; then
    printf "export XAUTHORITY_HOST_PATH=%q\n" "${XAUTHORITY_HOST_PATH}"
    printf "export TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE=%q\n" "${TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE}"
    printf "export __GLX_VENDOR_LIBRARY_NAME=nvidia\n"
    printf "export TELEOP_CLIENT_NVIDIA_GL=1\n"
    printf "export NVIDIA_DRIVER_CAPABILITIES=all\n"
    printf "export QT_XCB_GL_INTEGRATION=glx\n"
    return 0
  fi

  log "[$0] 全部预检通过。选用 NVIDIA overlay: ${picked}"
}

main "$@"
