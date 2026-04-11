#!/usr/bin/env bash
# V2：QML 录屏/黄金图资产存在性与清单校验（不强制安装图像对比工具）。
# 用法（仓库根目录）：./scripts/verify-qml-recorded-regression.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REG_DIR="$REPO_ROOT/client/qml/regression"
SCENARIOS="$REG_DIR/scenarios.md"
README="$REG_DIR/README.md"

if [[ ! -f "$README" ]]; then
  echo "[qml-regression][FAIL] 缺少 $README" >&2
  exit 1
fi

if [[ ! -f "$SCENARIOS" ]]; then
  echo "[qml-regression][WARN] 尚未添加 $SCENARIOS（见 README 资产约定；可在 client/qml/regression/ 手工创建）。"
fi

echo "[qml-regression][OK] 基线检查通过；详细流程见 client/qml/regression/README.md"
echo "  后续可在此脚本中加入 imagemagick/compare 或容器内 client 截图命令。"
