#!/usr/bin/env bash
# 一键：配置（如需）+ 编译客户端测试目标 + 运行全部 CTest。
# 映射表见 docs/CLIENT_UNIT_TEST_SOURCE_MAP.md（Codecov / 审计）。
#
# 用法（仓库根目录）：
#   ./scripts/run-client-unit-tests-oneclick.sh
#   ./scripts/run-client-unit-tests-oneclick.sh --fresh
#
# 主路径：客户端在 client-dev 镜像内编译/运行；本脚本默认在 client-dev 内 configure + run-unit-tests
# （与 compose、run-all-client-unit-tests.sh 一致）。仅在需要本机工具链排障时再用宿主机。
#
# 环境变量：
#   USE_CLIENT_DEV   1（默认，容器）| auto 有 cmake 则宿主机否则容器 | 0 强制宿主机（无 cmake 则失败）
#   CLIENT_DIR       客户端工程目录（宿主机默认 <repo>/client；容器内为 /workspace/client）
#   BUILD_DIR        仅宿主机构建目录（默认 <CLIENT_DIR>/build-ctest）
#   CLIENT_BUILD_DIR_IN_CONTAINER  容器内构建目录（默认 /tmp/client-build）
#   CMAKE_BUILD_TYPE 默认 Debug
#   JOBS             并行编译数
#   CMAKE_EXTRA_ARGS 追加 cmake 参数
#   COMPOSE_FILES    与仓库其它脚本一致
#
# 无图形：容器内默认 QT_QPA_PLATFORM=offscreen（可覆盖）。
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CLIENT_DIR="${CLIENT_DIR:-$REPO_ROOT/client}"
BUILD_DIR="${BUILD_DIR:-$CLIENT_DIR/build-ctest}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
CLIENT_BUILD_DIR_IN_CONTAINER="${CLIENT_BUILD_DIR_IN_CONTAINER:-/tmp/client-build}"
COMPOSE_FILES="${COMPOSE_FILES:--f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml}"
COMPOSE_CMD="docker compose ${COMPOSE_FILES}"
USE_CLIENT_DEV="${USE_CLIENT_DEV:-1}"

FRESH=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --fresh|-f) FRESH=1; shift ;;
    -h|--help)
      sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) echo "未知参数: $1（使用 --help）" >&2; exit 2 ;;
  esac
done

if [[ ! -f "$CLIENT_DIR/CMakeLists.txt" ]]; then
  echo "[run-client-unit-tests-oneclick] 未找到 $CLIENT_DIR/CMakeLists.txt" >&2
  exit 1
fi

want_client_dev() {
  case "$USE_CLIENT_DEV" in
    1|true|yes) return 0 ;;
    0|false|no) return 1 ;;
    auto)
      if command -v cmake >/dev/null 2>&1; then
        return 1
      fi
      return 0
      ;;
    *)
      echo "[run-client-unit-tests-oneclick] USE_CLIENT_DEV 无效: $USE_CLIENT_DEV（auto|0|1）" >&2
      exit 2
      ;;
  esac
}

run_in_client_dev() {
  cd "$REPO_ROOT"
  if ! command -v docker >/dev/null 2>&1; then
    echo "[run-client-unit-tests-oneclick] 默认在 client-dev 容器内跑单测，但未找到 docker。" >&2
    echo "  请安装 Docker 并启动: cd $REPO_ROOT && $COMPOSE_CMD up -d client-dev" >&2
    echo "  或显式本机构建: USE_CLIENT_DEV=0（需本机 cmake + Qt6::Test 等）" >&2
    exit 1
  fi
  if ! $COMPOSE_CMD ps client-dev 2>/dev/null | grep -qE 'Up|running'; then
    echo "[run-client-unit-tests-oneclick] client-dev 未运行。" >&2
    echo "  请先: cd $REPO_ROOT && $COMPOSE_CMD up -d client-dev" >&2
    echo "  或本机排障: USE_CLIENT_DEV=0（需 cmake + Qt6::Test）" >&2
    exit 1
  fi

  echo "[run-client-unit-tests-oneclick] 在 client-dev 容器内: $CLIENT_BUILD_DIR_IN_CONTAINER（cmake + run-unit-tests）…" >&2

  # heredoc + bash -s：避免嵌套双引号在部分环境下触发 “unexpected EOF / matching quote”
  # shellcheck disable=SC2086
  $COMPOSE_CMD exec -i -T \
    -e "QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen}" \
    -e "CBDIR=$CLIENT_BUILD_DIR_IN_CONTAINER" \
    -e "FRESH_FLAG=$FRESH" \
    -e "BT=$CMAKE_BUILD_TYPE" \
    -e "NJOBS=$JOBS" \
    -e "CEA=${CMAKE_EXTRA_ARGS:-}" \
    client-dev bash -euo pipefail -s <<'EOF'
mkdir -p "$CBDIR"
cd "$CBDIR"
if [[ "$FRESH_FLAG" == "1" ]]; then
  rm -f CMakeCache.txt || true
fi
if [[ "$FRESH_FLAG" == "1" ]] || [[ ! -f CMakeCache.txt ]]; then
  cmake /workspace/client \
    -DCMAKE_BUILD_TYPE="$BT" \
    -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel \
    $CEA
fi
if [[ ! -f CTestTestfile.cmake ]]; then
  echo '[run-client-unit-tests-oneclick] 容器内未生成 CTestTestfile（Qt6::Test 未找到？）' >&2
  exit 2
fi
cmake --build . -j"$NJOBS" --target run-unit-tests
EOF

  echo "[run-client-unit-tests-oneclick] 全部客户端单元测试通过（client-dev）。"
}

run_on_host() {
  if ! command -v cmake >/dev/null 2>&1; then
    echo "[run-client-unit-tests-oneclick] 宿主机未找到 cmake，且 USE_CLIENT_DEV=0。" >&2
    echo "  请安装 cmake，或去掉 USE_CLIENT_DEV=0（恢复默认容器内单测）。" >&2
    exit 1
  fi

  if [[ "$(uname -s 2>/dev/null || true)" == "Linux" ]] && [[ -z "${DISPLAY:-}" ]]; then
    export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
  fi

  mkdir -p "$BUILD_DIR"
  cd "$BUILD_DIR"

  if [[ "$FRESH" == "1" ]] && [[ -f CMakeCache.txt ]]; then
    rm -f CMakeCache.txt
  fi

  if [[ "$FRESH" == "1" ]] || [[ ! -f CMakeCache.txt ]]; then
    # shellcheck disable=SC2086
    cmake "$CLIENT_DIR" \
      -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
      ${CMAKE_EXTRA_ARGS:-}
  fi

  if [[ ! -f CTestTestfile.cmake ]]; then
    echo "[run-client-unit-tests-oneclick] 未生成 CTestTestfile.cmake。" >&2
    echo "  通常表示 Qt6::Test 未找到。可设置 CMAKE_PREFIX_PATH，或使用默认容器路径（去掉 USE_CLIENT_DEV=0）。" >&2
    exit 2
  fi

  echo "[run-client-unit-tests-oneclick] 宿主机构建并执行 run-unit-tests…"
  cmake --build . -j"$JOBS" --target run-unit-tests

  echo "[run-client-unit-tests-oneclick] 全部客户端单元测试通过（宿主机）。"
}

if want_client_dev; then
  run_in_client_dev
else
  run_on_host
fi
