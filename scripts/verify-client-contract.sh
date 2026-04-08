#!/usr/bin/env bash
# 客户端工程化契约：QML 不得直接 publish vehicle/control；控制须走 VehicleControlService 或 LEGACY 的 sendUiEnvelopeJson。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

echo "========== verify-client-contract: QML MQTT 直发控制 topic 检查 =========="
if command -v rg >/dev/null 2>&1; then
  if rg -n 'mqttController\.publish\s*\(\s*"vehicle/control"' client/qml -g '*.qml' 2>/dev/null; then
    echo "[FAIL] 发现 QML 直接 publish vehicle/control，请改为 vehicleControl.sendUiCommand 或 LEGACY 下 sendUiEnvelopeJson"
    exit 1
  fi
else
  if grep -RIn 'mqttController\.publish\s*(\s*"vehicle/control"' client/qml --include='*.qml' 2>/dev/null; then
    echo "[FAIL] 发现 QML 直接 publish vehicle/control"
    exit 1
  fi
fi
echo "[OK] QML 无 mqttController.publish(\"vehicle/control\", …)"
exit 0
