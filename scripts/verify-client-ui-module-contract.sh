#!/usr/bin/env bash
# DrivingFacade / CLIENT_UI_MODULE_CONTRACT.md 静态门禁（无 GUI）
# - 契约文档存在且声明 DrivingFacade v3
# - DrivingInterface.qml 含 teleop 窄入口与 driving/* 布局壳
# - driving/qmldir 五件套 + internal/qmldir 登记；internal 禁 RemoteDriving/AppContext
# - 五子模块均含 required property Item facade
# - 仅 DrivingInterface.qml 可 import components/driving/internal
# - driving/*（含 internal）QML 不得使用 rd_* 平行注入
# - 禁止 canonical 链引用遗留 DrivingInterface.{Controls,VideoPanels,Dashboard}
#
# 用法（仓库根）：
#   ./scripts/verify-client-ui-module-contract.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

fail() { echo -e "${RED}[FAIL] $*${NC}" >&2; exit 1; }

echo -e "${CYAN}>>> verify-client-ui-module-contract（DrivingFacade + 契约门禁）${NC}"

CONTRACT="docs/CLIENT_UI_MODULE_CONTRACT.md"
[[ -f "$CONTRACT" ]] || fail "缺失 $CONTRACT"

grep -q 'DrivingFacade v3' "$CONTRACT" \
  || fail "$CONTRACT 应声明 DrivingFacade v3（契约版本表）"

grep -q 'facade\.teleop' "$CONTRACT" \
  || fail "$CONTRACT 应描述 facade.teleop（§3.5）"

DI="client/qml/DrivingInterface.qml"
[[ -f "$DI" ]] || fail "缺失 $DI"

grep -q 'id: teleopBinder' "$DI" || fail "$DI 应含 teleopBinder（DrivingFacade v3）"
grep -q 'readonly property var teleop: teleopBinder' "$DI" \
  || fail "$DI 应暴露 readonly property var teleop: teleopBinder"
grep -q 'Drv\.DrivingLayoutShell' "$DI" || fail "$DI 应实例化 Drv.DrivingLayoutShell"
grep -q 'import "components/driving" as Drv' "$DI" \
  || fail "$DI 应 import components/driving as Drv"
grep -q 'import "components/driving/internal" as DiInt' "$DI" \
  || fail "$DI 应 import components/driving/internal（§2.1 internal 组合）"
grep -q 'id: appServicesBridge' "$DI" || fail "$DI 应含 appServicesBridge（DrivingFacade v3 appServices）"
grep -q 'readonly property var appServices: appServicesBridge' "$DI" \
  || fail "$DI 应暴露 readonly property var appServices: appServicesBridge"

DF_QMDIR="client/qml/DrivingFacade/qmldir"
[[ -f "$DF_QMDIR" ]] || fail "缺失 $DF_QMDIR（DrivingFacade 独立 URI，见契约 §1.3）"
grep -qE '^DrivingInterface[[:space:]]+1\.0[[:space:]]+\.\./DrivingInterface\.qml' "$DF_QMDIR" \
  || fail "$DF_QMDIR 应将 DrivingInterface 指向 ../DrivingInterface.qml"
grep -q '^typeinfo driving-facade.qmltypes' "$DF_QMDIR" \
  || fail "$DF_QMDIR 应含 typeinfo driving-facade.qmltypes"

DSH="client/qml/shell/DrivingStageHost.qml"
grep -qE '^import[[:space:]]+DrivingFacade[[:space:]]+1\.0' "$DSH" \
  || fail "$DSH 应 import DrivingFacade 1.0（显式 facade 模块）"

# 禁止 canonical 主界面再挂遗留切片（双轨）
if grep -qE 'DrivingInterface\.(Controls|VideoPanels|Dashboard)|CustomComponents\.DrivingInterface' "$DI"; then
  fail "$DI 不得引用遗留 DrivingInterface.* 切片；canonical 为 components/driving/*（见 CLIENT_UI_MODULE_CONTRACT §5）"
fi

QMDIR="client/qml/components/driving/qmldir"
[[ -f "$QMDIR" ]] || fail "缺失 $QMDIR"
for name in DrivingLayoutShell DrivingTopChrome DrivingLeftRail DrivingCenterColumn DrivingRightRail; do
  grep -q "^${name} " "$QMDIR" || fail "$QMDIR 应登记 ${name}"
done

INT_QMDIR="client/qml/components/driving/internal/qmldir"
[[ -f "$INT_QMDIR" ]] || fail "缺失 $INT_QMDIR"
for name in LayoutMetrics DrivingLayoutDiagnostics TeleopPresentationState TeleopKeyboardHandler DrivingVideoDiagnostics; do
  grep -q "^${name} " "$INT_QMDIR" || fail "$INT_QMDIR 应登记 ${name}"
done

# §2 五件套：required property Item facade（契约正式子模块）
DRIVING_DIR="$PROJECT_ROOT/client/qml/components/driving"
for f in DrivingLayoutShell.qml DrivingTopChrome.qml DrivingLeftRail.qml DrivingCenterColumn.qml DrivingRightRail.qml; do
  p="$DRIVING_DIR/$f"
  [[ -f "$p" ]] || fail "缺失 $p"
  grep -q 'required property Item facade' "$p" || fail "$f 须含 required property Item facade（§1.1 唯一正式通道）"
  if grep -qE 'AppContext\.' "$p" 2>/dev/null; then
    fail "$f 不得使用 AppContext.；统一 facade.appServices（CLIENT_UI_MODULE_CONTRACT §3.6）"
  fi
done

# §1.2：仅 DrivingInterface 可 import internal
while IFS= read -r -d '' qml; do
  if grep -q 'components/driving/internal' "$qml"; then
    case "$qml" in
      */DrivingInterface.qml) ;;
      *)
        rel="${qml#"$PROJECT_ROOT"/}"
        fail "禁止在 ${rel} import driving/internal；仅 client/qml/DrivingInterface.qml 允许（CLIENT_UI_MODULE_CONTRACT §1.2）"
        ;;
    esac
  fi
done < <(find "$PROJECT_ROOT/client/qml" -name '*.qml' -print0 2>/dev/null)

# §1.2：driving/* 含 internal 不得使用 rd_* 平行注入
if grep -r 'rd_' "$DRIVING_DIR" --include='*.qml' -q 2>/dev/null; then
  fail "client/qml/components/driving/（含 internal）不得使用 rd_* 根注入；统一 AppContext（CLIENT_UI_MODULE_CONTRACT §1.2）"
fi

# DrivingFacade v3：internal 禁止直连单例 AppContext / import RemoteDriving
INT_DIR="$DRIVING_DIR/internal"
if grep -rE 'import RemoteDriving|AppContext\.' "$INT_DIR" --include='*.qml' -q 2>/dev/null; then
  fail "internal/*.qml 禁止 import RemoteDriving 与 AppContext.；服务经 DrivingInterface.appServices 注入（CLIENT_UI_MODULE_CONTRACT §1.2 / §3.6）"
fi

# Teleop 状态与键盘须显式接收 facade（注入 appServices）
grep -q 'property Item facade' "$INT_DIR/TeleopPresentationState.qml" \
  || fail "TeleopPresentationState.qml 须含 property Item facade"
grep -q 'property Item facade' "$INT_DIR/TeleopKeyboardHandler.qml" \
  || fail "TeleopKeyboardHandler.qml 须含 property Item facade"

# internal/*.qml：根为 QtObject 且含 Connections → 运行期 “non-existent default property” / exit 93
# 仅看「首个非 import/块注释后的根类型」，避免 Item { QtObject { … } … } 误报
qml_root_type_after_imports() {
  awk '
    BEGIN { skip = 0 }
    /^\/\*/ { skip = 1 }
    skip {
      if (/\*\//) skip = 0
      next
    }
    /^import[[:space:]]/ { next }
    /^[[:space:]]*\/\// { next }
    /^[[:space:]]*$/ { next }
    /^[[:space:]]*QtObject[[:space:]]*\{/ { print "QtObject"; exit }
    /^[[:space:]]*Item[[:space:]]*\{/ { print "Item"; exit }
    /^[[:space:]]*[A-Za-z_][A-Za-z0-9_.]*[[:space:]]*\{/ { print "other"; exit }
    { exit }
  ' "$1"
}
while IFS= read -r -d '' f; do
  grep -q 'Connections[[:space:]]*{' "$f" || continue
  rt="$(qml_root_type_after_imports "$f")"
  if [[ "$rt" == "QtObject" ]]; then
    rel="${f#"$PROJECT_ROOT"/}"
    fail "${rel} 根为 QtObject 且含 Connections；请根用 Item（零尺寸+visible:false）或上移 Connections（CLIENT_UI_MODULE_CONTRACT §1.2）"
  fi
done < <(find "$INT_DIR" -maxdepth 1 -name '*.qml' -print0 2>/dev/null)

echo -e "${GREEN}[OK] verify-client-ui-module-contract 通过${NC}"
