#!/usr/bin/env bash
# Qt UI 层「Google 级可执行门禁」：契约 + 全量 qmllint + UI 相关 C++ 的 clang-format 一致性。
# Google 未发布 QML 风格指南；此处将「显式依赖 / 可静态验证 / 小模块边界」对齐 Google 工程习惯。
#
# 依赖（任一）：
#   - 宿主机 PATH 含 qmllint（Qt 6 bin）与 clang-format-18；或
#   - Docker 可用且存在镜像 remote-driving-client-dev:full（脚本内用 Qt 自带 qmllint；format 缺省在容器内尝试 apt 装 clang-format-18）
#
# 用法（仓库根）：
#   ./scripts/verify-client-qt-ui-google-style.sh
#   SKIP_CLANG_FORMAT=1 ./scripts/verify-client-qt-ui-google-style.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[0;33m'
NC='\033[0m'

fail() { echo -e "${RED}[FAIL] $*${NC}" >&2; exit 1; }

# shellcheck source=lib/collect-qml-related-cpp.sh
source "$SCRIPT_DIR/lib/collect-qml-related-cpp.sh"

echo -e "${CYAN}>>> verify-client-qt-ui-google-style（契约 + qmllint + QML 相关 C++ Google format）${NC}"

bash "$SCRIPT_DIR/verify-client-ui-module-contract.sh" || fail "verify-client-ui-module-contract.sh"

CLIENT_DIR="$PROJECT_ROOT/client"
QML_ROOT="$CLIENT_DIR/qml"
[[ -d "$QML_ROOT" ]] || fail "缺失 $QML_ROOT"

# Qt 6：默认 MaxWarnings=-1 时 warning 不抬升退出码；须 -W 0 与 .qmllint.ini 中 MaxWarnings=0 双保险。
# -D Quick：Quick 布局插件与 LintPluginWarnings=disable 并存时仍可能报 layout-positioning；与「类型/导入」硬门禁解耦。
QMLLINT_STRICT_FLAGS=(-W 0 -D Quick)

run_qmllint() {
  local -a cmd=("$@")
  local fail_one=0
  while IFS= read -r -d '' f; do
    local rel="${f#$QML_ROOT/}"
    local -a args=("${cmd[@]}" "${QMLLINT_STRICT_FLAGS[@]}" -s -I "$QML_ROOT" -I "$QML_ROOT/components" \
      -I "$QML_ROOT/components/driving" -I "$QML_ROOT/components/driving/internal" \
      -I "$QML_ROOT/shell" -I "$QML_ROOT/styles" "$rel")
    if ! "${args[@]}" 2>/dev/null; then
      echo -e "${RED}--- qmllint: $rel ---${NC}" >&2
      "${cmd[@]}" "${QMLLINT_STRICT_FLAGS[@]}" -I "$QML_ROOT" -I "$QML_ROOT/components" \
        -I "$QML_ROOT/components/driving" -I "$QML_ROOT/components/driving/internal" \
        -I "$QML_ROOT/shell" -I "$QML_ROOT/styles" "$rel" || fail_one=1
    fi
  done < <(find "$QML_ROOT" -name "*.qml" -print0)
  [[ "$fail_one" -eq 0 ]] || fail "qmllint 未通过（见上）；配置见 client/qml/.qmllint.ini 与 docs/CLIENT_QT_UI_GOOGLE_STYLE.md"
}

if command -v qmllint >/dev/null 2>&1; then
  echo -e "  ${CYAN}qmllint（宿主机，-s 静默，失败时带详情）…${NC}"
  (cd "$QML_ROOT" && run_qmllint qmllint)
elif command -v docker >/dev/null 2>&1 && docker image inspect remote-driving-client-dev:full >/dev/null 2>&1; then
  echo -e "  ${CYAN}qmllint（Docker remote-driving-client-dev:full + Qt 6.8）…${NC}"
  docker run --rm \
    -v "$CLIENT_DIR:/workspace/client" \
    remote-driving-client-dev:full \
    bash -c 'set -euo pipefail
      export PATH="/opt/Qt/6.8.0/gcc_64/bin:${PATH:-}"
      Q=/workspace/client/qml
      cd "$Q"
      fail=0
      while IFS= read -r -d "" f; do
        rel="${f#$Q/}"
        if ! qmllint -W 0 -D Quick -s -I "$Q" -I "$Q/components" -I "$Q/components/driving" -I "$Q/components/driving/internal" -I "$Q/shell" -I "$Q/styles" "$rel" 2>/dev/null; then
          echo "--- qmllint: $rel ---" >&2
          qmllint -W 0 -D Quick -I "$Q" -I "$Q/components" -I "$Q/components/driving" -I "$Q/components/driving/internal" -I "$Q/shell" -I "$Q/styles" "$rel" || fail=1
        fi
      done < <(find . -name "*.qml" -print0)
      exit "$fail"' \
    || fail "Docker 内 qmllint 失败"
else
  fail "未找到 qmllint 且无法使用 Docker 镜像 remote-driving-client-dev:full；请安装 Qt 6 命令行工具或拉取 client-dev 镜像"
fi

if [[ "${SKIP_CLANG_FORMAT:-0}" == "1" ]]; then
  echo -e "  ${YELLOW}跳过 clang-format（SKIP_CLANG_FORMAT=1）${NC}"
else
  FMT="${FORMAT_CMD:-clang-format-18}"
  run_fmt() {
    local bin="$1"
    local client_root="$2"
    local fail_fmt=0
    while IFS= read -r -d '' f; do
      if ! "$bin" --dry-run -Werror "$f" 2>/dev/null; then
        echo -e "${RED}格式不符（请运行 ./scripts/format-client-cpp.sh 或 FORMAT_CMD=$bin）: $f${NC}" >&2
        fail_fmt=1
      fi
    done < <(collect_qml_related_cpp_files "$client_root")
    [[ "$fail_fmt" -eq 0 ]] || fail "QML 相关 C++ 未按 client/.clang-format（Google 基线）格式化；路径清单见 scripts/lib/collect-qml-related-cpp.sh"
  }

  if command -v "$FMT" >/dev/null 2>&1; then
    echo -e "  ${CYAN}clang-format --dry-run（$FMT，QML 表面 C++ 全集，见 scripts/lib/collect-qml-related-cpp.sh）…${NC}"
    run_fmt "$FMT" "$CLIENT_DIR"
  elif command -v docker >/dev/null 2>&1 && docker image inspect remote-driving-client-dev:full >/dev/null 2>&1; then
    echo -e "  ${CYAN}clang-format --dry-run（Docker + clang-format-18，挂载仓库根以读取路径清单）…${NC}"
    docker run --rm \
      -v "$PROJECT_ROOT:/workspace/repo" \
      remote-driving-client-dev:full \
      bash -c 'set -euo pipefail
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        apt-get install -y -qq --no-install-recommends clang-format-18 >/dev/null
        # shellcheck source=/dev/null
        source /workspace/repo/scripts/lib/collect-qml-related-cpp.sh
        fail=0
        while IFS= read -r -d "" f; do
          clang-format-18 --dry-run -Werror "$f" || fail=1
        done < <(collect_qml_related_cpp_files /workspace/repo/client)
        exit "$fail"' \
      || fail "Docker 内 clang-format 检查失败"
  else
    fail "未找到 $FMT 且无法用 Docker 跑 clang-format；请安装 LLVM 18 或设 SKIP_CLANG_FORMAT=1"
  fi
fi

if [[ "${VERIFY_QMLTYPES_DRIFT:-0}" == "1" ]]; then
  echo -e "  ${CYAN}VERIFY_QMLTYPES_DRIFT=1：构建并校验 qmltypes 与 Git 一致…${NC}"
  bash "$SCRIPT_DIR/verify-qmltypes-match-generated.sh" || fail "verify-qmltypes-match-generated.sh"
fi

echo -e "${GREEN}[OK] verify-client-qt-ui-google-style 通过${NC}"
