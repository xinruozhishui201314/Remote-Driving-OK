#!/bin/bash
# Dev Container 初始化脚本
# 在容器启动后自动执行（后台运行）

# 不设置 set -e，允许脚本在后台运行而不因错误退出
# set -e

echo "=========================================="
echo "Qt6 Remote Driving Dev Container Setup"
echo "=========================================="

# 检查 Qt 环境
echo "Checking Qt environment..."
if [ -d "$QT_GCC" ]; then
    echo "✓ Qt found at: $QT_GCC"
    qmake --version || echo "⚠ qmake not in PATH"
else
    echo "✗ Qt not found at: $QT_GCC"
fi

# 检查 CMake
echo "Checking CMake..."
cmake --version || echo "⚠ CMake not found"

# 检查编译器
echo "Checking compiler..."
g++ --version || echo "⚠ g++ not found"

# 设置工作目录权限
if [ -d "/workspace" ]; then
    echo "Setting workspace permissions..."
    sudo chown -R user:user /workspace 2>/dev/null || true
fi

# 创建构建目录（如果不存在）
if [ ! -d "/workspace/build" ]; then
    echo "Creating build directory..."
    mkdir -p /workspace/build
fi

# 修复所有脚本的执行权限（处理 noexec 挂载问题）
echo "Fixing script permissions..."
if [ -d "/workspace" ]; then
    find /workspace -name "*.sh" -type f -exec chmod +x {} \; 2>/dev/null || true
    echo "✓ Script permissions fixed"
fi

# 安装常用工具、OpenGL 开发库和中文字体（Qt6 必需）
echo "Installing common tools, OpenGL development libraries and Chinese fonts..."
sudo apt-get update -qq > /dev/null 2>&1 || true
sudo apt-get install -y -qq \
    vim \
    git \
    curl \
    wget \
    gdb \
    build-essential \
    pkg-config \
    iputils-ping \
    iproute2 \
    dnsutils \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libxcb-xinerama0-dev \
    libxcb-cursor-dev \
    libxcb-keysyms1-dev \
    libxcb-image0-dev \
    libxcb-shm0-dev \
    libxcb-icccm4-dev \
    libxcb-sync-dev \
    libxcb-xfixes0-dev \
    libxcb-shape0-dev \
    libxcb-randr0-dev \
    libxcb-render-util0-dev \
    libxcb-util-dev \
    libxcb-xkb-dev \
    libxkbcommon-dev \
    libxkbcommon-x11-dev \
    fonts-wqy-zenhei \
    fonts-wqy-microhei \
    fonts-noto-cjk \
    fontconfig \
    > /dev/null 2>&1 || echo "⚠ Some packages may not be installed"

# 刷新字体缓存
if command -v fc-cache > /dev/null 2>&1; then
    echo "Refreshing font cache..."
    sudo fc-cache -fv > /dev/null 2>&1 || true
fi

# 检查 GUI 环境（X11）
echo ""
echo "Checking GUI environment..."
if [ -n "$DISPLAY" ] && [ -S /tmp/.X11-unix/X0 ]; then
    echo "✓ X11 DISPLAY detected: $DISPLAY"
    echo "✓ X11 socket mounted: /tmp/.X11-unix"
    
    # 检查 Qt GUI 平台插件
    if [ -d "$QT_GCC/plugins/platforms" ]; then
        echo "✓ Qt platform plugins found"
        export QT_QPA_PLATFORM=xcb
    else
        echo "⚠ Qt platform plugins not found"
    fi
    
    # 测试 X11 连接（如果 xdpyinfo 可用）
    if command -v xdpyinfo > /dev/null 2>&1; then
        if xdpyinfo -display "$DISPLAY" > /dev/null 2>&1; then
            echo "✓ X11 connection verified"
        else
            echo "⚠ X11 connection test failed (may still work)"
        fi
    else
        echo "ℹ Install x11-utils to verify X11 connection: sudo apt-get install x11-utils"
    fi
else
    echo "⚠ X11 not available - GUI applications may not work"
    echo "  DISPLAY=$DISPLAY"
    echo "  X11 socket: $([ -S /tmp/.X11-unix/X0 ] && echo 'found' || echo 'not found')"
fi

echo ""
echo "=========================================="
echo "Setup completed!"
echo "=========================================="
echo ""
echo "Quick commands:"
echo "  qmake --version    - Check Qt version"
echo "  cmake --version    - Check CMake version"
echo "  cd build && cmake .. -DCMAKE_PREFIX_PATH=\$QT_GCC - Configure project"
echo ""
echo "GUI Support:"
echo "  DISPLAY=$DISPLAY"
echo "  QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
echo "  To test GUI: xeyes &  (if x11-apps installed)"
echo ""
