#!/bin/bash
# Vehicle-side 模块独立运行脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
EXECUTABLE="$BUILD_DIR/VehicleSide"

echo "========================================"
echo "Starting Vehicle-side Module"
echo "========================================"

# 检查可执行文件
if [ ! -f "$EXECUTABLE" ]; then
    echo "错误: 未找到可执行文件 $EXECUTABLE"
    echo "请先运行: ./scripts/build.sh"
    exit 1
fi

# 设置环境变量（从配置文件或默认值）
export MQTT_BROKER_URL="${MQTT_BROKER_URL:-mqtt://mosquitto:1883}"
export ZLM_RTMP_URL="${ZLM_RTMP_URL:-rtmp://zlmediakit:1935}"
export ZLM_WHIP_URL="${ZLM_WHIP_URL:-http://zlmediakit/index/api/whip}"
export VIN="${VIN:-TEST_VEHICLE_001}"
export VEHICLE_CONTROL_SECRET="${VEHICLE_CONTROL_SECRET:-change_me_in_production}"
export LOG_LEVEL="${LOG_LEVEL:-info}"

# 推流脚本路径（可选）
export VEHICLE_PUSH_SCRIPT="${VEHICLE_PUSH_SCRIPT:-/app/scripts/push-testpattern-to-zlm.sh}"
export SWEEPS_PATH="${SWEEPS_PATH:-/data/sweeps}"

# 看门狗配置
export WATCHDOG_TIMEOUT="${WATCHDOG_TIMEOUT:-5}"  # 秒
export SAFE_STOP_ENABLED="${SAFE_STOP_ENABLED:-true}"

# 数据库连接（用于健康检查）
export DATABASE_URL="${DATABASE_URL:-postgresql://postgres:postgres@postgres:5432/teleop}"

# 日志配置
export SPDLOG_LEVEL="${SPDLOG_LEVEL:-info}"

echo ""
echo "配置信息:"
echo "  MQTT_BROKER_URL: $MQTT_BROKER_URL"
echo "  ZLM_RTMP_URL: $ZLM_RTMP_URL"
echo "  ZLM_WHIP_URL: $ZLM_WHIP_URL"
echo "  VIN: $VIN"
echo "  LOG_LEVEL: $LOG_LEVEL"
echo "  WATCHDOG_TIMEOUT: ${WATCHDOG_TIMEOUT}s"
echo "  SAFE_STOP_ENABLED: $SAFE_STOP_ENABLED"
echo "  VEHICLE_PUSH_SCRIPT: $VEHICLE_PUSH_SCRIPT"
echo ""

# 运行程序
cd "$BUILD_DIR"
exec ./VehicleSide
