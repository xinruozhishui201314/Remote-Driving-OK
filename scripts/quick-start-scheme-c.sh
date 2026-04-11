#!/bin/bash
# 方案C：快速开始脚本
# 一键编译多硬件支持版本

set -e

echo "============================================================"
echo "方案C：多硬件编译快速开始"
echo "============================================================"
echo ""
echo "本脚本会："
echo "1. 清理旧构建"
echo "2. 编译VA-API (Intel/AMD) + NVDEC (NVIDIA) 支持"
echo "3. 验证编译结果"
echo ""
read -p "继续? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    exit 1
fi

PROJECT_DIR="/home/wqs/Documents/github/Remote-Driving"
cd "$PROJECT_DIR/client"

echo ""
echo "[1/4] 清理旧构建..."
rm -rf build
echo "  ✓ 清理完成"

echo ""
echo "[2/4] 创建构建目录..."
mkdir -p build
cd build
echo "  ✓ 目录创建完成"

echo ""
echo "[3/4] 配置CMake（多硬件支持）..."
cmake -DENABLE_VAAPI_NVDEC_ALL=ON \
       -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
       .. || exit 1
echo "  ✓ CMake 配置完成"

echo ""
echo "[4/4] 编译源代码..."
num_jobs=$(nproc)
make -j"$num_jobs" || exit 1
echo "  ✓ 编译完成"

echo ""
echo "============================================================"
echo "✓ 构建成功！"
echo "============================================================"
echo ""
echo "硬解器配置："
cmake -LA | grep -E "^ENABLE_VAAPI|^ENABLE_NVDEC|^ENABLE_FFMPEG" || true
echo ""
echo "下一步："
echo "  1. 运行应用："
echo "     cd $PROJECT_DIR && ./run.sh"
echo ""
echo "  2. 监控硬解状态："
echo "     tail -f logs/*/client-*.log | grep DecoderFactory"
echo ""
echo "  3. 查看详细指南："
echo "     less docs/SCHEME_C_MULTI_HARDWARE_BUILD.md"
echo ""
