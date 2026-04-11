#!/usr/bin/env bash
# 在已存在 client/build/compile_commands.json 的前提下，对 client/src 下所有 .cpp 运行 clang-tidy，归档到 client/reports/clang-tidy-baseline.txt
#
# 推荐环境：remote-driving-client-dev:full（须用 LLVM 18+ 的 clang-tidy 解析 Qt 6.8 头；系统默认 clang-tidy 过旧易崩溃）
#
# clang-tidy 获取：
#   - 镜像内若已有 clang-tidy-18（或 TIDY 指向的可执行文件），直接使用。
#   - 若未找到且判定在容器内（/.dockerenv 等），或显式 CLANG_TIDY_AUTO_INSTALL=1，则 apt-get 安装 clang-tidy-18（已安装则 apt 无操作）。
#
# 用法：
#   ./scripts/run-client-clang-tidy-baseline.sh
#   CLIENT_DIR=/path/to/client TIDY=clang-tidy-18 ./scripts/run-client-clang-tidy-baseline.sh
#   CLANG_TIDY_AUTO_INSTALL=1 ./scripts/run-client-clang-tidy-baseline.sh   # 宿主机需 root/sudo 时自行处理
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLIENT_DIR="${CLIENT_DIR:-$REPO_ROOT/client}"
BUILD_DIR="${BUILD_DIR:-$CLIENT_DIR/build}"
REPORT_DIR="${REPORT_DIR:-$CLIENT_DIR/reports}"
OUT="${OUT:-$REPORT_DIR/clang-tidy-baseline.txt}"
TIDY="${TIDY:-clang-tidy-18}"

# 是否在「典型容器」环境（用于默认允许 apt 安装）
in_container_env() {
  [[ -f /.dockerenv ]] || [[ -n "${CONTAINER_ID:-}" ]] || [[ -n "${KUBERNETES_SERVICE_HOST:-}" ]]
}

# 未找到 TIDY 时：容器内或 CLANG_TIDY_AUTO_INSTALL=1 则尝试 apt 安装 clang-tidy-18
ensure_clang_tidy() {
  if command -v "$TIDY" >/dev/null 2>&1; then
    return 0
  fi

  local allow_install=0
  if [[ "${CLANG_TIDY_AUTO_INSTALL:-}" == "1" ]]; then
    allow_install=1
  elif [[ "${CLANG_TIDY_AUTO_INSTALL:-}" == "0" ]]; then
    allow_install=0
  elif in_container_env; then
    allow_install=1
  fi

  if [[ "$allow_install" -eq 0 ]]; then
    echo "[run-client-clang-tidy-baseline] 未找到可执行文件: $TIDY" >&2
    echo "  请安装 clang-tidy-18（或设置 TIDY=...），或在容器内重试（将自动 apt 安装）。" >&2
    echo "  宿主机强制安装可设: CLANG_TIDY_AUTO_INSTALL=1" >&2
    exit 1
  fi

  if ! command -v apt-get >/dev/null 2>&1; then
    echo "[run-client-clang-tidy-baseline] 未找到 $TIDY 且无 apt-get，无法自动安装" >&2
    exit 1
  fi

  echo "[run-client-clang-tidy-baseline] 未检测到 $TIDY，执行 apt-get 安装 clang-tidy-18（若已安装将快速跳过）..." >&2
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  # --no-install-recommends 减小镜像层；clang-tidy-18 在 Ubuntu 22.04/24.04 可用
  apt-get install -y -qq --no-install-recommends clang-tidy-18

  if ! command -v "$TIDY" >/dev/null 2>&1; then
    echo "[run-client-clang-tidy-baseline] 安装后仍无法执行: $TIDY（请检查 PATH 或包名）" >&2
    exit 1
  fi
}

if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
  echo "[run-client-clang-tidy-baseline] 缺少 $BUILD_DIR/compile_commands.json" >&2
  echo "  请先在 client-dev 容器内执行: cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..." >&2
  exit 1
fi

ensure_clang_tidy

mkdir -p "$REPORT_DIR"
cd "$CLIENT_DIR"

{
  echo "=== clang-tidy baseline $(date -u +%Y-%m-%dT%H:%MZ) ==="
  echo "client_dir=$CLIENT_DIR"
  echo "build_dir=$BUILD_DIR"
  echo "tidy=$TIDY"
  echo "config=$CLIENT_DIR/.clang-tidy"
  echo ""
  n=0
  while IFS= read -r -d '' f; do
    n=$((n + 1))
    echo "=== [$n] $f ==="
    "$TIDY" -p "$BUILD_DIR" "$f" 2>&1 || true
  done < <(find src -name '*.cpp' -print0 | sort -z)
  echo ""
  echo "=== Done. Files: $n ==="
} >"$OUT"

echo "[run-client-clang-tidy-baseline] Wrote $OUT ($(wc -l <"$OUT") lines)"
