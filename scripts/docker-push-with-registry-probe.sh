#!/usr/bin/env bash
# 使用自动化探测解决「docker push HOST/project/repo:tag」在仅 HTTP:80 的 Harbor 上失败（默认走 https:443 → EOF）的问题。
#
# 行为：
# 1) 解析镜像引用；若 registry 段已含显式端口（如 192.168.2.10:80/...），直接 docker push。
# 2) 若 registry 为「含点或 localhost 且无端口」的自定义仓库（如 192.168.2.10/...、harbor.example.com/...），
#    调用 verify-registry-docker-port-contract.sh；若 exit 2（无端口→443 与仅 HTTP 冲突），则自动：
#    docker tag 为 HOST:REGISTRY_HTTP_PORT/... 再 docker push。
# 3) Docker Hub 形式（首段无点、无冒号、非 localhost，如 user/repo）不做探测，直接 push。
#
# 用法：
#   ./scripts/docker-push-with-registry-probe.sh 192.168.2.10/remote_driving/zlmediakit:master
# 环境变量（与 verify 脚本对齐）：
#   REGISTRY_HTTP_PORT  默认 80（自动修复时追加的端口）
#   DRY_RUN=1           只打印将执行的 docker 命令
#   REGISTRY_PROBE_STRICT_IMPLICIT_443 传给探测脚本（默认 1）
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERIFY="${SCRIPT_DIR}/verify-registry-docker-port-contract.sh"
HTTP_PORT="${REGISTRY_HTTP_PORT:-80}"

usage() {
  echo "用法: $0 <镜像引用>" >&2
  echo "示例: $0 192.168.2.10/remote_driving/zlmediakit:master" >&2
  exit 1
}

# 首段是否为「自定义 registry」（含 . 或 : 或 localhost）；与 moby 参考实现一致的最小启发式。
is_custom_registry_segment() {
  local s=$1
  [[ "$s" == *.* || "$s" == *:* || "$s" == "localhost" ]]
}

# 首段是否已含显式端口（host:port / ipv4:port；不含 IPv6 方括号形式）
registry_has_explicit_port() {
  local s=$1
  [[ "$s" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}:[0-9]+$ ]] && return 0
  [[ "$s" =~ ^[a-zA-Z0-9.-]+:[0-9]+$ ]] && return 0
  return 1
}

# 从完整引用解析：registry_host（无端口）、repo_path（可含多级）、tag
parse_image_ref() {
  local img=$1
  if [[ "$img" != */* ]]; then
    echo "||${img}|latest"
    return 0
  fi
  local first rest name tag
  first="${img%%/*}"
  rest="${img#*/}"
  if ! is_custom_registry_segment "$first"; then
    echo "||${img}|"
    return 0
  fi
  if [[ "$rest" =~ ^(.+):([^:/]+)$ ]]; then
    name="${first}/${BASH_REMATCH[1]}"
    tag="${BASH_REMATCH[2]}"
  else
    name="${first}/${rest}"
    tag="latest"
  fi
  if registry_has_explicit_port "$first"; then
    local host_only pr
    host_only="${first%:*}"
    pr="${first#*:}"
    echo "${host_only}|${pr}|${name#*/}|${tag}"
    return 0
  fi
  echo "${first}||${name#*/}|${tag}"
}

dry() {
  if [[ "${DRY_RUN:-0}" == "1" ]]; then
    echo "[DRY_RUN] $*" >&2
    return 0
  fi
  "$@"
}

if [[ "${1:-}" == "--selftest" ]]; then
  if ! bash -n "$0"; then
    echo "[FAIL] bash -n" >&2
    exit 1
  fi
  out=$(parse_image_ref "192.168.2.10/remote_driving/zlmediakit:master")
  IFS='|' read -r h p r t <<<"$out"
  [[ "$h" == "192.168.2.10" && -z "$p" && "$r" == "remote_driving/zlmediakit" && "$t" == "master" ]] || {
    echo "[FAIL] parse bare IP: got host=$h port=$p repo=$r tag=$t" >&2
    exit 1
  }
  out=$(parse_image_ref "192.168.2.10:80/remote_driving/zlmediakit:master")
  IFS='|' read -r h p r t <<<"$out"
  [[ "$h" == "192.168.2.10" && "$p" == "80" ]] || {
    echo "[FAIL] parse with port: got host=$h port=$p" >&2
    exit 1
  }
  out=$(parse_image_ref "user/zlmediakit:master")
  IFS='|' read -r h p r t <<<"$out"
  [[ -z "$h" && -z "$p" ]] || {
    echo "[FAIL] dockerhub-like should skip probe: $out" >&2
    exit 1
  }
  echo "[OK] docker-push-with-registry-probe --selftest"
  exit 0
fi

[[ -n "${1:-}" ]] || usage
IMG=$1

if [[ "$IMG" == *@* ]]; then
  echo "[INFO] 含 digest 的引用不做端口重写，直接 push。" >&2
  dry docker push "$IMG"
  exit 0
fi

parsed=$(parse_image_ref "$IMG")
IFS='|' read -r probe_host registry_port_in_ref repo_path tag <<<"$parsed"

if [[ -z "$probe_host" ]]; then
  dry docker push "$IMG"
  exit 0
fi

if [[ -n "$registry_port_in_ref" ]]; then
  echo "[INFO] registry 已含显式端口 ${probe_host}:${registry_port_in_ref}，跳过契约探测。" >&2
  dry docker push "$IMG"
  exit 0
fi

echo "[INFO] 探测 registry 主机 ${probe_host}（无端口 → Docker 默认 https:443）..." >&2
set +e
REGISTRY_PROBE_HOST="$probe_host" REGISTRY_HTTP_PORT="$HTTP_PORT" \
  REGISTRY_PROBE_STRICT_IMPLICIT_443="${REGISTRY_PROBE_STRICT_IMPLICIT_443:-1}" \
  bash "$VERIFY" 2>&1
rc=$?
set -e

if [[ "$rc" -eq 0 ]]; then
  echo "[OK] 契约探测通过，直接 push ${IMG}" >&2
  dry docker push "$IMG"
  exit 0
fi

if [[ "$rc" -ne 2 ]]; then
  echo "[FAIL] 探测未返回可自动修复的 exit 2（实际 exit=$rc），请查看上方输出。" >&2
  exit "$rc"
fi

FIXED="${probe_host}:${HTTP_PORT}/${repo_path}:${tag}"
echo "[FIX] 根因：仅 HTTP 可达；重写为显式端口: ${FIXED}" >&2
dry docker tag "$IMG" "$FIXED"
dry docker push "$FIXED"
echo "[OK] 已推送 ${FIXED}（源标签 ${IMG} 仍保留在本地）" >&2
