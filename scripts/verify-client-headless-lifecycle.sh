#!/usr/bin/env bash
# 客户端无头「启动 → 事件循环 → 退出」冒烟（覆盖 main 中 app.exec / aboutToQuit / ClientLogging::shutdown）
#
# 依赖：已编译的 RemoteDrivingClient；常见在 client-dev 容器或 client/build 目录。
# 参考：Qt 无头平台 QT_QPA_PLATFORM=offscreen；CI 常用定时 quit 做 smoke。
# 默认 CLIENT_STARTUP_TCP_GATE=0：本脚本不依赖 Backend/MQTT。完整栈上请启用 TCP 门禁；默认 TCP 目标随
# CLIENT_STARTUP_READINESS_PROFILE（容器内未设时常为 full→四端点）。仅 URL 校验仍执行，除非设
# CLIENT_SKIP_CONFIG_READINESS_GATE=1。
#
# 用法（仓库根目录）：
#   CLIENT_BIN=/path/to/RemoteDrivingClient ./scripts/verify-client-headless-lifecycle.sh
# client-dev 容器（compose 已挂载 ./scripts → /workspace/scripts）：
#   CLIENT_BIN=/tmp/client-build/RemoteDrivingClient bash /workspace/scripts/verify-client-headless-lifecycle.sh
#   SMOKE_MS=5000  # 可选，默认 8000
#
set -euo pipefail

SMOKE_MS="${SMOKE_MS:-8000}"
if [[ -z "${CLIENT_BIN:-}" ]]; then
  echo "[verify-client-headless-lifecycle] 请设置 CLIENT_BIN=.../RemoteDrivingClient" >&2
  exit 2
fi
if [[ ! -x "$CLIENT_BIN" && ! -f "$CLIENT_BIN" ]]; then
  echo "[verify-client-headless-lifecycle] 未找到可执行文件: $CLIENT_BIN" >&2
  exit 2
fi

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
# 无交互显示会话时显示门禁跳过；跳过 GL 探测避免无 GPU 环境失败
export CLIENT_SKIP_OPENGL_PROBE="${CLIENT_SKIP_OPENGL_PROBE:-1}"
# 本脚本不拉起 Backend/MQTT/Keycloak/ZLM，关闭 TCP 启动必过（完整链路见 compose / 生产环境）
export CLIENT_STARTUP_TCP_GATE="${CLIENT_STARTUP_TCP_GATE:-0}"
unset CLIENT_REQUIRE_HARDWARE_PRESENTATION CLIENT_TELOP_STATION 2>/dev/null || true
export CLIENT_HEADLESS_SMOKE_MS="$SMOKE_MS"

echo "[verify-client-headless-lifecycle] CLIENT_BIN=$CLIENT_BIN SMOKE_MS=$SMOKE_MS QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
exec "$CLIENT_BIN" "$@"
