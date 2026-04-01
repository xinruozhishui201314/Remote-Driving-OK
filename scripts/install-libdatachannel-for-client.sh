#!/bin/bash
# 在宿主机或容器内拉取、编译并安装 libdatachannel 到 client/deps/libdatachannel-install，
# 供 client-dev 容器挂载后编译客户端使用。
#
# 用法：bash scripts/install-libdatachannel-for-client.sh
# 依赖：宿主机需 Docker；若宿主机 CMake < 3.21，将在与 client-dev 相同的基础镜像内构建（Qt 6.8，无需额外拉镜像）
#
# 安装完成后，client-dev 会挂载 client/deps/libdatachannel-install 到 /opt/libdatachannel。

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLIENT_DEPS="$PROJECT_ROOT/client/deps"
INSTALL_PREFIX="$CLIENT_DEPS/libdatachannel-install"
SRC_DIR="$CLIENT_DEPS/libdatachannel-src"
BUILD_DIR="$CLIENT_DEPS/libdatachannel-build"
# 与 build-client-dev-full-image.sh 一致：优先用宿主机已有的 client-dev 镜像，否则用 Qt 基础镜像，不拉取官方
if [ -z "$BUILD_IMAGE" ]; then
  if docker image inspect remote-driving-client-dev:full >/dev/null 2>&1; then
    BUILD_IMAGE="remote-driving-client-dev:full"
  else
    BUILD_IMAGE="docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt"
  fi
fi

# libdatachannel 要求 CMake >= 3.21；宿主机不满足则在容器内构建
host_cmake_ok() {
    local v
    v="$(cmake --version 2>/dev/null | head -1 | sed -n 's/.*version \([0-9]*\.[0-9]*\).*/\1/p')"
    [ -z "$v" ] && return 1
    local maj min
    maj="${v%%.*}"
    min="${v#*.}"
    min="${min%%.*}"
    [ "$maj" -gt 3 ] && return 0
    [ "$maj" -eq 3 ] && [ "${min:-0}" -ge 21 ] && return 0
    return 1
}

run_on_host() {
    mkdir -p "$CLIENT_DEPS"
    cd "$CLIENT_DEPS"

    if [ ! -d "$SRC_DIR" ]; then
        echo "[1/4] 克隆 libdatachannel..."
        git clone --depth 1 https://github.com/paullouisageneau/libdatachannel.git "$SRC_DIR"
        cd "$SRC_DIR"
        git submodule update --init --recursive --depth 1
        cd "$CLIENT_DEPS"
    else
        echo "[1/4] 源码已存在: $SRC_DIR（若需更新请删除后重跑）"
    fi

    echo "[2/4] 配置 CMake（USE_GNUTLS=OFF USE_NICE=OFF）..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake "$SRC_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
        -DUSE_GNUTLS=OFF \
        -DUSE_NICE=OFF \
        -DNO_EXAMPLES=ON \
        -DNO_TESTS=ON

    echo "[3/4] 编译..."
    cmake --build . -j$(nproc 2>/dev/null || echo 4)

    echo "[4/4] 安装到 $INSTALL_PREFIX ..."
    cmake --install .
}

run_in_docker() {
    echo "  使用宿主机本地镜像在容器内构建: $BUILD_IMAGE"
    docker run --rm --user root \
        -v "$PROJECT_ROOT:/workspace:rw" \
        -w /workspace \
        "$BUILD_IMAGE" \
        bash -c '
        set -e
        # Qt 镜像通常已有 cmake/g++，仅补充 git/openssl（若无则安装，需 root）
        if ! command -v git >/dev/null 2>&1 || ! pkg-config --exists libssl 2>/dev/null; then
            export DEBIAN_FRONTEND=noninteractive
            apt-get update -qq && apt-get install -y -qq git libssl-dev pkg-config >/dev/null
        fi
        CD=/workspace/client/deps
        SRC="$CD/libdatachannel-src"
        BLD="$CD/libdatachannel-build"
        INS="$CD/libdatachannel-install"
        mkdir -p "$CD"
        if [ ! -d "$SRC" ]; then
            echo "[1/4] 克隆 libdatachannel..."
            git clone --depth 1 https://github.com/paullouisageneau/libdatachannel.git "$SRC"
            ( cd "$SRC" && git submodule update --init --recursive --depth 1 )
        else
            echo "[1/4] 源码已存在（若需更新请删除 client/deps/libdatachannel-src 后重跑）"
        fi
        # 清空构建目录，避免宿主机曾用该目录生成的 CMakeCache 与容器路径 /workspace 冲突
        rm -rf "$BLD"
        mkdir -p "$BLD" && cd "$BLD"
        echo "[2/4] 配置 CMake..."
        cmake "$SRC" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$INS" \
            -DUSE_GNUTLS=OFF -DUSE_NICE=OFF -DNO_EXAMPLES=ON -DNO_TESTS=ON
        echo "[3/4] 编译..."
        cmake --build . -j$(nproc 2>/dev/null || echo 4)
        echo "[4/4] 安装..."
        cmake --install .
        echo "  Done."
        '
}

echo "=========================================="
echo "安装 libdatachannel → client/deps（供容器挂载）"
echo "=========================================="
echo "  安装路径: $INSTALL_PREFIX"
echo "  容器内将挂载为: /opt/libdatachannel"
echo ""

mkdir -p "$CLIENT_DEPS"

if host_cmake_ok; then
    run_on_host
else
    echo "  使用宿主机本地镜像: $BUILD_IMAGE"
    run_in_docker
fi

echo ""
echo "=========================================="
echo "libdatachannel 已安装到: $INSTALL_PREFIX"
echo "=========================================="
echo "下一步："
echo "  1) 启动/重建 client-dev 后，容器会挂载该路径到 /opt/libdatachannel"
echo "  2) 在容器内编译客户端（make build-client 或 make e2e-full）将自动找到 libdatachannel"
echo "  3) 连接车端后四路将显示「已连接」并接收 RTP（若需解码画面还需 FFmpeg，可选装）"
echo ""
