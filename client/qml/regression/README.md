# QML 录屏与视觉回归资产（V2）

## 目的

补充 C++ 单测无法覆盖的 **绑定、布局、动效与多分辨率** 行为；资产应 **可版本化、可 diff、可 CI 挂载**。

## 资产约定（建议）

| 类型 | 路径示例 | 说明 |
|------|----------|------|
| 黄金截图 | `client/qml/regression/golden/<feature>/<theme>/<w>x<h>.png` | 固定 `QT_QPA_PLATFORM=offscreen` 或指定软件栈下导出。 |
| 录屏 | `client/qml/regression/recordings/<scenario>.webm` | 短片段（&lt;30s），附 `manifest.json`（commit、Qt 版本、DPR）。 |
| 场景清单 | `client/qml/regression/scenarios.md` | 与 `docs/CLIENT_UI_FEATURE_COVERAGE_MATRIX.md` 交叉引用。 |

## 工具选型（开源/业界）

- **Squish for Qt**、**Qt Test + grabWindow**（轻量截图）、**自研 offscreen FBO 对比**（与 `scripts/verify-client-headless-lifecycle.sh` 同哲学）。
- 对比算法：`perceptualdiff`、`magick compare -metric AE`；阈值写入场景 manifest。

## 与现有脚本的关系

- 入口编排：`scripts/verify-qml-recorded-regression.sh`（检查资产存在性 + 可选对比命令占位）。
- L4 串联：`scripts/verify-client-ui-and-video-coverage.sh`、`verify-driving-layout.sh`。

## 维护

新增驾驶相关 QML 面时：至少增加一条 **scenario** 文档条目；合并录屏/截图前在 PR 描述注明 **Qt 版本与 DPI**。
