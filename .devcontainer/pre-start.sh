#!/bin/bash
# Dev Container 预启动脚本（在主机上运行）
# 在容器启动前执行必要的设置

# set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=========================================="
echo "Pre-starting Dev Container"
echo "=========================================="

# 1. 设置 X11 权限
echo "Step 1: Setting up X11 permissions..."
# 2. 【修改】加上 || true，表示即使这行失败了，也视为成功，继续往下走
bash "$SCRIPT_DIR/setup-x11.sh" || echo "⚠ Warning: Failed to set X11 permissions, but continuing..."

# 2. 检查 Docker 服务
echo ""
echo "Step 2: Checking Docker service..."
if ! docker info > /dev/null 2>&1; then
    echo "✗ Docker is not accessible"
    echo "  Attempting to start Docker service..."
    
    # 尝试多种方式启动 Docker
    DOCKER_STARTED=false
    
    # 方式1: 使用 systemctl（需要 systemd 且权限）
    if command -v systemctl > /dev/null 2>&1; then
        if sudo systemctl start docker 2>/dev/null; then
            sleep 2
            if docker info > /dev/null 2>&1; then
                echo "✓ Docker started via systemctl"
                DOCKER_STARTED=true
            fi
        fi
    fi
    
    # 方式2: 使用 service 命令（SysV init）
    if [ "$DOCKER_STARTED" = false ] && command -v service > /dev/null 2>&1; then
        if sudo service docker start 2>/dev/null; then
            sleep 2
            if docker info > /dev/null 2>&1; then
                echo "✓ Docker started via service"
                DOCKER_STARTED=true
            fi
        fi
    fi
    
    # 方式3: 直接运行 dockerd（如果可用）
    if [ "$DOCKER_STARTED" = false ] && command -v dockerd > /dev/null 2>&1; then
        echo "  Note: dockerd found, but manual start not recommended"
        echo "  Please start Docker manually or check Docker Desktop"
    fi
    
    # 如果仍然无法访问 Docker
    if [ "$DOCKER_STARTED" = false ]; then
        echo ""
        echo "⚠ Warning: Could not start Docker automatically"
        echo "  Possible reasons:"
        echo "    1. Docker service is managed by Docker Desktop or another tool"
        echo "    2. Insufficient permissions (need sudo or docker group membership)"
        echo "    3. Docker daemon is not installed"
        echo ""
        echo "  Solutions:"
        echo "    - If using Docker Desktop: Ensure it's running"
        echo "    - If using systemd: Run manually: sudo systemctl start docker"
        echo "    - If using service: Run manually: sudo service docker start"
        echo "    - Check Docker status: docker info"
        echo ""
        echo "  Continuing anyway... (Dev Container may handle Docker internally)"
    fi
else
    echo "✓ Docker is accessible"
fi

# 3. 检查镜像是否存在
echo ""
echo "Step 3: Checking Docker image..."
if docker info > /dev/null 2>&1; then
    if docker images 2>/dev/null | grep -q "docker.1ms.run/stateoftheartio/qt6.*6.8-gcc-aqt"; then
        echo "✓ Docker image found"
    else
        echo "⚠ Docker image not found"
        echo "  Image: docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt"
        echo "  You may need to pull it manually"
    fi
else
    echo "⚠ Skipping image check (Docker not accessible)"
fi

# 4. 检查网络配置（如果 Docker 可访问）
echo ""
echo "Step 4: Checking network configuration..."
if docker info > /dev/null 2>&1; then
    # 检查 devcontainer.json 中的网络配置
    if [ -f "$SCRIPT_DIR/devcontainer.json" ]; then
        if grep -q '"--network=host"' "$SCRIPT_DIR/devcontainer.json" || grep -q '"network": "host"' "$SCRIPT_DIR/devcontainer.json"; then
            echo "✓ devcontainer.json configured with --network=host"
            echo "  Container will use host network (shared with host)"
        else
            echo "⚠ devcontainer.json may not use host network mode"
            echo "  For remote driving, --network=host is recommended"
        fi
    fi
else
    echo "⚠ Skipping network check (Docker not accessible)"
fi

# 5. 确保工作目录权限
echo ""
echo "Step 5: Checking workspace permissions..."
if [ -w "$PROJECT_ROOT" ]; then
    echo "✓ Workspace is writable"
else
    echo "⚠ Workspace may have permission issues"
fi

echo ""
echo "=========================================="
echo "Pre-start setup completed!"
echo "=========================================="
echo ""
echo "You can now:"
echo "1. Open Cursor/VSCode"
echo "2. Press F1 → 'Dev Containers: Reopen in Container'"
echo "3. Wait for container to start"
echo ""
