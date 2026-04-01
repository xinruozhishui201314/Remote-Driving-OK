#!/bin/bash
# GUI 环境验证脚本（在容器内运行）

set -e

echo "=========================================="
echo "Verifying GUI Environment"
echo "=========================================="

# 检查 DISPLAY 环境变量
if [ -z "$DISPLAY" ]; then
    echo "✗ DISPLAY environment variable not set"
    echo "  GUI applications will not work"
    exit 1
else
    echo "✓ DISPLAY=$DISPLAY"
fi

# 检查 X11 socket
if [ -S /tmp/.X11-unix/X0 ] || [ -d /tmp/.X11-unix ]; then
    echo "✓ X11 socket mounted"
else
    echo "✗ X11 socket not found"
    echo "  GUI applications will not work"
    exit 1
fi

# 检查 Qt 平台插件
if [ -d "$QT_GCC/plugins/platforms" ]; then
    PLUGIN_COUNT=$(ls "$QT_GCC/plugins/platforms"/*.so 2>/dev/null | wc -l)
    echo "✓ Qt platform plugins found ($PLUGIN_COUNT plugins)"
else
    echo "⚠ Qt platform plugins not found"
fi

# 测试 X11 连接
if command -v xdpyinfo > /dev/null 2>&1; then
    if xdpyinfo -display "$DISPLAY" > /dev/null 2>&1; then
        echo "✓ X11 connection verified"
        XINFO=$(xdpyinfo -display "$DISPLAY" 2>/dev/null | grep -E "dimensions|resolution" | head -2)
        echo "  $XINFO"
    else
        echo "✗ X11 connection failed"
        echo "  Run on host: xhost +local:docker"
        exit 1
    fi
else
    echo "⚠ xdpyinfo not available (skipping connection test)"
    echo "  Install: sudo apt-get install x11-utils"
fi

# 检查必要的库
echo ""
echo "Checking required libraries..."
MISSING_LIBS=0

if ldconfig -p | grep -q libX11.so; then
    echo "✓ libX11 found"
else
    echo "✗ libX11 not found"
    MISSING_LIBS=$((MISSING_LIBS + 1))
fi

if ldconfig -p | grep -q libxcb.so; then
    echo "✓ libxcb found"
else
    echo "✗ libxcb not found"
    MISSING_LIBS=$((MISSING_LIBS + 1))
fi

if [ $MISSING_LIBS -gt 0 ]; then
    echo ""
    echo "⚠ Missing $MISSING_LIBS library(ies)"
    echo "  Install: sudo apt-get install libx11-dev libxcb1-dev"
fi

echo ""
echo "=========================================="
if [ $MISSING_LIBS -eq 0 ]; then
    echo "✓ GUI environment ready!"
    echo "=========================================="
    echo ""
    echo "You can now run Qt GUI applications:"
    echo "  ./build/bin/your_app"
    echo ""
    echo "To test GUI:"
    echo "  xeyes &  (if x11-apps installed)"
    echo ""
else
    echo "⚠ GUI environment has issues"
    echo "=========================================="
    echo ""
    echo "Please install missing libraries and retry"
    echo ""
fi
