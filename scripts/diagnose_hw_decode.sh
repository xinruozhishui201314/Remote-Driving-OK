#!/bin/bash
# 硬件解码诊断和快速修复脚本
# 使用：./diagnose_hw_decode.sh

set -e

echo "════════════════════════════════════════════════════════════════"
echo "客户端硬件解码诊断工具 v1.0"
echo "════════════════════════════════════════════════════════════════"
echo ""

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_error() { echo -e "${RED}❌ $1${NC}"; }
echo_success() { echo -e "${GREEN}✅ $1${NC}"; }
echo_warning() { echo -e "${YELLOW}⚠️  $1${NC}"; }
echo_info() { echo "ℹ️  $1"; }

# ════════════════════════════════════════════════════════════════
# 1. 环境变量检查
# ════════════════════════════════════════════════════════════════
echo ""
echo "【步骤1】环境变量检查"
echo "─────────────────────────────────────────"

if [ -z "$CLIENT_MEDIA_HARDWARE_DECODE" ]; then
    echo_warning "CLIENT_MEDIA_HARDWARE_DECODE 未设置（默认启用）"
else
    echo_info "CLIENT_MEDIA_HARDWARE_DECODE=$CLIENT_MEDIA_HARDWARE_DECODE"
fi

if [ -z "$CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE" ]; then
    echo_warning "CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE 未设置（默认禁用）"
else
    echo_info "CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=$CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE"
fi

if [ -z "$CLIENT_STRICT_HW_DECODE_REQUIRED" ]; then
    echo_info "CLIENT_STRICT_HW_DECODE_REQUIRED 未设置（严格模式禁用）"
else
    echo_info "CLIENT_STRICT_HW_DECODE_REQUIRED=$CLIENT_STRICT_HW_DECODE_REQUIRED"
fi

# ════════════════════════════════════════════════════════════════
# 2. GPU 设备检查
# ════════════════════════════════════════════════════════════════
echo ""
echo "【步骤2】GPU 设备检查"
echo "─────────────────────────────────────────"

if [ -e /dev/dri/renderD128 ]; then
    echo_success "/dev/dri/renderD128 存在"
    ls -lh /dev/dri/renderD* 2>/dev/null | head -5
else
    echo_error "/dev/dri/renderD128 不存在（无 GPU 设备）"
fi

# ════════════════════════════════════════════════════════════════
# 3. VA-API 驱动检查
# ════════════════════════════════════════════════════════════════
echo ""
echo "【步骤3】VA-API 驱动检查"
echo "─────────────────────────────────────────"

VAAPI_DRIVERS=(
    "/usr/lib/x86_64-linux-gnu/dri/iHD_drv_video.so"
    "/usr/lib/x86_64-linux-gnu/dri/i965_drv_video.so"
    "/usr/lib/dri/iHD_drv_video.so"
    "/usr/lib/dri/i965_drv_video.so"
)

VAAPI_FOUND=0
for driver in "${VAAPI_DRIVERS[@]}"; do
    if [ -e "$driver" ]; then
        echo_success "找到 VA-API 驱动：$driver"
        VAAPI_FOUND=1
    fi
done

if [ $VAAPI_FOUND -eq 0 ]; then
    echo_warning "未找到 VA-API 驱动文件"
    echo_info "尝试安装：sudo apt-get install -y libva-drivers intel-media-driver mesa-va-drivers"
fi

# ════════════════════════════════════════════════════════════════
# 4. libva 库检查
# ════════════════════════════════════════════════════════════════
echo ""
echo "【步骤4】libva 库检查"
echo "─────────────────────────────────────────"

if command -v vainfo &> /dev/null; then
    echo_success "vainfo 命令可用"
    echo ""
    vainfo 2>&1 | head -10
    echo ""
else
    echo_warning "vainfo 命令不可用，尝试安装：sudo apt-get install -y vainfo"
fi

# ════════════════════════════════════════════════════════════════
# 5. 日志分析（如果提供了日志路径）
# ════════════════════════════════════════════════════════════════
if [ -n "$1" ] && [ -f "$1" ]; then
    echo ""
    echo "【步骤5】日志分析"
    echo "─────────────────────────────────────────"
    
    LOGFILE="$1"
    echo_info "分析日志文件：$LOGFILE"
    echo ""
    
    # 检查硬解相关错误
    if grep -q "HW-REQUIRED\|hardware.*decode.*require" "$LOGFILE"; then
        echo_error "检测到硬解强制失败错误"
        grep "HW-REQUIRED" "$LOGFILE" | head -3
    fi
    
    if grep -q "vaInitialize failed" "$LOGFILE"; then
        echo_error "检测到 VA-API 初始化失败"
        grep "vaInitialize failed" "$LOGFILE" | head -1
    fi
    
    if grep -q "selected FFmpegSoftDecoder" "$LOGFILE"; then
        echo_success "检测到使用 FFmpeg 软解"
        grep "selected FFmpegSoftDecoder" "$LOGFILE" | head -1
    fi
    
    if grep -q "selected VAAPIDecoder\|selected NvdecDecoder" "$LOGFILE"; then
        echo_success "检测到使用硬件解码"
        grep "selected.*Decoder" "$LOGFILE" | head -1
    fi
    
    echo ""
    echo "关键诊断日志摘录："
    echo "─────────────────────────────────────────"
    grep -i "HW-E2E\|DecoderFactory\|ensureDecoder\|CodecHealth" "$LOGFILE" | \
        tail -20 | sed 's/^/  /'
fi

# ════════════════════════════════════════════════════════════════
# 6. 快速修复建议
# ════════════════════════════════════════════════════════════════
echo ""
echo "【快速修复方案】"
echo "─────────────────────────────────────────"
echo ""

if [ "$CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE" = "1" ] || \
   [ "$CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE" = "true" ]; then
    echo_warning "当前强制硬解模式，但硬解可能不可用"
    echo ""
    echo "【短期修复】允许降级到软解（立即生效）："
    echo "  export CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0"
    echo "  ./RemoteDrivingClient"
    echo ""
fi

if [ $VAAPI_FOUND -eq 0 ]; then
    echo "【长期修复】安装 VA-API 驱动："
    echo "  sudo apt-get update"
    echo "  sudo apt-get install -y libva2 libva-drm2 intel-media-driver mesa-va-drivers"
    echo ""
fi

# ════════════════════════════════════════════════════════════════
# 7. 配置检查
# ════════════════════════════════════════════════════════════════
echo ""
echo "【步骤6】配置文件检查"
echo "─────────────────────────────────────────"

CONFIG_FILE="client/config/client_config.yaml"
if [ -f "$CONFIG_FILE" ]; then
    echo_info "检查 $CONFIG_FILE"
    if grep -q "require_hardware_decode: true" "$CONFIG_FILE"; then
        echo_warning "配置指定 require_hardware_decode: true（优选硬解）"
    elif grep -q "require_hardware_decode: false" "$CONFIG_FILE"; then
        echo_info "配置指定 require_hardware_decode: false（软解优先）"
    fi
fi

echo ""
echo "════════════════════════════════════════════════════════════════"
echo "诊断完成"
echo ""
echo "后续步骤："
echo "  1. 如果视频无法显示，尝试：export CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0"
echo "  2. 如果需要硬解，安装驱动并重启应用"
echo "  3. 查看完整指南：docs/HARDWARE_DECODE_TROUBLESHOOTING.md"
echo "════════════════════════════════════════════════════════════════"
