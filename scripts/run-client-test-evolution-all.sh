#!/usr/bin/env bash
# MVP → V2 串联：单元测试 +（可选）覆盖率门禁 +（可选）变异探测。
#
#   ./scripts/run-client-test-evolution-all.sh
#
# 环境变量：
#   RUN_COVERAGE=1              运行 run-client-coverage-with-thresholds.sh
#   CLIENT_COVERAGE_ENFORCE=1   与覆盖率脚本一致，启用阈值
#   RUN_MUTATION=1              若本机有 mull-runner 则尝试（通常需 MULL_BUILD_DIR）
#   RUN_MQTT_FIXTURE=1         需已构建 test_mqtt_paho_broker_smoke + docker，见 run-client-mqtt-integration-fixture.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== L1/L2: CTest 全量 ==="
"$SCRIPT_DIR/run-client-unit-tests-oneclick.sh"

if [[ "${RUN_COVERAGE:-0}" == "1" ]]; then
  echo "=== MVP: 覆盖率报告 / 门禁 ==="
  "$SCRIPT_DIR/run-client-coverage-with-thresholds.sh"
fi

if [[ "${RUN_MUTATION:-0}" == "1" ]]; then
  echo "=== V1/V2: 变异测试（可选）==="
  "$SCRIPT_DIR/run-client-mutation-sample.sh" || true
fi

if [[ "${RUN_MQTT_FIXTURE:-0}" == "1" ]]; then
  echo "=== V1: MQTT broker 夹具 + Paho 烟测 ==="
  "$SCRIPT_DIR/run-client-mqtt-integration-fixture.sh"
fi

echo "=== 完成 ==="
