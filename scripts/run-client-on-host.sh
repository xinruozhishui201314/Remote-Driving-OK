#!/usr/bin/env bash
# 在宿主机直接运行 RemoteDrivingClient（不经 client-dev 容器），用于对照：
#   若宿主上 GL_RENDERER 为 NVIDIA/硬件而容器内为 llvmpipe，则根因在容器 GL / 驱动注入 / DISPLAY。
#
# 默认 URL 对齐 docker-compose.dev.yml 端口映射：backend 8081→8080、Keycloak 8080、MQTT 1883、ZLM 80。
#
# 环境变量：
#   CLIENT_HOST_BIN   可执行文件绝对路径（优先）
#   BACKEND_URL / KEYCLOAK_URL / MQTT_BROKER_URL / ZLM_VIDEO_URL / ZLM_WHEP_URL / ZLM_API_SECRET
#   DISPLAY           默认 :0
#
# 取证（可选）：
#   CLIENT_X11_DEEP_DIAG=1 CLIENT_XWININFO_SNAPSHOT=1 CLIENT_QPA_XCB_DEBUG=1
#
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

resolve_bin() {
  if [[ -n "${CLIENT_HOST_BIN:-}" && -x "${CLIENT_HOST_BIN}" ]]; then
    echo "$CLIENT_HOST_BIN"
    return 0
  fi
  local c
  for c in \
    "$PROJECT_ROOT/client/build/RemoteDrivingClient" \
    "$PROJECT_ROOT/client/build/Debug/RemoteDrivingClient" \
    "$PROJECT_ROOT/client/build/Release/RemoteDrivingClient" \
    "$PROJECT_ROOT/build/client/RemoteDrivingClient"; do
    if [[ -x "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

BIN="$(resolve_bin)" || {
  echo "[run-client-on-host] 未找到可执行的 RemoteDrivingClient。请先 cmake --build client/build，或 export CLIENT_HOST_BIN=/path/to/RemoteDrivingClient" >&2
  exit 1
}

export DISPLAY="${DISPLAY:-:0}"
export BACKEND_URL="${BACKEND_URL:-http://127.0.0.1:8081}"
export DEFAULT_SERVER_URL="${DEFAULT_SERVER_URL:-$BACKEND_URL}"
export KEYCLOAK_URL="${KEYCLOAK_URL:-http://127.0.0.1:8080}"
export MQTT_BROKER_URL="${MQTT_BROKER_URL:-mqtt://127.0.0.1:1883}"
export ZLM_VIDEO_URL="${ZLM_VIDEO_URL:-http://127.0.0.1:80}"
export ZLM_WHEP_URL="${ZLM_WHEP_URL:-http://127.0.0.1:80/index/api/webrtc}"

if [[ -z "${ZLM_API_SECRET:-}" ]]; then
  echo "[run-client-on-host] 提示: ZLM_API_SECRET 未设置；若 ZLM API 失败请与 docker-compose 中 backend/client 使用的 secret 一致" >&2
fi

echo "[run-client-on-host] DISPLAY=$DISPLAY" >&2
echo "[run-client-on-host] BIN=$BIN" >&2
echo "[run-client-on-host] BACKEND_URL=$BACKEND_URL KEYCLOAK_URL=$KEYCLOAK_URL" >&2
exec "$BIN" "$@"
