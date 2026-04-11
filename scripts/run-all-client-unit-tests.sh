#!/usr/bin/env bash
# 一键运行客户端全部 C++ 单元测试（Qt Test / CTest）。
# 被测源文件 ↔ 用例映射：docs/CLIENT_UNIT_TEST_SOURCE_MAP.md
# 本机无需 compose 时可用：./scripts/run-client-unit-tests-oneclick.sh
#
# 用法：
#   ./scripts/run-all-client-unit-tests.sh
#   CLIENT_BUILD_DIR=/tmp/other-build ./scripts/run-all-client-unit-tests.sh
#   IN_DOCKER=0 CLIENT_BUILD_DIR=./client/build ./scripts/run-all-client-unit-tests.sh
#
# 容器内源码路径默认为 /workspace/client（与 compose dev 挂载一致）。
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

COMPOSE_FILES="${COMPOSE_FILES:--f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml}"
COMPOSE_CMD="docker compose ${COMPOSE_FILES}"

IN_DOCKER="${IN_DOCKER:-1}"
CLIENT_BUILD_DIR="${CLIENT_BUILD_DIR:-/tmp/client-build}"
CONTAINER_CLIENT_SRC="${CONTAINER_CLIENT_SRC:-/workspace/client}"

CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${CYAN}========== 客户端全量单元测试（CTest）==========${NC}"

if [[ "$IN_DOCKER" == "1" ]]; then
  if ! command -v docker >/dev/null 2>&1; then
    echo -e "${RED}未找到 docker，设 IN_DOCKER=0 并在本地已 cmake 的 CLIENT_BUILD_DIR 运行${NC}" >&2
    exit 1
  fi
  if ! $COMPOSE_CMD ps client-dev 2>/dev/null | grep -qE 'Up|running'; then
    echo -e "${YELLOW}client-dev 未运行。请先启动 compose 中的 client-dev 服务${NC}" >&2
    exit 1
  fi
  echo -e "${CYAN}在 client-dev 内: ${CLIENT_BUILD_DIR} → cmake + build + ctest${NC}"
  $COMPOSE_CMD exec -T client-dev env CONTAINER_CLIENT_SRC="$CONTAINER_CLIENT_SRC" CLIENT_BUILD_DIR="$CLIENT_BUILD_DIR" bash -euo pipefail -c '
    mkdir -p "$CLIENT_BUILD_DIR"
    cd "$CLIENT_BUILD_DIR"
    if [ ! -f CTestTestfile.cmake ]; then
      cmake "$CONTAINER_CLIENT_SRC" \
        -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel \
        -DCMAKE_BUILD_TYPE=Debug
    fi
    cmake --build . -j"$(nproc)"
    ctest --output-on-failure
  '
  echo -e "${GREEN}========== 全部客户端单元测试通过 ==========${NC}"
  exit 0
fi

if [[ ! -f "$CLIENT_BUILD_DIR/CTestTestfile.cmake" ]]; then
  echo -e "${RED}未找到 $CLIENT_BUILD_DIR/CTestTestfile.cmake${NC}" >&2
  exit 1
fi
( cd "$CLIENT_BUILD_DIR" && ctest --output-on-failure )
echo -e "${GREEN}========== 全部客户端单元测试通过 ==========${NC}"
