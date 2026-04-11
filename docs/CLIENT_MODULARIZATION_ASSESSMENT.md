# 客户端 C++ / QML 模块化评估与演进清单

| 字段 | 值 |
|------|-----|
| 文档版本 | 1.0 |
| 关联契约 | [CLIENT_UI_MODULE_CONTRACT.md](CLIENT_UI_MODULE_CONTRACT.md)（DrivingFacade v3，`facade.appServices` + `internal` 禁单例） |
| 调用链说明 | [client/docs/CALLCHAIN_AND_ARCHITECTURE.md](../client/docs/CALLCHAIN_AND_ARCHITECTURE.md) |
| Google 级工程对齐（Qt/`m_`/clang-tidy） | [CLIENT_GOOGLE_ENGINEERING_ALIGNMENT.md](CLIENT_GOOGLE_ENGINEERING_ALIGNMENT.md) |
| 视频/UI 热点路径审计 | [CLIENT_CPP_HOTPATH_VIDEO_AUDIT.md](CLIENT_CPP_HOTPATH_VIDEO_AUDIT.md) |
| Qt UI「Google 级」可执行门禁 | [CLIENT_QT_UI_GOOGLE_STYLE.md](CLIENT_QT_UI_GOOGLE_STYLE.md)、`./scripts/verify-client-qt-ui-google-style.sh` |

本文落实「5 Whys + 进一步模块化空间」计划：对齐 **Google 公开工程准则**、盘点 `DrivingInterface.qml` 职责、厘清遗留 QML 与 canonical 路径、给出 C++ 协调类拆分草图，并约定 **契约门禁**（自动化见 `./scripts/verify-client-ui-module-contract.sh`）。

---

## 1. Executive Summary

- **C++**：`core/`、`services/`、`infrastructure/`、`presentation/models/`、`app/` 分层清晰，符合「按职责分目录 + 显式依赖」的实践。
- **QML**：`shell/SessionWorkspace` 隔离会话阶段；`components/driving/*` 经 `required property Item facade` 与 **DrivingFacade v3**（`facade` / `facade.teleop` / `internal` → `facade.appServices`）收敛，模块化 **中等偏上**。
- **主要债**：[`DrivingInterface.qml`](../client/qml/DrivingInterface.qml) 仍聚合布局度量、遥测展示、控车入口、键盘输入与诊断；[`AppContext.qml`](../client/qml/AppContext.qml) 为全局服务定位器（有意设计，可测试性需额外策略）。
- **门禁**：修改契约或 facade 形状时，须跑 `./scripts/verify-client-ui-module-contract.sh`（已接入 `verify-client-ui-and-video-coverage.sh`）。

---

## 2. 与 Google 公开准则的对齐（模块化 / 接口化）

> 本仓库使用 **Qt/QML + CMake**，并非 Bazel monorepo；下列映射将 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) 中与**模块边界、依赖、接口**相关的条目，转译为可执行检查项。

| Google C++ Style Guide 要点 | 在本客户端的落点 |
|-----------------------------|------------------|
| **Include What You Use**（须直接包含所用符号的头文件，勿依赖传递包含） | 新增/修改 `.cpp` 时保持头文件自洽；审查 `vehiclemanager.cpp`、`mqttcontroller.cpp` 等协调类时，避免在头文件中暴露仅实现细节需要的类型。 |
| **Names and Order of Includes** | 已有约定：Related header → 系统/C++ 库 → 第三方 → 本项目（见各 `.cpp` 实践）。 |
| **Namespaces** | C++ 侧可用 `Client::` / `ClientApp::` 渐进包裹新代码（[`client_app_bootstrap.cpp`](../client/src/app/client_app_bootstrap.cpp) 已用 `ClientApp`）；大迁移非本阶段目标。 |
| **最小意外依赖 / 面向读者的接口** | QML：**唯一 canonical 远驾 UI** = `components/driving/*` + `DrivingInterface` 根 facade；五件套与 `internal` 经 **`facade.appServices`** 取全局服务，不在子 QML 写 `AppContext.`（契约 §3.6）。 |
| **API 稳定性与版本语义**（与 Google 大型代码库惯例一致：显式破坏性变更） | [`CLIENT_UI_MODULE_CONTRACT.md`](CLIENT_UI_MODULE_CONTRACT.md) §6：additive vs breaking；**DrivingFacade 主版本**与 `teleopBinder`、`appServices` 同步。 |

**未采用**（环境差异）：Google 内部常用的 Bazel `visibility` / strict deps 在 CMake 中无等价一键检查；以 **目录约定 + 脚本静态 grep + 契约文档** 补偿。

---

## 3. 5 Whys（模块化视角，摘要）

| # | Why | 结论 |
|---|-----|------|
| 1 | 为何要谈模块化？ | 远驾涉及控车/会话/视频/降级；边界模糊则一改多崩、难以切片验证。 |
| 2 | 当前是否已缓解？ | **部分缓解**：C++ 分层；QML `shell` + `facade`/`facade.teleop` 契约。 |
| 3 | 为何仍不足？ | `DrivingInterface.qml` 单文件职责过多；契约根上成员多，主版本升级成本高。 |
| 4 | 为何 Facade 膨胀？ | QML 惯用根 Item 汇聚状态；排障日志/诊断与历史切片文件并存。 |
| 5 | 根因级结论 | 缺 **Teleop 领域层**（独立 State/Commands/LayoutMetrics 对象或 C++ Model）；全局 `AppContext` 利于启动、不利于替换测试。 |

---

## 4. `DrivingInterface.qml` 四类职责盘点（audit-driving-interface）

**已实现（代码）**：`internal/*` 上列五文件；[`DrivingInterface.qml`](../client/qml/DrivingInterface.qml) 以 **property alias** 保持根 facade，并以 **`appServicesBridge`** 向 `internal` 注入全局服务（**v3**：`internal` 不直连 `AppContext`）。

以下表仍作 **职责说明** 与后续 C++ Model 演进参考。

### 4.1 Layout（布局常量 + 几何 + 诊断）

| 片段 | 行号（约） | 迁出目标名 | 说明 |
|------|------------|------------|------|
| `layoutConfig` 与各 `readonly property` 比例 | ~156–239 | `LayoutMetrics.qml` 或 `QtObject` 子节点 | 只读度量；`mainRowAvailH` 依赖 `drivingLayoutShell` 时需传入或在子对象内 `property Item shell`。 |
| `logLayout` / `logLayoutFull` / `layoutLogTimer` / `isLayoutDebugEnabled` | ~25–154 | `DrivingLayoutDiagnostics.qml` 或 JS 模块 | 与 `CLIENT_LAYOUT_DEBUG` 同路径文档化。 |

### 4.2 Teleop presentation state（展示状态 + 车端绑定）

| 片段 | 行号（约） | 迁出目标 | 说明 |
|------|------------|----------|------|
| `currentGear`…`workLampActive`、`displaySpeed`/`displayGear`/`displaySteering`、水箱/清扫等 | ~259–291 | `TeleopPresentationState`（QML `QtObject` 或 C++ `QObject`） | 绑定 `AppContext.vehicleStatus` / MQTT 的逻辑集中；`teleopBinder` 改为 alias 到新对象。 |
| `onCurrentGearChanged` 内远驾检查 + `sendControlCommand` | ~441–464 | `TeleopGearPolicy` 或并入 `VehicleControlService` 触发条件 | 减少 QML 内业务规则。 |

### 4.3 Input（键盘 + 安全死手）

| 片段 | 行号（约） | 迁出目标 | 说明 |
|------|------------|----------|------|
| `Keys.onPressed`、`safetyMonitor.notifyOperatorActivity` | ~473–500 | `TeleopKeyboardHandler.qml` | `focus: true` 留在根或交给子 Item；接口：修改 `facade.teleop` / 调 `sendControlCommand`。 |

### 4.4 Diagnostics（调试）

| 片段 | 行号（约） | 迁出目标 | 说明 |
|------|------------|----------|------|
| `dumpVideoDiagnostics` | ~386–439 | `DrivingVideoDiagnostics.js` 或独立 `Item` + `function` | 仅 `--debug` 路径；减少根 Item 体积。 |

### 4.5 根上保留（薄 Facade）

- `Drv.DrivingLayoutShell { facade: drivingInterface }`
- `readonly property var teleop` 及对子对象的 **组合与 alias**（契约 §3.5）
- `sendControlCommand` 可保留一行委托至 `VehicleControlService`，或迁入 State 对象

---

## 5. 待办落实：`DrivingInterface.*.qml` 与 `components/driving/*`（reconcile-ui-assets）

### 5.1 仓库内引用检索（2026-04-10）

- 全仓库 **无** `DrivingInterface.Controls` / `DrivingInterface.VideoPanels` / `DrivingInterface.Dashboard` 的 import 或类型引用（除 [CLIENT_UI_MODULE_CONTRACT.md](CLIENT_UI_MODULE_CONTRACT.md) 说明外）。
- **Canonical 链**：[`DrivingInterface.qml`](../client/qml/DrivingInterface.qml) → `import "components/driving" as Drv` → `DrivingLayoutShell` → 各 Rail / Center / TopChrome。
- **静态验证入口**：[`scripts/verify-driving-layout.sh`](../scripts/verify-driving-layout.sh) 仅扫描 `DrivingInterface.qml` + `components/driving/*.qml`。

### 5.2 团队约定（与契约 §5 一致）

| 文件 | 状态 | 动作 |
|------|------|------|
| `components/DrivingInterface.Controls.qml` | 归档 / 未接入 | 文件头 **@archived**；功能变更须 port 到 `DrivingCenterColumn` 等 |
| `components/DrivingInterface.VideoPanels.qml` | 同上 | 同上 |
| `components/DrivingInterface.Dashboard.qml` | 同上 | 同上 |

### 5.3 迁移清单（若需合并历史差异）

1.  diff 上述三文件与 `DrivingCenterColumn` / `DrivingLeftRail` / `DrivingRightRail` 的行为差异。  
2.  将有效差异 port 到 `driving/*` 并更新 [CLIENT_UI_MODULE_CONTRACT.md](CLIENT_UI_MODULE_CONTRACT.md) §3–§4。  
3.  跑 `./scripts/verify-driving-layout.sh` 与 `./scripts/verify-client-ui-module-contract.sh`。  
4.  可选：删除三文件或移至 `docs/archive/`（需确认无外部 fork/脚本依赖）。

---

## 6. C++ 协调类接口级拆分（cpp-split-candidates）

### 6.1 `VehicleManager`（[`vehiclemanager.h`](../client/src/vehiclemanager.h)）

**已实现**：HTTP 列表由 [`VehicleCatalogClient`](../client/src/services/vehiclecatalogclient.h) 执行，建会话由 [`RemoteSessionClient`](../client/src/services/remotesessionclient.h) 执行；JSON 解析在 [`vehicle_api_parsers.cpp`](../client/src/services/vehicle_api_parsers.cpp)（单测 `test_vehicle_api_parsers`）。`VehicleManager` 保留 **Q_PROPERTY / 信号 / VIN 状态 / updateVehicleList**，行为与拆分前一致。

历史说明：原先职责合一为 **车辆目录（GET /api/v1/vins）** + **会话创建（POST …/sessions）** + **本地 VIN 选择与缓存**。

| 提议接口（新类型，示意名） | 职责 | 依赖 |
|----------------------------|------|------|
| `IVehicleCatalog` / `VehicleCatalogClient` | `refreshVehicleList`、解析 JSON → `VehicleInfo` | `QNetworkAccessManager`、Backend base URL |
| `ISessionProvisioner` / `RemoteSessionClient` | `startSessionForCurrentVin`、解析 whip/whep/controlConfig | 同上 + VIN |
| `VehicleManager`（变薄） | 组合二者、保留 `Q_PROPERTY` 与 QML 信号 | 二者 + 状态字段 |

**契约对齐**：HTTP 路径与字段以后端 OpenAPI / 项目 `docs/` 为准；**MQTT 载荷**仍以 [`mqtt/schemas/`](../mqtt/schemas/) 为准（会话响应里的 `controlConfig` 仅作桥接，不替代 MQTT schema）。

### 6.2 其他协调类（后续 PR）

- **`MqttController`**：保持「传输 + 信封」边界；策略见 [`utils/MqttControlEnvelope`](../client/src/utils/MqttControlEnvelope.h)。  
- **`WebRTCStreamManager`**：编排 vs 单路 client 已部分分离；新逻辑优先进 `media/*` 或独立 `StreamOrchestrator`。

---

## 7. 待办落实：契约版本门禁（contract-metrics）

### 7.1 人工规则（与 [CLIENT_UI_MODULE_CONTRACT.md](CLIENT_UI_MODULE_CONTRACT.md) §6 一致）

| 变更类型 | DrivingFacade | 文档 |
|----------|---------------|------|
| 根上新增可选 property + `teleopBinder` alias | v2，修订号 +1 | 更新 §3.3–§3.5 表 |
| 仅 `driving/*` 新增只读输出 | v2，修订号 +1 | 更新 §4 |
| 重命名/删改根或 `teleop` 成员语义 | **主版本 bump**（如 v3） | 全量子模块 + 脚本期望 |

### 7.2 自动化

- 脚本：[`scripts/verify-client-ui-module-contract.sh`](../scripts/verify-client-ui-module-contract.sh)  
- 串联：[`scripts/verify-client-ui-and-video-coverage.sh`](../scripts/verify-client-ui-and-video-coverage.sh) 第 3 步  

检查项摘要：契约文件存在且声明 **DrivingFacade v3**；`DrivingInterface.qml` 含 `appServices`、`teleop`/`teleopBinder` 与 `DrivingLayoutShell`；**禁止** canonical 文件再 import 遗留 `DrivingInterface.*`；`internal` 禁 `RemoteDriving`/`AppContext`；`components/driving/qmldir` 模块齐全。

---

## 8. 验证命令

```bash
./scripts/verify-client-ui-module-contract.sh
./scripts/verify-client-ui-and-video-coverage.sh   # 已含上者
```

---

## 9. 修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0 | 2026-04-10 | 首版：Google 对齐、四类职责盘点、遗留 QML 结论、VehicleManager 拆分草图、契约门禁 |
