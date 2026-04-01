#!/bin/bash
# M0 阶段：基础设施部署脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPLOY_DIR="${PROJECT_ROOT}/deploy"

echo "=========================================="
echo "远程驾驶系统 - M0 阶段部署脚本"
echo "=========================================="

# 检查 Docker 和 Docker Compose
if ! command -v docker &> /dev/null; then
    echo "错误: 未找到 Docker，请先安装 Docker"
    exit 1
fi

if ! command -v docker-compose &> /dev/null && ! docker compose version &> /dev/null; then
    echo "错误: 未找到 Docker Compose，请先安装 Docker Compose"
    exit 1
fi

# 检查 .env 文件
if [ ! -f "${DEPLOY_DIR}/.env" ]; then
    echo "警告: 未找到 .env 文件，从 .env.example 创建..."
    cp "${DEPLOY_DIR}/.env.example" "${DEPLOY_DIR}/.env"
    echo "请编辑 ${DEPLOY_DIR}/.env 文件，修改默认密码和配置"
    read -p "按 Enter 继续（将使用默认配置）..."
fi

# 进入部署目录
cd "${DEPLOY_DIR}"

echo ""
echo "1. 启动 Docker Compose 服务..."
docker-compose up -d

echo ""
echo "2. 等待服务就绪..."
sleep 10

echo ""
echo "3. 检查服务状态..."
docker-compose ps

echo ""
echo "4. 导入 Keycloak Realm..."
if [ -f "${DEPLOY_DIR}/keycloak/import-realm.sh" ]; then
    cd "${DEPLOY_DIR}/keycloak"
    bash import-realm.sh || echo "警告: Keycloak Realm 导入失败，请手动导入"
    cd "${DEPLOY_DIR}"
else
    echo "警告: 未找到 Keycloak 导入脚本"
fi

echo ""
echo "=========================================="
echo "部署完成！"
echo "=========================================="
echo ""
echo "服务访问地址:"
echo "  - Keycloak Admin: http://localhost:8080/admin"
echo "  - ZLMediaKit API: http://localhost/index/api/getServerConfig"
echo ""
echo "查看日志: docker-compose logs -f"
echo "停止服务: docker-compose down"
echo "=========================================="
