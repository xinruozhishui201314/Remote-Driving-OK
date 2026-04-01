#!/bin/bash
# Docker Compose 安装脚本

set -e

echo "=========================================="
echo "Docker Compose 安装脚本"
echo "=========================================="
echo ""

# 检查是否已安装
if command -v docker-compose >/dev/null 2>&1; then
    echo "✓ docker-compose 已安装:"
    docker-compose version
    exit 0
fi

if docker compose version >/dev/null 2>&1; then
    echo "✓ docker compose (插件版本) 已安装:"
    docker compose version
    exit 0
fi

echo "检测到 Docker Compose 未安装，开始安装..."
echo ""

# 方式1: 尝试使用 pip 安装
if command -v pip3 >/dev/null 2>&1; then
    echo "方式1: 使用 pip3 安装 docker-compose..."
    pip3 install --user docker-compose
    if command -v docker-compose >/dev/null 2>&1 || [ -f ~/.local/bin/docker-compose ]; then
        if [ -f ~/.local/bin/docker-compose ]; then
            export PATH="$HOME/.local/bin:$PATH"
            echo "✓ docker-compose 安装成功"
            docker-compose version
            echo ""
            echo "注意: 请将 ~/.local/bin 添加到 PATH:"
            echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
            echo "  或添加到 ~/.bashrc:"
            echo "  echo 'export PATH=\"\$HOME/.local/bin:\$PATH\"' >> ~/.bashrc"
            exit 0
        fi
    fi
fi

# 方式2: 下载二进制文件
echo "方式2: 下载 docker-compose 二进制文件..."
DOCKER_COMPOSE_VERSION="1.29.2"
DOCKER_COMPOSE_URL="https://github.com/docker/compose/releases/download/${DOCKER_COMPOSE_VERSION}/docker-compose-Linux-x86_64"

INSTALL_DIR="${HOME}/.local/bin"
mkdir -p "${INSTALL_DIR}"

if curl -L "${DOCKER_COMPOSE_URL}" -o "${INSTALL_DIR}/docker-compose" 2>/dev/null; then
    chmod +x "${INSTALL_DIR}/docker-compose"
    export PATH="${INSTALL_DIR}:$PATH"
    
    if docker-compose version >/dev/null 2>&1; then
        echo "✓ docker-compose 安装成功"
        docker-compose version
        echo ""
        echo "请将以下内容添加到 ~/.bashrc:"
        echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
        exit 0
    fi
fi

# 方式3: 使用 apt 安装（如果可用）
if command -v apt-get >/dev/null 2>&1 && [ "$EUID" -eq 0 ]; then
    echo "方式3: 使用 apt-get 安装 docker-compose-plugin..."
    apt-get update
    apt-get install -y docker-compose-plugin
    if docker compose version >/dev/null 2>&1; then
        echo "✓ docker compose (插件版本) 安装成功"
        docker compose version
        exit 0
    fi
fi

echo "❌ Docker Compose 安装失败"
echo ""
echo "请手动安装 Docker Compose:"
echo "1. 使用 pip: pip3 install --user docker-compose"
echo "2. 下载二进制: https://github.com/docker/compose/releases"
echo "3. 使用 Docker Desktop (推荐)"
exit 1
