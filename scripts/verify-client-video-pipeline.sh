#!/bin/bash
# 客户端视频管线自动化验证：源码结构 + CMake 配置 + 可选编译/运行
# 用法：./scripts/verify-client-video-pipeline.sh [--build] [--run] [--in-docker]
#  --build:     在验证通过后尝试编译（需本机或容器内具备 Qt6/FFmpeg/libdatachannel）
#  --run:       编译成功后运行客户端约 5 秒（需 DISPLAY）
#  --in-docker: 在 client-dev 容器内执行（镜像 docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt 内含 Qt6）
# 注意：不使用 set -e，检查函数仅累计 PASS/FAIL，最后统一退出码
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLIENT_DIR="$PROJECT_ROOT/client"
BUILD_VERIFY_DIR="${BUILD_VERIFY_DIR:-/tmp/rd-client-video-verify}"
DO_BUILD=false
DO_RUN=false
DO_DOCKER=false

# 若带 --in-docker 且在宿主机上，则进入 client-dev 容器内执行本脚本（去掉 --in-docker 避免递归）
for x in "$@"; do
    case "$x" in
        --build)      DO_BUILD=true ;;
        --run)        DO_RUN=true ;;
        --in-docker)  DO_DOCKER=true ;;
    esac
done

if [ "$DO_DOCKER" = true ] && [ ! -f /.dockerenv ]; then
    echo "=== 在 client-dev 容器内执行验证（镜像含 Qt6）==="
    EXTRA=""
    [ "$DO_BUILD" = true ] && EXTRA="$EXTRA --build"
    [ "$DO_RUN" = true ]   && EXTRA="$EXTRA --run"
    exec docker compose -f "$PROJECT_ROOT/docker-compose.yml" run --rm \
        -v "$PROJECT_ROOT:/workspace/project:ro" \
        -e CMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel \
        -e BUILD_VERIFY_DIR=/tmp/rd-client-video-verify \
        -e DISPLAY="${DISPLAY:-:0}" \
        client-dev bash -c "cd /workspace/project && bash scripts/verify-client-video-pipeline.sh $EXTRA"
fi

echo "=== 客户端视频管线自动化验证 ==="
echo "  工作目录: $CLIENT_DIR"
echo "  验证用 build 目录: $BUILD_VERIFY_DIR"
echo "  容器内执行: $([ -f /.dockerenv ] && echo '是' || echo '否')"
echo ""

PASS=0
FAIL=0

# ---- 1. 必需文件与符号检查 ----
echo "[1/4] 源码与配置结构..."

check_file() {
    if [ -f "$1" ]; then
        echo "  OK  $1"; ((PASS++)) || true
    else
        echo "  FAIL 缺失: $1"; ((FAIL++)) || true
    fi
}

check_grep() {
    local file="$1"
    local pattern="$2"
    local desc="${3:-$pattern}"
    if [ -f "$file" ] && grep -q "$pattern" "$file" 2>/dev/null; then
        echo "  OK  $file 含 $desc"; ((PASS++)) || true
    else
        echo "  FAIL $file 中未找到: $desc"; ((FAIL++)) || true
    fi
}

check_file "$CLIENT_DIR/src/h264decoder.h"
check_file "$CLIENT_DIR/src/h264decoder.cpp"
check_file "$CLIENT_DIR/Dockerfile.client-dev"
check_file "$PROJECT_ROOT/docker-compose.client-dev.yml"

check_grep "$CLIENT_DIR/CMakeLists.txt" "pkg_check_modules(FFMPEG" "FFmpeg 查找"
check_grep "$CLIENT_DIR/CMakeLists.txt" "h264decoder.cpp" "h264decoder 源文件"
check_grep "$CLIENT_DIR/CMakeLists.txt" "Qt6::Multimedia" "Qt Multimedia 链接"
check_grep "$CLIENT_DIR/CMakeLists.txt" "MultimediaQuick" "Qt MultimediaQuick 依赖"
check_grep "$CLIENT_DIR/src/webrtcclient.h" "QVideoSink" "QVideoSink 属性"
check_grep "$CLIENT_DIR/src/webrtcclient.h" "videoFrameReady" "videoFrameReady 信号"
check_grep "$CLIENT_DIR/src/webrtcclient.cpp" "setupVideoDecoder" "setupVideoDecoder"
check_grep "$CLIENT_DIR/src/webrtcclient.cpp" "feedRtp" "onMessage feedRtp"
check_grep "$CLIENT_DIR/src/main.cpp" "QImage" "QImage 注册"
# 呈现路径：自定义 RemoteVideoSurface（Qt Quick Item）+ onVideoFrameReady；已迁出 DrivingInterface 根文件
check_grep "$CLIENT_DIR/qml/components/VideoPanel.qml" "RemoteVideoSurface" "QML RemoteVideoSurface（侧向 VideoPanel）"
check_grep "$CLIENT_DIR/qml/components/VideoPanel.qml" "onVideoFrameReady" "VideoPanel onVideoFrameReady"
check_grep "$CLIENT_DIR/qml/components/driving/DrivingCenterColumn.qml" "RemoteVideoSurface" "主视 RemoteVideoSurface"
check_grep "$CLIENT_DIR/qml/components/driving/DrivingCenterColumn.qml" "onVideoFrameReady" "主视 onVideoFrameReady"

echo "  结构检查: $PASS 通过, $FAIL 失败"
echo ""

# ---- 2. CMake 配置（独立目录，不污染原有 build） ----
echo "[2/4] CMake 配置（$BUILD_VERIFY_DIR）..."
mkdir -p "$BUILD_VERIFY_DIR"
CMAKE_OK=0
if ! command -v cmake >/dev/null 2>&1; then
    echo "  SKIP 未找到 cmake（PATH 无 cmake 时正常；L1 结构检查仍以上方 grep 为准）"
else
    ( cd "$BUILD_VERIFY_DIR" && cmake "$CLIENT_DIR" -DCMAKE_BUILD_TYPE=Release > /tmp/rd-cmake.log 2>&1 )
    CMAKE_EXIT=$?
    tail -25 /tmp/rd-cmake.log
    echo ""
    echo "========== Client HW decode (CMake; CI grep VA-API / NVDEC) =========="
    grep -E "VA-API:|NVDEC:|Client HW decode" /tmp/rd-cmake.log 2>/dev/null || echo "  (no match — see full /tmp/rd-cmake.log)"
    echo "====================================================================="
    if [ "$CMAKE_EXIT" -eq 0 ]; then
        echo "  OK  CMake 配置成功"; ((PASS++)) || true
        CMAKE_OK=1
    else
        # 本机无 Qt6 时属预期，仅作 SKIP 不记为失败
        if grep -q "Qt6" /tmp/rd-cmake.log 2>/dev/null && grep -qi "did not find\|not find\|Could not find" /tmp/rd-cmake.log 2>/dev/null; then
            echo "  SKIP CMake 未找到 Qt6（本机无 Qt 时正常；带 Qt 环境或容器内可加 --build 验证编译）"
        else
            echo "  FAIL CMake 配置失败（见 /tmp/rd-cmake.log）"; ((FAIL++)) || true
        fi
    fi
fi
echo ""

# ---- 3. 可选：编译 ----
BUILD_OK=0
BINARY=""
if [ "$CMAKE_OK" = "1" ] && [ "$DO_BUILD" = true ]; then
    echo "[3/4] 编译..."
    ( cd "$BUILD_VERIFY_DIR" && cmake --build . -j$(nproc 2>/dev/null || echo 2) ) > /tmp/rd-build.log 2>&1
    BUILD_EXIT=$?
    tail -15 /tmp/rd-build.log
    if [ "$BUILD_EXIT" -eq 0 ]; then
        if [ -f "$BUILD_VERIFY_DIR/RemoteDrivingClient" ] || [ -f "$BUILD_VERIFY_DIR/RemoteDrivingClient.exe" ]; then
            echo "  OK  编译成功，可执行文件已生成"; ((PASS++)) || true
            BINARY="$BUILD_VERIFY_DIR/RemoteDrivingClient"
            [ -f "$BUILD_VERIFY_DIR/RemoteDrivingClient.exe" ] && BINARY="$BUILD_VERIFY_DIR/RemoteDrivingClient.exe"
            BUILD_OK=1
        else
            echo "  FAIL 编译完成但未找到可执行文件"; ((FAIL++)) || true
        fi

        echo "  视频显示路径: Qt Multimedia（无需 .qsb 自定义着色器）"
    else
        echo "  FAIL 编译失败"; ((FAIL++)) || true
    fi
else
    echo "[3/4] 跳过编译（未传 --build 或 CMake 未通过）"
fi
echo ""

# ---- 4. 可选：运行若干秒 ----
if [ "$BUILD_OK" = "1" ] && [ "$DO_RUN" = true ] && [ -n "$BINARY" ]; then
    echo "[4/4] 运行客户端约 5 秒..."
    if [ -z "${DISPLAY}" ]; then
        echo "  SKIP 未设置 DISPLAY，跳过运行验证"
    else
        RUN_EXIT=0
        timeout 5 "$BINARY" --reset-login 2>&1 || RUN_EXIT=$?
        if [ "$RUN_EXIT" = 0 ]; then
            echo "  OK  客户端正常退出"; ((PASS++)) || true
        elif [ "$RUN_EXIT" = 124 ]; then
            echo "  OK  客户端运行 5 秒无崩溃（timeout 正常结束）"; ((PASS++)) || true
        else
            echo "  WARN 客户端退出码 $RUN_EXIT（可能无显示或预期退出）"; ((FAIL++)) || true
        fi
    fi
else
    echo "[4/4] 跳过运行（未传 --run 或未编译成功）"
fi
echo ""

# ---- 汇总 ----
echo "=== 验证汇总 ==="
echo "  通过: $PASS  失败: $FAIL"
if [ "$FAIL" -eq 0 ]; then
    echo "VERIFY_OK: 视频管线结构与配置验证通过"
    echo "  完整编译并运行请: $SCRIPT_DIR/verify-client-video-pipeline.sh --build --run"
    exit 0
else
    echo "VERIFY_FAIL: 存在 $FAIL 项失败"
    exit 1
fi
