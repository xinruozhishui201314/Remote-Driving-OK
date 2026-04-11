# Qt UI 客户端与「Google 级工程标准」落地说明

| 字段 | 值 |
|------|-----|
| 文档版本 | 1.4 |
| 关联 | [CLIENT_UI_MODULE_CONTRACT.md](CLIENT_UI_MODULE_CONTRACT.md)、[CLIENT_GOOGLE_ENGINEERING_ALIGNMENT.md](CLIENT_GOOGLE_ENGINEERING_ALIGNMENT.md)、[CLIENT_UI_QUALITY_CHAIN.md](CLIENT_UI_QUALITY_CHAIN.md)（四链路总览） |

## 1. 结论先行

**Google 未发布 QML / Qt Quick 官方风格指南**，因此不能声称「逐字符合 Google QML 规范」。本仓库将 **Google C++ Style Guide 中的工程原则**（显式依赖、可静态验证、模块边界清晰、避免隐式全局）**转写为 Qt UI 可执行规则**：

| 层级 | 要求 | 门禁 |
|------|------|------|
| QML 模块与 Facade | DrivingFacade v3、canonical `components/driving/*`、禁止违规 `AppContext` 穿透 | `./scripts/verify-client-ui-module-contract.sh` |
| QML 语法与分析 | 全量 `qmllint` + 仓库内 [`client/qml/.qmllint.ini`](../client/qml/.qmllint.ini) | `./scripts/verify-client-qt-ui-google-style.sh` |
| **QML 相关 C++（严格）** | 凡经 **`QML_ELEMENT` + `qt_add_qml_module`（URI `RemoteDriving`）** 或 `QQmlContext::setContextProperty` 暴露给 QML 的类型，以及应用内 QML 引擎引导代码，**一律**须符合 [`client/.clang-format`](../client/.clang-format)（Google 基线） | `./scripts/verify-client-qt-ui-google-style.sh` 内 **`clang-format-18 --dry-run -Werror`**；**路径全集**由 [`scripts/lib/collect-qml-related-cpp.sh`](../scripts/lib/collect-qml-related-cpp.sh) 维护 |

**一体化入口**：`./scripts/verify-client-qt-ui-google-style.sh`（含上述契约 + qmllint + format）。

---

## 2. 与 Google 原则的对应关系（非字面 cpplint）

| Google 工程习惯（抽象） | Qt UI 落地 |
|-------------------------|------------|
| 依赖显式、可检索 | `qmllint` + `unused-imports`；契约脚本 grep 禁止模式 |
| 接口稳定、破坏性变更有版本语义 | [CLIENT_UI_MODULE_CONTRACT.md](CLIENT_UI_MODULE_CONTRACT.md) §6 |
| 避免「魔法全局」 | driving 子组件仅 `facade` / `facade.appServices`；`internal/` 禁 `AppContext` |
| 静态分析门禁 | `qmllint`；C++ `clang-tidy` 基线见 [CLIENT_GOOGLE_ENGINEERING_ALIGNMENT.md §6](CLIENT_GOOGLE_ENGINEERING_ALIGNMENT.md) |

### 2.1 维护 `collect-qml-related-cpp.sh`（必做）

在以下情况 **必须** 更新 [`scripts/lib/collect-qml-related-cpp.sh`](../scripts/lib/collect-qml-related-cpp.sh)，否则门禁会漏检或误报：

- 新增 **`QML_ELEMENT` 类型**（并确保列入 `client/CMakeLists.txt` 中可执行目标的源集）或 `setContextProperty` 所指向的 **新 C++ 类型**；**并运行** [`scripts/regenerate-client-qmltypes.sh`](../scripts/regenerate-client-qmltypes.sh) 更新 `remote-driving-cpp.qmltypes` 与 `DrivingFacade/driving-facade.qmltypes` 后提交。
- 将某服务从「仅 C++ 内部」改为 **QML 可见**（或相反：从清单中移除时需确认无 QML 引用）。

全仓 `clang-format` 仍可用 [`scripts/format-client-cpp.sh`](../scripts/format-client-cpp.sh)；本清单用于 **QML 边界上的强制一致性与 PR 审查范围**。

---

## 3. `qmllint`、模块类型与 `.qmllint.ini` 说明

- 配置文件路径：**`client/qml/.qmllint.ini`**（与 `qmldir` 同树，`qmllint` 自动拾取）。
- **退出码（硬门禁）**：Qt 6 默认 `MaxWarnings=-1` 时，**仅有 Warning 也可能返回 0**。本仓库在 **`.qmllint.ini` 中设 `MaxWarnings=0`**，且 **`./scripts/verify-client-qt-ui-google-style.sh` 对所有文件传 `-W 0`**，保证 Warning 级问题导致脚本失败。
- **C++ 注册类型（类型系统）**：可执行目标上 **`qt_add_qml_module(... URI RemoteDriving ... NO_GENERATE_QMLDIR OUTPUT_DIRECTORY client/qml TYPEINFO remote-driving-cpp.qmltypes)`** 驱动 **`qmltyperegistrar`** 生成 **`client/qml/remote-driving-cpp.qmltypes`**（勿手写）；`qmldir` 中 **`typeinfo remote-driving-cpp.qmltypes`** 与之对应。运行期类型由 **`QML_ELEMENT`** 与链接单元内生成的 `qml_register_types_RemoteDriving()` 注册（**不再** `qmlRegisterType`）。`qmldir` 顶行 **`import QtQuick auto` / `import QtQml auto`** 满足依赖声明。
- **Facade 独立 URI（`DrivingFacade`）**：静态库目标 **`DrivingFacadeQml`** + **`qt_add_qml_module(... URI DrivingFacade ... TYPEINFO driving-facade.qmltypes)`**；**`OUTPUT_DIRECTORY` 在 CMake 构建目录**，**`POST_BUILD`** 将 **`driving-facade.qmltypes`** 同步到 **`client/qml/DrivingFacade/`**（避免在源码树落 `.a` / 资源副本）。源码树中 **[`client/qml/DrivingFacade/qmldir`](../client/qml/DrivingFacade/qmldir)** 将 **`DrivingInterface`** 指向 **`../DrivingInterface.qml`**。变更 C++ QML API 或影响类型导出的 QML 后执行 **`./scripts/regenerate-client-qmltypes.sh`** 并提交两份 `*.qmltypes`。
- **生成物与 Git 一致**：具备 Qt/CMake 或 Docker 时运行 **`./scripts/verify-qmltypes-match-generated.sh`**；**`./scripts/verify-qml-changes.sh`** 在编译成功后亦会检查上述两文件是否漂移。可选：`VERIFY_QMLTYPES_DRIFT=1 ./scripts/verify-client-ui-quality-chain.sh` 或 `./scripts/verify-client-qt-ui-google-style.sh` 串联该检查。
- **仅上下文属性模型**：`TelemetryModel` / `NetworkStatusModel` / `SafetyStatusModel` 等 **仅** `setContextProperty` 注入、**不**加 `QML_ELEMENT`，避免进入同一 URI 的自动注册与 `qmltyperegistrations.cpp` 依赖链爆炸。
- **Quick 插件**：verify 脚本对每文件附加 **`-D Quick`**，与 `LintPluginWarnings=disable` 并存时仍可能出现的 **layout-positioning** 类告警解耦；布局最佳实践可后续专项打开 Quick 插件再修。
- **规则分级（当前）**：**`ImportFailure` / `UnresolvedType` / `UnusedImports` / `IncompatibleType` 等为 `warning`**；**`UnqualifiedAccess` 与 `MissingProperty` 暂为 `info`**（前者体量大；后者在 `contentItem` delegate、`required property Item facade` 实际注入 DrivingFacade 等场景易产生误报）。契约仍要求五件套 **`required property Item facade`**（见 `verify-client-ui-module-contract.sh`），与 `MissingProperty=info` 组合可在不削弱 Facade 字面契约的前提下通过 `qmllint -W 0`。

---

## 4. 编译 / 运行说明

```bash
# 修改带 QML_ELEMENT 的 C++ 类型后：重建并提交 remote-driving-cpp.qmltypes
./scripts/regenerate-remote-driving-qmltypes.sh

# Qt UI + QML 门禁（推荐每次改 QML / UI C++ 后执行）
./scripts/verify-client-qt-ui-google-style.sh

# 无 clang-format-18 时可仅跳过 format 检查（不推荐长期）
SKIP_CLANG_FORMAT=1 ./scripts/verify-client-qt-ui-google-style.sh
```

**环境**：宿主机需 **Qt 6 `qmllint` 在 PATH**，或 **Docker 镜像 `remote-driving-client-dev:full`**（脚本内用 `/opt/Qt/6.8.0/gcc_64/bin/qmllint`）。

---

## 5. 验证与串联

- 已接入总串联脚本：`./scripts/verify-client-ui-and-video-coverage.sh`（在 UI 契约步骤之后增加本门禁）。

---

## 6. 风险与回滚

- **收紧 `UnqualifiedAccess` 为 `warning`** 可能一次性暴露大量历史告警；应 **分目录** 或 **分 PR** 提升级别。
- **回滚**：删除或放宽 `client/qml/.qmllint.ini` 中对应项；从 `verify-client-ui-and-video-coverage.sh` 去掉对本脚本的调用。
