#!/bin/bash
# 全局部署脚本：构建所有 Docker 镜像

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================"
echo "Deploying All Modules (Docker)"
echo "========================================"
echo "Project directory: $PROJECT_DIR"
echo ""

# 设置镜像标签
IMAGE_TAG="${IMAGE_TAG:-latest}"
echo "Image tag: $IMAGE_TAG"
echo ""

# 构建 backend 镜像
echo ""
echo "========================================"
echo "[1/3] Building Backend Docker image"
echo "========================================"
cd "$PROJECT_DIR/backend"
if [ -f "Dockerfile" ]; then
    docker build -t teleop-backend:${IMAGE_TAG} .
    echo "✓ Backend image built: teleop-backend:${IMAGE_TAG}"
else
    echo "错误: 未找到 backend/Dockerfile"
    exit 1
fi

# 构建 client 镜像
echo ""
echo "========================================"
echo "[2/3] Building Client Docker image"
echo "========================================"
cd "$PROJECT_DIR/client"
if [ -f "Dockerfile.prod" ]; then
    docker build -f Dockerfile.prod -t teleop-client:${IMAGE_TAG} .
    echo "✓ Client image built: teleop-client:${IMAGE_TAG}"
else
    echo "错误: 未找到 client/Dockerfile.prod"
    exit 1
fi

# 构建 Vehicle-side 镜像
echo ""
echo "========================================"
echo "[3/3] Building Vehicle-side Docker image"
echo "========================================"
cd "$PROJECT_DIR/Vehicle-side"
if [ -f "Dockerfile.prod" ]; then
    docker build -f Dockerfile.prod -t teleop-vehicle:${IMAGE_TAG} .
    echo "✓ Vehicle-side image built: teleop-vehicle:${IMAGE_TAG}"
else
    echo "错误: 未找到 Vehicle-side/Dockerfile.prod"
    exit 1
fi

echo ""
echo "========================================"
echo "All Docker images built successfully!"
echo "========================================"
echo ""
echo "镜像列表:"
echo "  - teleop-backend:${IMAGE_TAG}"
echo "  - teleop-client:${IMAGE_TAG}"
echo "  - teleop-vehicle:${IMAGE_TAG}"
echo ""
echo "启动完整链路:"
echo "  cd $PROJECT_DIR && docker compose up -d"
echo ""
echo "查看镜像:"
echo "  docker images | grep teleop"
echo ""
