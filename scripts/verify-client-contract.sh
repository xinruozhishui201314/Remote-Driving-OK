#!/usr/bin/env bash
# 客户端工程化契约：
# - QML 不得直接 publish vehicle/control；
# - QML 不得调用 mqttController.sendUiEnvelopeJson（已移除；UI 控车唯一入口 AppContext.sendUiCommand → VehicleControlService）；
# - QML 不得调用 mqttController.sendDriveCommand（周期性/立即驾驶指令须走 vehicleControl.sendDriveCommand）。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

fail() {
  echo "[FAIL] $*"
  exit 1
}

echo "========== verify-client-contract: QML 控车契约检查 =========="

if command -v rg >/dev/null 2>&1; then
  if rg -n 'mqttController\.publish\s*\(\s*"vehicle/control"' client/qml -g '*.qml' 2>/dev/null; then
    fail "发现 QML 直接 publish vehicle/control，请改为 AppContext.sendUiCommand 或 vehicleControl.sendUiCommand"
  fi
  if rg -n 'sendUiEnvelopeJson' client/qml -g '*.qml' 2>/dev/null; then
    fail "发现 QML 引用 sendUiEnvelopeJson（已删除）；请统一使用 AppContext.sendUiCommand(type, payload)"
  fi
  if rg -n 'mqttController\.sendDriveCommand' client/qml -g '*.qml' 2>/dev/null; then
    fail "发现 QML 调用 mqttController.sendDriveCommand；请仅使用 vehicleControl.sendDriveCommand"
  fi
  if rg -n 'mqttController\.(sendSteeringCommand|sendThrottleCommand|sendBrakeCommand|sendGearCommand|sendSweepCommand|sendLightCommand|sendSpeedCommand|sendEmergencyStopCommand)\s*\(' client/qml -g '*.qml' 2>/dev/null; then
    fail "发现 QML 经 mqttController 发送逐类控车指令；请统一 vehicleControl.sendUiCommand / sendDriveCommand / requestEmergencyStop"
  fi
  if rg -n 'clientLegacyControlOnly' client/qml -g '*.qml' 2>/dev/null; then
    fail "发现 QML 引用 clientLegacyControlOnly（已移除旁路）；控车须走 VehicleControlService"
  fi
else
  if grep -RIn 'mqttController\.publish\s*(\s*"vehicle/control"' client/qml --include='*.qml' 2>/dev/null; then
    fail "发现 QML 直接 publish vehicle/control"
  fi
  if grep -RIn 'sendUiEnvelopeJson' client/qml --include='*.qml' 2>/dev/null; then
    fail "发现 QML 引用 sendUiEnvelopeJson"
  fi
  if grep -RIn 'mqttController\.sendDriveCommand' client/qml --include='*.qml' 2>/dev/null; then
    fail "发现 QML 调用 mqttController.sendDriveCommand"
  fi
  if grep -RInE 'mqttController\.(sendSteeringCommand|sendThrottleCommand|sendBrakeCommand|sendGearCommand|sendSweepCommand|sendLightCommand|sendSpeedCommand|sendEmergencyStopCommand)\s*\(' client/qml --include='*.qml' 2>/dev/null; then
    fail "发现 QML 经 mqttController 发送逐类控车指令"
  fi
  if grep -RIn 'clientLegacyControlOnly' client/qml --include='*.qml' 2>/dev/null; then
    fail "发现 QML 引用 clientLegacyControlOnly"
  fi
fi

echo "[OK] QML 控车契约：无直发 vehicle/control、无 sendUiEnvelopeJson、无 mqtt sendDriveCommand、无 legacy 旁路标记"
exit 0
