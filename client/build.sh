#!/bin/bash
# Client 工程一键编译脚本（仅允许在 Docker 容器内执行）
# 宿主机请使用: make build-client 或 make run（会在 client-dev 容器内编译并运行）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 禁止在宿主机上编译：远程驾驶策略为仅容器内编译/运行
if [ ! -f "/.dockerenv" ] && [ -z "$CONTAINER_ID" ]; then
    echo "错误: 禁止在宿主机上编译客户端。" >&2
    echo "请在容器内执行本脚本，或使用: make build-client（将在 client-dev 容器内编译）" >&2
    echo "详见: docs/BUILD_AND_RUN_POLICY.md" >&2
    exit 1
fi

echo "=========================================="
echo "Building Remote Driving Client"
echo "=========================================="
IN_CONTAINER=true

# 检测 Qt6 路径（优先使用容器内的路径）；若挂载了宿主机安装的 libdatachannel 则一并加入
if [ -z "$CMAKE_PREFIX_PATH" ]; then
    # 容器内路径（docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt）
    if [ -d "/opt/Qt/6.8.0/gcc_64" ]; then
        export CMAKE_PREFIX_PATH="/opt/Qt/6.8.0/gcc_64"
        [ -d "/opt/libdatachannel" ] && export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH:/opt/libdatachannel"
        export QT_GCC="/opt/Qt/6.8.0/gcc_64"
        echo "✓ 使用容器内 Qt6 路径: $CMAKE_PREFIX_PATH"
    elif [ -n "$QT_GCC" ] && [ -d "$QT_GCC" ]; then
        export CMAKE_PREFIX_PATH="$QT_GCC"
        echo "✓ 使用环境变量 QT_GCC: $CMAKE_PREFIX_PATH"
    elif [ -d "$HOME/Qt/6.8.0/gcc_64" ]; then
        export CMAKE_PREFIX_PATH="$HOME/Qt/6.8.0/gcc_64"
        echo "⚠ 使用用户目录 Qt6 路径: $CMAKE_PREFIX_PATH"
    else
        echo "错误: 未找到 Qt6"
        echo "  容器内路径: /opt/Qt/6.8.0/gcc_64"
        echo "  请设置 CMAKE_PREFIX_PATH 或 QT_GCC 环境变量"
        exit 1
    fi
fi

echo "使用 Qt6 路径: $CMAKE_PREFIX_PATH"

# 设置 Qt 环境变量（容器内）
if [ "$IN_CONTAINER" = true ]; then
    export LD_LIBRARY_PATH="$CMAKE_PREFIX_PATH/lib:${LD_LIBRARY_PATH:-}"
    export PATH="$CMAKE_PREFIX_PATH/bin:${PATH:-}"
    echo "✓ 已设置容器内 Qt 环境变量"
fi

# 创建构建目录
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    echo "清理旧的构建目录..."
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 配置 CMake
echo ""
echo "配置 CMake..."
cmake .. \
    -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Debug}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 编译
echo ""
echo "开始编译..."
CPU_COUNT=$(nproc)
make -j${CPU_COUNT}

echo ""
echo "=========================================="
echo "编译完成！"
echo "=========================================="
echo ""
echo "可执行文件位置: $BUILD_DIR/RemoteDrivingClient"
echo ""
echo "运行命令:"
echo "  cd $BUILD_DIR && ./RemoteDrivingClient"
echo "  或使用: ./run.sh"
echo ""
