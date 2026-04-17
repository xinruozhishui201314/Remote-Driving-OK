#!/usr/bin/env bash
# 严格预检：TELEOP_CLIENT_NVIDIA_GL=1 使用 docker-compose.client-nvidia-gl*.yml 之前必须满足的条件。
#
# 检查项（失败非 0 退出）：
#   1) X11 cookie：解析为绝对路径、存在、可读、非空文件
#   2) DISPLAY：非空（可用 TELEOP_CLIENT_NVIDIA_GL_SKIP_DISPLAY_CHECK=1 跳过，仅无头自动化）
#   3) Docker 可将 GPU 交给容器：docker run --rm --gpus all …（可用 TELEOP_CLIENT_NVIDIA_GL_SKIP_DOCKER_GPU_TEST=1 跳过）
#   4) 与主链路相同的 compose 合并能通过 `docker compose … config -q`
#      - 默认依次尝试 docker-compose.client-nvidia-gl.yml（gpus: all）与
#        docker-compose.client-nvidia-gl.deploy.yml（deploy.reservations.devices）
#      - 已设置 TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE 时只校验该文件
#
# 用法：
#   bash scripts/verify-client-nvidia-gl-prereqs.sh
#   eval "$(bash scripts/verify-client-nvidia-gl-prereqs.sh --emit-export)"   # 导出 XAUTHORITY_HOST_PATH 与 TELEOP_CLIENT_NVIDIA_GL_COMPOSE_FILE
#
# 解析顺序（cookie 文件）：
#   XAUTHORITY_HOST_PATH → 环境变量 XAUTHORITY（若为绝对路径且为文件）→ ${HOME}/.Xauthority
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
  local p
  if [[ -n "${XAUTHORITY_HOST_PATH:-}" ]]; then
    p="${XAUTHORITY_HOST_PATH}"
  elif [[ -n "${XAUTHORITY:-}" && "${XAUTHORITY}" == /* && -f "${XAUTHORITY}" ]]; then
    p="${XAUTHORITY}"
  else
    p="${HOME}/.Xauthority"
  fi
  # 绝对化（compose bind source 必须稳定）
  if [[ "${p}" == /* ]]; then
    printf '%s' "${p}"
    return 0
  fi
  die "X authority 路径须为绝对路径，当前: ${p}（请设置 XAUTHORITY_HOST_PATH）"
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

validate_docker_gpu() {
  if [[ "${TELEOP_CLIENT_NVIDIA_GL_SKIP_DOCKER_GPU_TEST:-0}" == "1" ]]; then
    log "[$0] WARN 已跳过 docker --gpus 探测（TELEOP_CLIENT_NVIDIA_GL_SKIP_DOCKER_GPU_TEST=1）"
    return 0
  fi
  command -v docker >/dev/null 2>&1 || die "未找到 docker 命令"
  local out ec
  local ec=0
  out="$(docker run --rm --gpus all busybox:latest true 2>&1)" || ec=$?
  if [[ "${ec}" -ne 0 ]]; then
    log "${out}"
    die "docker run --gpus all 失败（exit=${ec}）。请安装并配置 nvidia-container-toolkit，并重启 Docker；见 https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/"
  fi
  log "[$0] OK docker run --rm --gpus all busybox:latest true"

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
    return 0
  fi

  log "[$0] 全部预检通过。选用 NVIDIA overlay: ${picked}"
}

main "$@"
