#!/usr/bin/env bash
# 验证 CARLA Bridge 四路相机：先跑单元测试，再（可选）跑集成验证。
# 用法：
#   ./scripts/verify-carla-cameras.sh           # 仅单元测试
#   ./scripts/verify-carla-cameras.sh --integrate  # 单元测试 + 集成（需 CARLA 已启动）

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BRIDGE_DIR="${PROJECT_ROOT}/carla-bridge"

cd "${BRIDGE_DIR}"

echo "=== 1. 四路相机单元测试 ==="
python3 -m unittest tests.test_cameras -v
echo ""

if [ "$1" = "--integrate" ]; then
  echo "=== 2. 四路相机集成验证（需 CARLA 已启动）==="
  python3 verify_cameras.py
  echo "集成验证通过"
else
  echo "跳过集成验证。若 CARLA 已启动，可运行: ./scripts/verify-carla-cameras.sh --integrate"
fi
