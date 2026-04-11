#!/usr/bin/env bash
# 重新生成并同步到源码树：
#   - client/qml/remote-driving-cpp.qmltypes（RemoteDriving C++ / QML_ELEMENT）
#   - client/qml/DrivingFacade/driving-facade.qmltypes（DrivingFacade 静态 QML 模块，POST_BUILD 复制）
# 修改 QML_ELEMENT、CMake qt_add_qml_module、或 DrivingInterface.qml 影响类型导出时执行；生成物应提交。
#
# 用法（仓库根）：
#   ./scripts/regenerate-client-qmltypes.sh
# Docker：
#   docker run --rm -v "$PWD:/repo" remote-driving-client-dev:full bash /repo/scripts/regenerate-client-qmltypes.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${REGEN_QMLTYPES_BUILD_DIR:-${TMPDIR:-/tmp}/remote-driving-qmltypes-build}"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
echo ">>> cmake $ROOT/client (build dir: $BUILD_DIR)"
cmake "$ROOT/client" -DCMAKE_BUILD_TYPE="${REGEN_QMLTYPES_BUILD_TYPE:-Debug}"
echo ">>> cmake --build RemoteDrivingClient"
cmake --build . --target RemoteDrivingClient -j"${REGEN_QMLTYPES_JOBS:-$(nproc 2>/dev/null || echo 4)}"

for out in \
  "$ROOT/client/qml/remote-driving-cpp.qmltypes" \
  "$ROOT/client/qml/DrivingFacade/driving-facade.qmltypes"; do
  if [[ ! -f "$out" ]]; then
    echo "[FAIL] 未找到: $out（检查 CMake TYPEINFO / DrivingFacade POST_BUILD）" >&2
    exit 1
  fi
  echo "[OK] $out"
done
echo "请 git diff 上述文件后提交。"
