#!/usr/bin/env bash
# V1：临时 Mosquitto + Paho 连接烟测（`test_mqtt_paho_broker_smoke`）。
# 需本机/容器已构建客户端且 CMake 找到 Paho（ENABLE_MQTT_PAHO=ON）。
#
# 用法（仓库根目录）：
#   CLIENT_BUILD_DIR=/path/to/client/build ./scripts/run-client-mqtt-integration-fixture.sh
#
# 环境变量：
#   CLIENT_BUILD_DIR   已 cmake --build 且含 test_mqtt_paho_broker_smoke 的目录
#   MQTT_TEST_PORT     默认 18883（避免与系统 1883 冲突）
#   DOCKER_IMAGE       默认 eclipse-mosquitto:2
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLIENT_BUILD_DIR="${CLIENT_BUILD_DIR:-$REPO_ROOT/client/build-ctest}"
PORT="${MQTT_TEST_PORT:-18883}"
IMAGE="${DOCKER_IMAGE:-eclipse-mosquitto:2}"

if [[ ! -x "$CLIENT_BUILD_DIR/test_mqtt_paho_broker_smoke" ]]; then
  echo "[mqtt-fixture] 未找到可执行文件: $CLIENT_BUILD_DIR/test_mqtt_paho_broker_smoke" >&2
  echo "  请先在该目录用 ENABLE_MQTT_PAHO=ON 配置并编译目标 test_mqtt_paho_broker_smoke。" >&2
  exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "[mqtt-fixture] 需要 docker 以启动 $IMAGE" >&2
  exit 1
fi

NAME="teleop-mqtt-fixture-$$"
cleanup() {
  docker rm -f "$NAME" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[mqtt-fixture] 启动 Mosquitto 容器 $NAME :$PORT"
docker run -d --rm --name "$NAME" -p "${PORT}:1883" "$IMAGE" >/dev/null

export MQTT_TEST_BROKER="tcp://127.0.0.1:${PORT}"
echo "[mqtt-fixture] MQTT_TEST_BROKER=$MQTT_TEST_BROKER"
sleep 1

cd "$CLIENT_BUILD_DIR"
./test_mqtt_paho_broker_smoke
echo "[mqtt-fixture][OK] Paho broker smoke 通过"
