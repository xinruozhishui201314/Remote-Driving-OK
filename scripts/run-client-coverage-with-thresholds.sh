#!/usr/bin/env bash
# MVP：客户端 CTest + lcov 合并报告 + 可选行覆盖率门禁（按路径前缀）。
#
# 用法（仓库根目录）：
#   ./scripts/run-client-coverage-with-thresholds.sh
#
# 环境变量：
#   CLIENT_DIR              默认 <repo>/client
#   BUILD_DIR               默认 <CLIENT_DIR>/build-coverage
#   CMAKE_BUILD_TYPE        默认 Debug
#   QT_QPA_PLATFORM         无 DISPLAY 时建议 offscreen
#
# 门禁（未设置则不检查该项；设置后低于阈值则 exit 1）：
#   CLIENT_COVERAGE_ENFORCE=1       启用总线门禁（需设 OVERALL_LINES_MIN 或任意 PREFIX_MIN）
#   OVERALL_LINES_MIN=30            全量 client/src 行覆盖率下限（%）
#   CORE_LINES_MIN=25               */client/src/core/* 下限
#   MEDIA_LINES_MIN=20              */client/src/media/* 下限
#   SERVICES_LINES_MIN=20           */client/src/services/* 下限
#   UTILS_LINES_MIN=15              */client/src/utils/*
#   INFRA_LINES_MIN=10            */client/src/infrastructure/*
#   APP_LINES_MIN=5               */client/src/app/*
#
# 分支覆盖率（lcov --summary 中 branches 行；未设置则不检查）：
#   OVERALL_BRANCHES_MIN=10
#   CORE_BRANCHES_MIN=8
#   MEDIA_BRANCHES_MIN=8
#   SERVICES_BRANCHES_MIN=8
#   （utils/infrastructure/app 分支门禁可按需同理扩展脚本）
#
# CI 建议（示例，可按模块逐步提高）：
#   CLIENT_COVERAGE_ENFORCE=1 OVERALL_LINES_MIN=18 CORE_LINES_MIN=20 MEDIA_LINES_MIN=15 \
#   SERVICES_LINES_MIN=15 UTILS_LINES_MIN=12 INFRA_LINES_MIN=8 APP_LINES_MIN=5
#
# 依赖：cmake、Qt6::Test、gcc/clang 带 gcov、lcov、genhtml
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLIENT_DIR="${CLIENT_DIR:-$REPO_ROOT/client}"
BUILD_DIR="${BUILD_DIR:-$CLIENT_DIR/build-coverage}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}"

if [[ ! -f "$CLIENT_DIR/CMakeLists.txt" ]]; then
  echo "[coverage] 未找到 $CLIENT_DIR/CMakeLists.txt" >&2
  exit 1
fi

if ! command -v lcov >/dev/null 2>&1 || ! command -v genhtml >/dev/null 2>&1; then
  echo "[coverage] 需要 lcov 与 genhtml（apt install lcov）" >&2
  exit 1
fi

if [[ "$(uname -s 2>/dev/null || true)" == "Linux" ]] && [[ -z "${DISPLAY:-}" ]]; then
  export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# shellcheck disable=SC2086
cmake "$CLIENT_DIR" \
  -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
  -DENABLE_QT6_Test=ON \
  -DENABLE_COVERAGE=ON \
  ${CMAKE_EXTRA_ARGS:-}

cmake --build . -j"$(nproc 2>/dev/null || echo 4)" --target run-unit-tests

lcov --capture --directory . --output-file coverage-raw.info --rc lcov_branch_coverage=1
lcov --remove coverage-raw.info \
  '/usr/*' \
  '*/tests/*' \
  '*/build*/*' \
  '*/Qt/*' \
  --output-file coverage.info \
  --ignore-errors unused

genhtml coverage.info --output-directory coverage-html --branch-coverage \
  --title "RemoteDrivingClient coverage" || true

lcov --summary coverage.info

extract_line_pct() {
  # lcov --summary: "  lines......: 78.5% (1234 of 1572 lines)"
  local info_file="$1"
  lcov --summary "$info_file" 2>/dev/null | grep -E 'lines\.\.\.\.\.\.:' | head -1 \
    | sed -E 's/.*: ([0-9.]+)%.*/\1/'
}

extract_branch_pct() {
  # lcov 2.x: "  branches...: 50.0% (100 of 200 branches)"
  local info_file="$1"
  lcov --summary "$info_file" 2>/dev/null | grep -Ei 'branches\.+:' | head -1 \
    | sed -E 's/.*: ([0-9.]+)%.*/\1/'
}

check_min() {
  local name="$1"
  local min="$2"
  local info="$3"
  [[ -z "$min" ]] && return 0
  local pct
  pct="$(extract_line_pct "$info")"
  if [[ -z "$pct" ]]; then
    echo "[coverage] 无法解析 $name 行覆盖率" >&2
    return 1
  fi
  awk -v p="$pct" -v m="$min" 'BEGIN { exit (p+0 >= m+0) ? 0 : 1 }' || {
    echo "[coverage][FAIL] $name 行覆盖率 ${pct}% < 门禁 ${min}%" >&2
    return 1
  }
  echo "[coverage][OK] $name 行覆盖率 ${pct}% >= ${min}%"
}

check_branch_min() {
  local name="$1"
  local min="$2"
  local info="$3"
  [[ -z "$min" ]] && return 0
  local pct
  pct="$(extract_branch_pct "$info")"
  if [[ -z "$pct" ]]; then
    echo "[coverage][WARN] $name 无 branches 摘要（子集可能无分支数据），跳过分支门禁" >&2
    return 0
  fi
  awk -v p="$pct" -v m="$min" 'BEGIN { exit (p+0 >= m+0) ? 0 : 1 }' || {
    echo "[coverage][FAIL] $name 分支覆盖率 ${pct}% < 门禁 ${min}%" >&2
    return 1
  }
  echo "[coverage][OK] $name 分支覆盖率 ${pct}% >= ${min}%"
}

if [[ "${CLIENT_COVERAGE_ENFORCE:-0}" != "1" ]]; then
  echo "[coverage] CLIENT_COVERAGE_ENFORCE!=1，跳过阈值检查。报告: $BUILD_DIR/coverage-html/index.html"
  exit 0
fi

FAIL=0
if [[ -n "${OVERALL_LINES_MIN:-}" ]]; then
  check_min "overall(client/src)" "$OVERALL_LINES_MIN" coverage.info || FAIL=1
fi

if [[ -n "${CORE_LINES_MIN:-}" ]]; then
  lcov --extract coverage.info '*/client/src/core/*' --output-file coverage-core.info || true
  check_min "core/" "$CORE_LINES_MIN" coverage-core.info || FAIL=1
fi

if [[ -n "${MEDIA_LINES_MIN:-}" ]]; then
  lcov --extract coverage.info '*/client/src/media/*' --output-file coverage-media.info || true
  check_min "media/" "$MEDIA_LINES_MIN" coverage-media.info || FAIL=1
fi

if [[ -n "${SERVICES_LINES_MIN:-}" ]]; then
  lcov --extract coverage.info '*/client/src/services/*' --output-file coverage-services.info || true
  check_min "services/" "$SERVICES_LINES_MIN" coverage-services.info || FAIL=1
fi

if [[ -n "${UTILS_LINES_MIN:-}" ]]; then
  lcov --extract coverage.info '*/client/src/utils/*' --output-file coverage-utils.info || true
  check_min "utils/" "$UTILS_LINES_MIN" coverage-utils.info || FAIL=1
fi

if [[ -n "${INFRA_LINES_MIN:-}" ]]; then
  lcov --extract coverage.info '*/client/src/infrastructure/*' --output-file coverage-infra.info || true
  check_min "infrastructure/" "$INFRA_LINES_MIN" coverage-infra.info || FAIL=1
fi

if [[ -n "${APP_LINES_MIN:-}" ]]; then
  lcov --extract coverage.info '*/client/src/app/*' --output-file coverage-app.info || true
  check_min "app/" "$APP_LINES_MIN" coverage-app.info || FAIL=1
fi

if [[ -n "${OVERALL_BRANCHES_MIN:-}" ]]; then
  check_branch_min "overall branches" "$OVERALL_BRANCHES_MIN" coverage.info || FAIL=1
fi

if [[ -n "${CORE_BRANCHES_MIN:-}" ]]; then
  lcov --extract coverage.info '*/client/src/core/*' --output-file coverage-core-br.info || true
  check_branch_min "core/ branches" "$CORE_BRANCHES_MIN" coverage-core-br.info || FAIL=1
fi

if [[ -n "${MEDIA_BRANCHES_MIN:-}" ]]; then
  lcov --extract coverage.info '*/client/src/media/*' --output-file coverage-media-br.info || true
  check_branch_min "media/ branches" "$MEDIA_BRANCHES_MIN" coverage-media-br.info || FAIL=1
fi

if [[ -n "${SERVICES_BRANCHES_MIN:-}" ]]; then
  lcov --extract coverage.info '*/client/src/services/*' --output-file coverage-services-br.info || true
  check_branch_min "services/ branches" "$SERVICES_BRANCHES_MIN" coverage-services-br.info || FAIL=1
fi

if [[ -z "${OVERALL_LINES_MIN:-}" && -z "${CORE_LINES_MIN:-}" && -z "${MEDIA_LINES_MIN:-}" && -z "${SERVICES_LINES_MIN:-}" && -z "${UTILS_LINES_MIN:-}" && -z "${INFRA_LINES_MIN:-}" && -z "${APP_LINES_MIN:-}" && -z "${OVERALL_BRANCHES_MIN:-}" && -z "${CORE_BRANCHES_MIN:-}" && -z "${MEDIA_BRANCHES_MIN:-}" && -z "${SERVICES_BRANCHES_MIN:-}" ]]; then
  echo "[coverage][WARN] CLIENT_COVERAGE_ENFORCE=1 但未设置任何 *_LINES_MIN / *_BRANCHES_MIN，视为无门禁。" >&2
fi

exit "$FAIL"
