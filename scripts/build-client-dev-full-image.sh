#!/bin/bash
# 在宿主机拉取/编译 libdatachannel，挂载进基础 Qt 容器，在容器内“安装”（复制到 /opt）后提交为完备镜像并打 tag。
# 下次直接使用该镜像启动即可，无需在镜像构建时拉取或编译。
#
# 用法：bash scripts/build-client-dev-full-image.sh [镜像 tag]
# 默认 tag：remote-driving-client-dev:full
#
# 依赖：宿主机 cmake、g++、git、libssl-dev；Docker 可拉取 docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt
#
# 可选：加速 Qt 模块下载（aqtinstall -b），与 Dockerfile AQT_BASE 一致：
#   export AQT_BASE=https://mirrors.tuna.tsinghua.edu.cn/qt/

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

IMAGE_TAG="${1:-remote-driving-client-dev:full}"
# 优先使用宿主机上已有的 client-dev 镜像（如曾用 Dockerfile 构建过），否则用宿主机上的 Qt 基础镜像，不拉取官方
if [ -z "$BASE_IMAGE" ]; then
  if docker image inspect remote-driving-client-dev:full >/dev/null 2>&1; then
    BASE_IMAGE="remote-driving-client-dev:full"
  else
    BASE_IMAGE="docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt"
  fi
fi
INSTALL_DIR="$PROJECT_ROOT/client/deps/libdatachannel-install"
CONTAINER_NAME="client-dev-full-builder-$$"

echo "=========================================="
echo "生成完备 client-dev 镜像并打 tag"
echo "=========================================="
echo "  目标镜像: $IMAGE_TAG"
echo "  基础镜像: $BASE_IMAGE（宿主机本地镜像，不拉取）"
echo ""

# 1. 宿主机上确保已安装 libdatachannel 到 client/deps/libdatachannel-install
if [ ! -f "$INSTALL_DIR/lib/cmake/LibDataChannel/LibDataChannelConfig.cmake" ] && [ ! -f "$INSTALL_DIR/lib/libdatachannel.so" ]; then
    echo "[1/3] 宿主机尚未安装 libdatachannel，先执行安装..."
    bash "$SCRIPT_DIR/install-libdatachannel-for-client.sh"
else
    echo "[1/3] 宿主机已存在 libdatachannel 安装: $INSTALL_DIR"
fi

if [ ! -d "$INSTALL_DIR" ] || [ -z "$(ls -A "$INSTALL_DIR" 2>/dev/null)" ]; then
    echo "错误: $INSTALL_DIR 为空或不存在，请先运行 bash scripts/install-libdatachannel-for-client.sh" >&2
    exit 1
fi

# 2. 启动临时容器（仅 Qt 基础镜像，以 root 运行以便创建 /opt/libdatachannel）
echo "[2/3] 启动临时容器并写入 /opt/libdatachannel..."
docker run -d --user root --name "$CONTAINER_NAME" \
    -v "$INSTALL_DIR:/mnt/libdatachannel-install:ro" \
    "$BASE_IMAGE" \
    sleep infinity

cleanup() {
    docker rm -f "$CONTAINER_NAME" 2>/dev/null || true
}
trap cleanup EXIT

# 3. 在容器内将挂载内容复制到 /opt/libdatachannel（写入容器层，便于后续 commit）
docker exec "$CONTAINER_NAME" bash -c "mkdir -p /opt/libdatachannel && cp -a /mnt/libdatachannel-install/. /opt/libdatachannel/"

# 4. 在容器内安装依赖项（必须成功）
echo "  安装系统依赖项..."
docker exec "$CONTAINER_NAME" bash -c "
    # 设置 apt 非交互模式
    export DEBIAN_FRONTEND=noninteractive
    mkdir -p /var/lib/apt/lists/partial \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        # 中文字体
        fonts-wqy-zenhei \
        # 开发工具
        build-essential \
        cmake \
        pkg-config \
        # FFmpeg 开发库
        libavcodec-dev \
        libavutil-dev \
        libswscale-dev \
        libavformat-dev \
        libswresample-dev \
        # 网络工具
        mosquitto-clients \
        curl \
        wget \
        # Qt 依赖
        libxcb-xinerama0 \
        libxcb-cursor0 \
        libxkbcommon-x11-0 \
        libpulse0 \
        libpulse-dev \
        # 其他
        git \
    && rm -rf /var/lib/apt/lists/* \
    && fc-cache -fv \
    && echo '✓ 系统依赖项已安装'
" || {
    echo "错误: 安装系统依赖项失败" >&2
    exit 1
}

# 4b. 安装 qsb（Qt ShaderTools 编译器），用于将 GLSL 着色器编译为 .qsb 二进制
#     qsb 是 GPU 视频渲染管线的必要组件；无 qsb → 无 .qsb → 无视频画面（黑屏）
#     策略：qsb + Qt Multimedia 齐全则跳过；否则 aqt 安装 qtshadertools + qtmultimedia（与 Dockerfile.client-dev 一致）
echo "  检查 / 安装 qsb + Qt Multimedia..."
docker exec -e AQT_BASE="${AQT_BASE:-}" "$CONTAINER_NAME" bash -c "
    set -e
    export DEBIAN_FRONTEND=noninteractive

    # ── 步骤 1：检查 qsb + Qt Multimedia（与 Dockerfile.client-dev / start-full-chain 一致）──
    _QSB_OK=0
    [ -x /opt/Qt/6.8.0/gcc_64/bin/qsb ] && _QSB_OK=1
    _MM_OK=0
    if [ -f /opt/Qt/6.8.0/gcc_64/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake ] \\
        && { [ -f /opt/Qt/6.8.0/gcc_64/lib/cmake/Qt6MultimediaQuick/Qt6MultimediaQuickConfig.cmake ] \\
            || [ -f /opt/Qt/6.8.0/gcc_64/lib/libQt6MultimediaQuick.so ]; }; then
        _MM_OK=1
    fi
    if [ \"\$_QSB_OK\" -eq 1 ] && [ \"\$_MM_OK\" -eq 1 ]; then
        echo '✓ qsb 与 Qt Multimedia 已齐全，跳过 aqt 安装'
        exit 0
    fi
    if [ \"\$_QSB_OK\" -eq 0 ]; then
        echo '  qsb 未找到，将通过 aqt 安装 qtshadertools...'
    fi
    if [ \"\$_MM_OK\" -eq 0 ]; then
        echo '  Qt Multimedia 未齐全，将通过 aqt 安装 qtmultimedia...'
    fi

    # ── 步骤 2：确保有 root 权限（apt-get 需要） ─────────────────────────
    if ! id | grep -q 'uid=0'; then
        echo '  切换到 root 用户...'
        # 重新以 root 启动容器（此处仅记录；实际已在 run -d --user root 时指定）
        true
    fi

    # ── 步骤 3：安装 python3-pip（如不存在） ──────────────────────────────
    if ! command -v pip3 &>/dev/null; then
        echo '  安装 python3-pip（apt）...'
        apt-get update -qq
        apt-get install -y --no-install-recommends python3-pip 2>&1 | tail -2
        rm -rf /var/lib/apt/lists/partial
    fi

    # ── 步骤 4：安装 aqtinstall（如不存在）─────────────────────────────────
    if ! command -v aqt &>/dev/null; then
        echo '  安装 aqtinstall（清华镜像）...'
        pip3 install --no-cache-dir -i https://pypi.tuna.tsinghua.edu.cn/simple aqtinstall 2>&1 | tail -2
    fi

    # ── 步骤 5：通过 aqt 安装 qtshadertools + qtmultimedia（与 Dockerfile 一致）────────
    echo '  通过 aqt 安装 qtshadertools + qtmultimedia...'
    if [ -n \"\${AQT_BASE:-}\" ]; then
        echo \"  使用 Qt 下载镜像: \${AQT_BASE}\"
        aqt install-qt -b \"\${AQT_BASE}\" -O /opt/Qt \
            linux desktop 6.8.0 linux_gcc_64 \
            -m qtshadertools -m qtmultimedia 2>&1 | tail -12
    else
        aqt install-qt -O /opt/Qt \
            linux desktop 6.8.0 linux_gcc_64 \
            -m qtshadertools -m qtmultimedia 2>&1 | tail -12
    fi

    # ── 步骤 6：验证 qsb 与 Multimedia ─────────────────────────────────────
    if [ -x /opt/Qt/6.8.0/gcc_64/bin/qsb ]; then
        echo '✓ qsb: /opt/Qt/6.8.0/gcc_64/bin/qsb'
        /opt/Qt/6.8.0/gcc_64/bin/qsb --version 2>&1 | grep -v 'Detected locale' | head -1
    else
        echo '✗ 错误: qsb 安装失败' >&2
        ls /opt/Qt/6.8.0/gcc_64/bin/ 2>&1 | head -10 || true
        exit 1
    fi
    if [ ! -f /opt/Qt/6.8.0/gcc_64/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake ]; then
        echo '✗ 错误: Qt6Multimedia CMake 未安装' >&2
        exit 1
    fi
    if [ ! -f /opt/Qt/6.8.0/gcc_64/lib/cmake/Qt6MultimediaQuick/Qt6MultimediaQuickConfig.cmake ] \\
        && [ ! -f /opt/Qt/6.8.0/gcc_64/lib/libQt6MultimediaQuick.so ]; then
        echo '✗ 错误: Qt MultimediaQuick 库/CMake 未安装' >&2
        exit 1
    fi
    echo '✓ Qt Multimedia 模块已就绪（CMake + libQt6MultimediaQuick 或 Config）'
" || {
    echo "错误: qsb 安装流程失败，GPU 视频渲染将失效（黑屏）" >&2
    echo "提示: 可手动检查: docker exec $CONTAINER_NAME bash -c 'ls /opt/Qt/6.8.0/gcc_64/bin/qsb'" >&2
    exit 1
}

# 5. 安装 Python 依赖（用于调试工具）
echo "  安装 Python 依赖项..."
docker exec "$CONTAINER_NAME" bash -c "
    python3 -m pip install --no-cache-dir --upgrade pip \
    && python3 -m pip install --no-cache-dir \
        paho-mqtt \
        requests \
        websocket-client \
        pyyaml \
    && echo '✓ Python 依赖项已安装'
" || {
    echo "错误: 安装 Python 依赖项失败" >&2
    exit 1
}

# 5. 预编译客户端（如果代码可用）
echo "[3/4] 尝试预编译客户端..."
if [ -d "$PROJECT_ROOT/client/src" ] && [ -f "$PROJECT_ROOT/client/CMakeLists.txt" ]; then
    echo "  客户端代码可用，开始预编译..."
    # 挂载客户端代码到容器
    docker cp "$PROJECT_ROOT/client" "$CONTAINER_NAME:/workspace/" 2>/dev/null || {
        echo "警告: 无法挂载客户端代码，将在运行时编译"
        echo "✓ 跳过预编译阶段"
        exit 0
    }
    
    # 预编译客户端
    docker exec "$CONTAINER_NAME" bash -c "
        echo '开始预编译客户端...'
        mkdir -p /tmp/client-build \
        && cd /tmp/client-build \
        && echo '配置 CMake...' \
        && cmake /workspace/client \
            -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_FLAGS='-O2 -march=native -flto' \
            -DCMAKE_EXE_LINKER_FLAGS='-flto' \
        && echo '编译项目...' \
        && cmake --build . -j\$(nproc 2>/dev/null || echo 4) \
        && [ -x ./RemoteDrivingClient ] \
        && echo '✓ 客户端预编译成功: /tmp/client-build/RemoteDrivingClient' \
        || { echo '⚠ 客户端预编译失败（将在运行时编译）'; exit 1; }
    " 2>&1 | grep -E "(✓|⚠|error|Error|WARNING|Shader|shader|begin|编译|配置|预编译)" || true

    # 验证 .qsb 着色器文件已生成（缺失则 GPU 渲染失败，界面黑屏）
    _QSB_CHECK=$(docker exec "$CONTAINER_NAME" bash -c 'ls /tmp/client-build/shaders_gen/video.vert.qsb /tmp/client-build/shaders_gen/video.frag.qsb 2>&1')
    if echo "$_QSB_CHECK" | grep -q 'No such file'; then
        echo "⚠ 警告: .qsb 着色器文件未生成，GPU 视频渲染将失败（黑屏）"
        echo "  提示: 确保 qsb 工具已安装（见上方 qsb 安装步骤）"
    else
        echo "✓ .qsb 着色器文件已生成: $_QSB_CHECK"
    fi
    
    # 创建启动脚本（stdin 写入容器内文件，避免嵌套 heredoc 引号错误）
    docker exec -i "$CONTAINER_NAME" bash -c 'cat > /usr/local/bin/start-client.sh' <<'STARTCLIENT'
#!/bin/bash
set -e
export DISPLAY=${DISPLAY:-:0}
export QT_QPA_PLATFORM=xcb
export ZLM_VIDEO_URL=http://zlmediakit:80
export MQTT_BROKER_URL=mqtt://mosquitto:1883

cd /tmp/client-build
if [ -x ./RemoteDrivingClient ]; then
    echo '启动预编译的客户端...'
    ./RemoteDrivingClient "$@"
else
    echo '未找到预编译客户端，正在重新编译...'
    mkdir -p /tmp/client-build
    cd /tmp/client-build
    cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug
    make -j4
    ./RemoteDrivingClient "$@"
fi
STARTCLIENT

    docker exec "$CONTAINER_NAME" chmod +x /usr/local/bin/start-client.sh
    echo "✓ 已创建客户端启动脚本: /usr/local/bin/start-client.sh"
else
    echo "  客户端代码不可用，跳过预编译（将在运行时编译）"
fi

# 6. 提交容器为镜像并打 tag（运行时 CMAKE_PREFIX_PATH 由项目脚本传入，包含 /opt/libdatachannel）
echo "[4/4] 提交容器为镜像并打 tag: $IMAGE_TAG"
docker commit "$CONTAINER_NAME" "$IMAGE_TAG"

echo ""
echo "=========================================="
echo "完备镜像已生成: $IMAGE_TAG（已具备运行条件）"
echo "=========================================="
echo "该镜像已具备运行条件（Qt6、libdatachannel、FFmpeg、xcb 等），下次直接启动即可："
echo "  bash scripts/start-all-nodes-and-verify.sh"
echo "  或 docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml up -d client-dev"
echo ""
