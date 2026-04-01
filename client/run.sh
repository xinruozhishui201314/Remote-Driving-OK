#!/bin/bash
# Client 工程一键运行脚本（仅允许在 Docker 容器内执行）
# 宿主机请使用: make run 或 make run-client（会在 client-dev 容器内运行）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 禁止在宿主机上运行
if [ ! -f "/.dockerenv" ] && [ -z "$CONTAINER_ID" ]; then
    echo "错误: 禁止在宿主机上运行客户端。" >&2
    echo "请使用: make run 或 make run-client（将在 client-dev 容器内运行）" >&2
    echo "详见: docs/BUILD_AND_RUN_POLICY.md" >&2
    exit 1
fi

BUILD_DIR="build"
EXECUTABLE=""
# 容器内可能由 make build-client 使用 /tmp/client-build，或本目录 ./build.sh 使用 build/
if [ -f "/tmp/client-build/RemoteDrivingClient" ]; then
    BUILD_DIR="/tmp/client-build"
    EXECUTABLE="$BUILD_DIR/RemoteDrivingClient"
elif [ -f "$SCRIPT_DIR/build/RemoteDrivingClient" ]; then
    BUILD_DIR="$SCRIPT_DIR/build"
    EXECUTABLE="$BUILD_DIR/RemoteDrivingClient"
fi
if [ -z "$EXECUTABLE" ] || [ ! -f "$EXECUTABLE" ]; then
    echo "错误: 未找到可执行文件。请先在容器内运行 ./build.sh 或使用 make build-client 在容器内编译。"
    exit 1
fi

IN_CONTAINER=true

# 设置 Qt 环境变量（容器内路径）
if [ -d "/opt/Qt/6.8.0/gcc_64" ]; then
    export LD_LIBRARY_PATH="/opt/Qt/6.8.0/gcc_64/lib:${LD_LIBRARY_PATH:-}"
    export PATH="/opt/Qt/6.8.0/gcc_64/bin:${PATH:-}"
    export QT_PLUGIN_PATH="/opt/Qt/6.8.0/gcc_64/plugins"
    export QML2_IMPORT_PATH="/opt/Qt/6.8.0/gcc_64/qml"
    echo "✓ 使用容器内 Qt6 库路径"
elif [ -n "$QT_GCC" ] && [ -d "$QT_GCC" ]; then
    export LD_LIBRARY_PATH="$QT_GCC/lib:${LD_LIBRARY_PATH:-}"
    export PATH="$QT_GCC/bin:${PATH:-}"
    export QT_PLUGIN_PATH="$QT_GCC/plugins"
    export QML2_IMPORT_PATH="$QT_GCC/qml"
fi

# 设置 X11 显示（容器内 GUI 支持）
if [ -z "$DISPLAY" ]; then
    if [ "$IN_CONTAINER" = true ]; then
        # 容器内默认使用 :0
        export DISPLAY=:0
    else
        export DISPLAY=:0
    fi
fi

# 设置 Qt 平台插件
export QT_QPA_PLATFORM=xcb

echo "=========================================="
echo "启动 Remote Driving Client"
echo "=========================================="
echo "运行环境: $([ "$IN_CONTAINER" = true ] && echo "Docker 容器" || echo "宿主机")"
echo "DISPLAY: $DISPLAY"
echo "Qt 路径: ${QT_GCC:-/opt/Qt/6.8.0/gcc_64}"
echo ""

# 验证 GUI 环境（容器内）
if [ "$IN_CONTAINER" = true ]; then
    if [ ! -S /tmp/.X11-unix/X0 ] && [ ! -d /tmp/.X11-unix ]; then
        echo "⚠ 警告: X11 socket 未挂载，GUI 可能无法显示"
        echo "  确保容器启动时挂载了 /tmp/.X11-unix"
    fi
fi

# 运行程序（处理 noexec 挂载问题）
cd "$BUILD_DIR"
EXECUTABLE_PATH="$(pwd)/RemoteDrivingClient"
if [ ! -f "$EXECUTABLE_PATH" ]; then
    echo "错误: 可执行文件不存在: $EXECUTABLE_PATH"
    exit 1
fi

# 由于工作目录挂载了 noexec，需要复制到 /tmp 执行
TEMP_EXEC="/tmp/RemoteDrivingClient_$$"
echo "复制可执行文件到临时目录（处理 noexec 挂载）..."
cp "$EXECUTABLE_PATH" "$TEMP_EXEC"
chmod +x "$TEMP_EXEC"

# 设置退出时清理临时文件
trap "rm -f $TEMP_EXEC" EXIT INT TERM

# 执行程序
exec "$TEMP_EXEC" "$@"
