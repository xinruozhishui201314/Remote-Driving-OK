#!/bin/bash
# build-with-all-hw-decoders.sh - 方案C：多硬件编译脚本
# 用途：同时编译VA-API和NVDEC支持
# 使用：./build-with-all-hw-decoders.sh [Release|Debug]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_TYPE="${1:-Release}"
CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-}"

echo "==================================================================="
echo "方案C：多硬件编译脚本"
echo "==================================================================="
echo "项目目录：$PROJECT_DIR"
echo "构建类型：$BUILD_TYPE"
echo "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH:-未指定（使用系统默认）}"
echo ""

# 检查依赖
echo "[1/5] 检查编译依赖..."
missing_deps=0

for cmd in cmake make pkg-config; do
    if ! command -v "$cmd" &> /dev/null; then
        echo "  ❌ 缺失: $cmd"
        missing_deps=$((missing_deps + 1))
    else
        version=$($cmd --version 2>&1 | head -1)
        echo "  ✓ $version"
    fi
done

if [ $missing_deps -gt 0 ]; then
    echo ""
    echo "❌ 缺失必要工具，请安装："
    echo "   Debian/Ubuntu: sudo apt-get install cmake make pkg-config qt6-base-dev"
    exit 1
fi

# 检查FFmpeg
echo ""
echo "[2/5] 检查FFmpeg和硬解库..."
ffmpeg_found=0
vaapi_found=0
nvdec_found=0

if pkg-config --exists libavcodec libavutil libswscale; then
    ffmpeg_version=$(pkg-config --modversion libavcodec)
    echo "  ✓ FFmpeg: $ffmpeg_version"
    ffmpeg_found=1
else
    echo "  ⚠ FFmpeg 未找到（可选但推荐）"
fi

if pkg-config --exists libva libva-drm; then
    vaapi_version=$(pkg-config --modversion libva)
    echo "  ✓ VA-API: $vaapi_version"
    vaapi_found=1
else
    echo "  ⚠ VA-API 未找到"
fi

if command -v nvidia-smi &> /dev/null; then
    nvidia_driver=$(nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>/dev/null | head -1)
    echo "  ✓ NVIDIA GPU (驱动: $nvidia_driver)"
    if ffmpeg -codecs 2>/dev/null | grep -q h264_nvdec; then
        echo "  ✓ FFmpeg 支持 NVDEC"
        nvdec_found=1
    else
        echo "  ⚠ FFmpeg 不支持 NVDEC（需要CUDA编译的FFmpeg）"
    fi
else
    echo "  ℹ 未检测到 NVIDIA GPU"
fi

echo ""

# 清理旧构建
echo "[3/5] 清理旧构建..."
if [ -d "$PROJECT_DIR/build" ]; then
    echo "  删除: $PROJECT_DIR/build"
    rm -rf "$PROJECT_DIR/build"
fi

# 创建构建目录
mkdir -p "$PROJECT_DIR/build"
cd "$PROJECT_DIR/build"
echo "  创建: $PROJECT_DIR/build"

# 配置CMake
echo ""
echo "[4/5] 配置CMake..."
CMAKE_ARGS=(
    "-DENABLE_VAAPI_NVDEC_ALL=ON"
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
)

if [ -n "$CMAKE_PREFIX_PATH" ]; then
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH")
fi

echo "  CMake 参数："
for arg in "${CMAKE_ARGS[@]}"; do
    echo "    $arg"
done
echo ""

cmake "${CMAKE_ARGS[@]}" .. || {
    echo "❌ CMake 配置失败"
    exit 1
}

# 编译
echo ""
echo "[5/5] 编译源代码..."
num_jobs=$(nproc)
echo "  使用 $num_jobs 个并行任务编译..."
echo ""

make -j"$num_jobs" || {
    echo "❌ 编译失败"
    exit 1
}

# 验证
echo ""
echo "==================================================================="
echo "✓ 编译完成！"
echo "==================================================================="
echo ""

echo "硬解器支持情况："
cmake -LA | grep -E "^ENABLE_VAAPI|^ENABLE_NVDEC|^ENABLE_FFMPEG" || true
echo ""

echo "可执行文件："
ls -lh RemoteDrivingClient
echo ""

echo "链接的库："
ldd RemoteDrivingClient | grep -E "libva|libav|libcuda" || echo "  （无硬解库链接）"
echo ""

echo "下一步："
echo "  运行应用："
echo "    cd $PROJECT_DIR"
echo "    ./run.sh"
echo ""
echo "  或手动运行："
echo "    cd $PROJECT_DIR/build"
echo "    ./RemoteDrivingClient"
echo ""
echo "  查看硬解状态："
echo "    tail -f ../logs/*/client-*.log | grep -E 'VAAPI|NVDEC|DecoderFactory'"
echo ""
