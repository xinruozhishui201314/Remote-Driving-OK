#!/usr/bin/env bash
# 列出「暴露给 QML 的 C++ 表面」：引擎启动、上下文属性、qmlRegisterType、QML_ELEMENT 模型、Quick 项。
# 用于 clang-format --dry-run（Google 基线，见 client/.clang-format）。
#
# 用法：
#   source "$(dirname "$0")/collect-qml-related-cpp.sh"
#   collect_qml_related_cpp_files "/abs/path/to/client"   # 打印 NUL 分隔的 .cpp/.h 路径
#
collect_qml_related_cpp_files() {
  local client_dir="${1:?client_dir}"
  local src="$client_dir/src"
  [[ -d "$src" ]] || return 1

  _qml_emit_tree() {
    local d="$1"
    [[ -d "$d" ]] || return 0
    find "$d" -type f \( -name '*.cpp' -o -name '*.h' \) -print0
  }

  _qml_emit_tree "$src/app"
  _qml_emit_tree "$src/ui"
  _qml_emit_tree "$src/presentation"

  [[ -f "$src/main.cpp" ]] && printf '%s\0' "$src/main.cpp"

  local stem
  for stem in authmanager vehiclemanager webrtcclient webrtcstreammanager mqttcontroller vehiclestatus \
    nodehealthchecker; do
    local ext f
    for ext in cpp h; do
      f="$src/${stem}.${ext}"
      [[ -f "$f" ]] && printf '%s\0' "$f"
    done
  done

  local rel
  for rel in \
    core/eventbus.cpp \
    core/eventbus.h \
    core/systemstatemachine.cpp \
    core/systemstatemachine.h \
    core/networkqualityaggregator.cpp \
    core/networkqualityaggregator.h \
    core/tracing.cpp \
    core/tracing.h \
    services/sessionmanager.cpp \
    services/sessionmanager.h \
    services/vehiclecontrolservice.cpp \
    services/vehiclecontrolservice.h \
    services/safetymonitorservice.cpp \
    services/safetymonitorservice.h \
    services/degradationmanager.cpp \
    services/degradationmanager.h; do
    [[ -f "$src/$rel" ]] && printf '%s\0' "$src/$rel"
  done
}
