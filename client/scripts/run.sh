#!/bin/bash
# Client 模块独立运行脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
EXECUTABLE="$BUILD_DIR/client"

echo "========================================"
echo "Starting Client Module"
echo "========================================"

# 检查可执行文件
if [ ! -f "$EXECUTABLE" ]; then
    echo "错误: 未找到可执行文件 $EXECUTABLE"
    echo "请先运行: ./scripts/build.sh"
    exit 1
fi

# 设置环境变量（从配置文件或默认值）
export BACKEND_URL="${BACKEND_URL:-http://localhost:8000}"
export KEYCLOAK_URL="${KEYCLOAK_URL:-http://localhost:8080}"
export MQTT_BROKER_URL="${MQTT_BROKER_URL:-mqtt://localhost:1883}"
export ZLM_WHEP_URL="${ZLM_WHEP_URL:-http://localhost:80/index/api/webrtc}"

# Qt 平台插件设置
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"

# 日志配置
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-*.debug=false;qt.qml.debug=false}"

# QML 导入路径
export QML_IMPORT_PATH="$PROJECT_DIR/qml:$PROJECT_DIR/build/qml"

echo ""
echo "配置信息:"
echo "  BACKEND_URL: $BACKEND_URL"
echo "  KEYCLOAK_URL: $KEYCLOAK_URL"
echo "  MQTT_BROKER_URL: $MQTT_BROKER_URL"
echo "  ZLM_WHEP_URL: $ZLM_WHEP_URL"
echo "  QT_QPA_PLATFORM: $QT_QPA_PLATFORM"
echo "  QML_IMPORT_PATH: $QML_IMPORT_PATH"
echo ""

# 运行程序
cd "$BUILD_DIR"
exec ./client
