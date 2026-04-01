#!/bin/bash
# X11 权限设置脚本（在主机上运行）
# 此脚本在容器启动前自动设置 X11 显示权限

set -e

echo "=========================================="
echo "Setting up X11 permissions for Docker"
echo "=========================================="

# 检测 DISPLAY 环境变量
if [ -z "$DISPLAY" ]; then
    # 尝试自动检测 DISPLAY
    if [ -S /tmp/.X11-unix/X0 ]; then
        export DISPLAY=:0
        echo "✓ Auto-detected DISPLAY=:0"
    else
        # 尝试从当前用户会话获取
        DISPLAY=$(ps e | grep -oP 'DISPLAY=\K[^\s]+' | head -1)
        if [ -n "$DISPLAY" ]; then
            export DISPLAY
            echo "✓ Found DISPLAY=$DISPLAY from process"
        else
            export DISPLAY=:0
            echo "⚠ Using default DISPLAY=:0 (may need manual adjustment)"
        fi
    fi
else
    echo "✓ Using DISPLAY=$DISPLAY"
fi

# 检查 xhost 命令是否存在
if ! command -v xhost > /dev/null 2>&1; then
    echo "⚠ xhost not found. Installing x11-xserver-utils..."
    sudo apt-get update -qq > /dev/null 2>&1
    sudo apt-get install -y -qq x11-xserver-utils > /dev/null 2>&1 || {
        echo "✗ Failed to install x11-xserver-utils"
        echo "  Please install manually: sudo apt-get install x11-xserver-utils"
        exit 1
    }
fi

# 设置 X11 权限（允许本地连接）
echo "Setting X11 permissions..."
xhost +local:docker > /dev/null 2>&1 || {
    echo "⚠ Failed to set xhost +local:docker, trying alternative method..."
    xhost +SI:localuser:$(whoami) > /dev/null 2>&1 || {
        echo "⚠ Alternative method also failed, trying xhost +..."
        xhost + > /dev/null 2>&1 || {
            echo "✗ Failed to set X11 permissions. GUI applications may not work."
            echo "  Try running manually: xhost +local:docker"
            exit 1
        }
    }
}

echo "✓ X11 permissions set successfully"
echo "  DISPLAY=$DISPLAY"
echo "  X11 socket: /tmp/.X11-unix"

# 验证 X11 连接
if command -v xdpyinfo > /dev/null 2>&1; then
    if xdpyinfo -display "$DISPLAY" > /dev/null 2>&1; then
        echo "✓ X11 connection verified"
    else
        echo "⚠ X11 connection verification failed (may still work)"
    fi
fi

echo ""
echo "=========================================="
echo "X11 setup completed!"
echo "=========================================="
echo ""
