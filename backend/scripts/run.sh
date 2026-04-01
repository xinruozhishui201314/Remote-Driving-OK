#!/bin/bash
# Backend 模块独立运行脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
EXECUTABLE="$BUILD_DIR/teleop_backend"

echo "========================================"
echo "Starting Backend Module"
echo "========================================"

# 检查可执行文件
if [ ! -f "$EXECUTABLE" ]; then
    echo "错误: 未找到可执行文件 $EXECUTABLE"
    echo "请先运行: ./scripts/build.sh"
    exit 1
fi

# 设置环境变量（从配置文件或默认值）
export DATABASE_URL="${DATABASE_URL:-postgresql://postgres:postgres@localhost:5432/teleop}"
export KEYCLOAK_URL="${KEYCLOAK_URL:-http://keycloak:8080}"
export KEYCLOAK_ISSUER="${KEYCLOAK_ISSUER:-http://keycloak:8080/realms/teleop}"
export ZLM_API_URL="${ZLM_API_URL:-http://zlmediakit/index/api}"
# ZLM_PUBLIC_BASE：客户端可达的 ZLM 地址；Docker 网络内默认 zlmediakit:80，宿主机部署时覆盖为实际 IP
export ZLM_PUBLIC_BASE="${ZLM_PUBLIC_BASE:-http://zlmediakit:80}"
export MQTT_BROKER_URL="${MQTT_BROKER_URL:-mqtt://mosquitto:1883}"

# 日志配置
export RUST_LOG="${RUST_LOG:-info}"

echo ""
echo "配置信息:"
echo "  DATABASE_URL: $DATABASE_URL"
echo "  KEYCLOAK_URL: $KEYCLOAK_URL"
echo "  KEYCLOAK_ISSUER: $KEYCLOAK_ISSUER"
echo "  ZLM_API_URL: $ZLM_API_URL"
echo "  ZLM_PUBLIC_BASE: $ZLM_PUBLIC_BASE"
echo "  MQTT_BROKER_URL: $MQTT_BROKER_URL"
echo ""

# 运行程序
cd "$BUILD_DIR"
exec ./teleop_backend
