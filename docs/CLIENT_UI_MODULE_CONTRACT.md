# 客户端 UI 模块契约（远驾主界面）

| 字段 | 值 |
|------|-----|
| 文档版本 | 1.6 |
| 契约版本 | **DrivingFacade v3**（根：`facade`；`facade.teleop`；**`facade.appServices`**：`internal/*` 与 **五件套** 统一服务窄面） |
| 适用路径 | `client/qml/DrivingInterface.qml`、`client/qml/DrivingFacade/*`（独立 URI **`DrivingFacade 1.0`**，见 §1.3）、`client/qml/components/driving/*`（含 `driving/internal/*`，规则见 §1.2） |
| 与 project_spec | 驾驶舱客户端能力见 `project_spec.md` 第 3/5 章；**契约分级与 CI 映射**见 **§12**；本文档为 **QML 模块边界** 补充，冲突时以 `project_spec.md` 为准 |
| Qt UI 静态门禁 | [CLIENT_QT_UI_GOOGLE_STYLE.md](CLIENT_QT_UI_GOOGLE_STYLE.md)（`qmllint` + UI C++ format）；入口 `./scripts/verify-client-qt-ui-google-style.sh` |

---

## 1. 目的与范围

### 1.1 目的

- 规定远驾主界面拆分后 **子模块与父级之间的唯一正式通道**：`required property Item facade`（运行时指向 `DrivingInterface` 根 `id: drivingInterface`）。
- 规定 **`components/driving/*`（五件套）与 `internal/*`** 业务服务 **只** 经 **`facade.appServices`** 访问（`DrivingInterface` 内 `appServicesBridge` 绑定 **`AppContext`**）；五件套 **禁止**出现 **`AppContext.`**（与 `internal` 门禁一致）。**例外**：五件套可 **`import RemoteDriving 1.0` 且仅用于** 引用 **`QML_ELEMENT` 暴露的 C++ 类型名**（如 `WebRtcClient`），**不得**在该 import 下使用 `AppContext` 单例或任何 `AppContext.` 访问。`internal/*` 仍 **禁止** `import RemoteDriving`（见 §1.2）。全树 **不**引入平行于 `AppContext` 的 `rd_*` 注入。
- 便于评审「功能是否独立、接口是否可扩展」：新增远驾会话相关能力时优先加在 **根 property + `facade.teleop` alias**（见 §3.5），或 bump 契约版本（见 §6）。

### 1.2 范围

| 在范围内 | 不在范围内（另文档/另契约） |
|----------|------------------------------|
| `components/driving/*.qml`（布局壳五件套，见 §2） | `shell/*` 会话壳（可另立 `SessionShellContract`） |
| `components/driving/internal/*.qml` | **仅允许**由 [`DrivingInterface.qml`](../client/qml/DrivingInterface.qml) `import "components/driving/internal"`；**禁止**由 `DrivingLayoutShell` 子树或其它 QML 直接 import（避免绕过 `facade` 通道） |
| `DrivingInterface` 作为 `facade` 提供者 | **URI `DrivingFacade 1.0`** + `driving-facade.qmltypes`；C++ 服务类型见 **`RemoteDriving`** / `remote-driving-cpp.qmltypes` |
| `AppContext` 作为服务定位器 | MQTT payload JSON Schema（见 `mqtt/schemas/`） |

**`internal/` 与契约关系（v3）**：

- **`internal/*` 禁止** `import RemoteDriving 1.0`、**禁止** 出现 `AppContext.`；**必须**通过 **`property Item facade`**（由 `DrivingInterface` 传入根 item）读取 **`facade.appServices.*`**（见 §3.6）。五件套对 `import RemoteDriving` 的例外见 §1.1。
- **例外**：纯布局切片（`LayoutMetrics`、`DrivingLayoutDiagnostics`）无服务依赖，**可不**声明 `facade`；`DrivingVideoDiagnostics` 的 `dump(facade, teleop, shell)` **首参**为 facade。
- **依赖 `appServices` 的类型**：`TeleopPresentationState`、`TeleopKeyboardHandler` **须**含 `property Item facade` 并由 `DrivingInterface` 赋值。
- **对外稳定面**：§2 五件套仍 **只** 接收 `facade`；根上 §3.1–§3.5 与 `teleopBinder` 不变；**新增**只读 `appServices`（§3.6）。

### 1.3 Facade 独立 URI（V1：`DrivingFacade`）

- **目的**：将远驾根 facade 类型 **`DrivingInterface`** 从隐式「同目录文件类型」提升为显式模块 **`import DrivingFacade 1.0`**，与 **`RemoteDriving`**（C++ `QML_ELEMENT` + 单例）解耦；并配套 **`client/qml/DrivingFacade/driving-facade.qmltypes`**（`qmltyperegistrar` 生成，构建后同步入库，见 [CLIENT_QT_UI_GOOGLE_STYLE.md](CLIENT_QT_UI_GOOGLE_STYLE.md)）。
- **磁盘布局**：[`client/qml/DrivingFacade/qmldir`](../client/qml/DrivingFacade/qmldir) 将 `DrivingInterface` 指向 **`../DrivingInterface.qml`**（实现文件仍在 `client/qml/` 根，避免破坏 `import "components/driving"` 等相对路径）。
- **引用约定**：`shell/DrivingStageHost.qml` 等会话壳 **须** `import DrivingFacade 1.0` 后再实例化 **`DrivingInterface`**；**禁止**依赖未限定模块名的隐式解析。
- **说明**：当前 Qt 6 对「仅 QML、无 C++ 导出类型」的静态插件，`driving-facade.qmltypes` 可能为 **`Module {}` 占位**；**qmllint** 仍主要依赖 **`qmldir` + 源 `.qml`** 与 `RemoteDriving` 的 `remote-driving-cpp.qmltypes` 完成类型解析。

---

## 2. 模块清单与版本

| 模块文件 | 角色 | 模块契约版本 |
|----------|------|----------------|
| `DrivingLayoutShell.qml` | 布局壳：顶栏槽位 + 三列 `GridLayout` | **1.0** |
| `DrivingTopChrome.qml` | 顶部 HUD（连接、远驾、状态、时间等） | **1.0** |
| `DrivingLeftRail.qml` | 左视频带（左视 + 后视 `VideoPanel`） | **1.0** |
| `DrivingCenterColumn.qml` | 中列：主视 + 控制 + 仪表盘 | **1.0** |
| `DrivingRightRail.qml` | 右视频带 + 高精地图占位 | **1.0** |
| `qmldir` | 目录内类型登记 | 随上述 **1.0** |

#### 2.1 `internal/`（仅 `DrivingInterface` import；不进入 `qmldir` 对外模块名）

| 文件 | 角色 | 契约说明 |
|------|------|----------|
| `internal/LayoutMetrics.qml` | 布局常量与 `mainRowAvailH/W` 等 | 根 `facade.*` 布局 §3.1 的物理存放处；经根 **alias** 暴露；无 `facade` 属性 |
| `internal/DrivingLayoutDiagnostics.qml` | `logLayout` / `logLayoutFull`、布局诊断 Timer | [`DrivingInterface.logLayout` / `logLayoutFull`](../client/qml/DrivingInterface.qml) 转发至此 |
| `internal/TeleopPresentationState.qml` | §3.3 状态绑定（车端/MQTT 经 `facade.appServices`） | `property Item facade` + `teleopBinder` **alias**；**无** `AppContext` |
| `internal/TeleopKeyboardHandler.qml` | 快捷键逻辑 | `property Item facade`；死手经 `facade.appServices.safetyMonitor` |
| `internal/DrivingVideoDiagnostics.qml` | `dump(facade, teleop, shell)` | `--debug` 诊断；`facade.appServices.webrtcStreamManager` |
| `internal/qmldir` | 供 `import "components/driving/internal"` 解析 | 与 `driving/qmldir`（五件套）分离 |

**引用方式（父级 QML）**：

```qml
import "components/driving" as Drv

Drv.DrivingLayoutShell {
    id: drivingLayoutShell
    anchors.fill: parent
    facade: drivingInterface   // DrivingInterface 根 item
}
```

---

## 3. `DrivingInterface` 根 item（`facade`）

以下成员被 `components/driving/*` **直接**或通过 **`facade.teleop`** /（仅 `internal/*`）**`facade.appServices`** 使用。任何 **删除、重命名、改类型** 视为 **破坏性变更**，须 bump **DrivingFacade 主版本**（见 §6）。

**v3 约定**：在 **v2** 基础上增加 **`facade.appServices`**（§3.6）；**`internal/*` 与五件套** 均只读该窄面；**`AppContext` 仅在 `DrivingInterface` 根** 参与绑定，子 QML 不直连单例。

### 3.1 布局与几何（只读为主）

| 成员 | 类型（QML） | 读写 | 说明 |
|------|-------------|------|------|
| `layoutConfig` | `var`（对象） | 读 | 比例与最小尺寸配置 |
| `topBarRatio` | `real` | 读 | 顶栏高度比例 |
| `mainRowRatio` | `real` | 读 | 主内容区比例 |
| `mainRowSpacing` | `real` | 读 | 主行列间距 |
| `mainRowAvailW` | `real` | 读 | 主行可用宽度 |
| `mainRowAvailH` | `real` | 读 | 主行可用高度（依赖 `drivingLayoutShell` 与顶栏） |
| `sideColAllocW` / `leftColAllocW` / `rightColAllocW` | `real` | 读 | 侧列分配宽度 |
| `centerColAllocW` | `real` | 读 | 中列分配宽度 |
| `sideColMinWidth` / `sideColMaxWidth` | `int` | 读 | 侧列宽约束 |
| `sideColMinHeight` | `real` | 读 | 侧列最小高度 |
| `sideColTopMinHeight` / `sideColBottomMinHeight` | `int` | 读 | 侧列上下区最小高度 |
| `leftVideoRatio` / `leftMapRatio` | `real` | 读 | 侧列上下比例 |
| `centerCameraRatio` / `centerControlsRatio` / `centerDashboardRatio` | `real` | 读 | 中列三段比例 |
| `controlAreaMargin` / `controlAreaSpacing` | `int` | 读 | 控制区内边距与间距 |
| `controlSideColumnRatio` | `real` | 读 | 控制区左右侧列占中列宽比例 |
| `controlSpeedometerSize` | `int` | 读 | 速度表最大边长上限 |
| `minControlHeight` / `minDashboardHeight` | `int` | 读 | 控制区 / 仪表盘最小高度 |
| `dashboardMargin` / `dashboardSpacing` | `int` | 读 | 仪表盘边距与间距 |
| `dashboardGearWidth` / `dashboardTankWidth` / `dashboardSpeedWidth` / `dashboardStatusWidth` / `dashboardProgressWidth` / `dashboardGearSelectWidth` | `int` | 读 | 仪表盘各卡槽宽度 |
| `dashboardSplitterMargin` | `int` | 读 | 仪表盘分隔条边距 |

### 3.2 主题与字体（只读）

| 成员 | 类型 | 说明 |
|------|------|------|
| `colorBackground` / `colorPanel` / `colorBorder` / `colorBorderActive` | `color` | 来自 `styles/Theme` |
| `colorAccent` / `colorWarning` / `colorDanger` | `color` | 状态强调色 |
| `colorTextPrimary` / `colorTextSecondary` | `color` | 文案色 |
| `colorButtonBg` / `colorButtonBorder` / `colorButtonBgHover` / `colorButtonBgPressed` | `color` | 按钮系 |
| `chineseFont` | `string` | 自 `AppContext.chineseFont` |

### 3.3 会话与展示状态（根上持久存储；子模块请经 `facade.teleop`）

| 成员 | 类型 | 说明 |
|------|------|------|
| `currentGear` | `string` | 当前档位（`onCurrentGearChanged` 等在根上） |
| `forwardMode` | `bool` | 派生：`currentGear !== "R"` |
| `vehicleSpeed` / `targetSpeed` | `real` | 本地/目标速度 |
| `steeringAngle` | `real` | 本地转向 |
| `displaySpeed` / `displayGear` / `displaySteering` | `real`/`string` | 展示用（优先车端遥测） |
| `waterTankLevel` / `trashBinLevel` | `real` | 箱体遥测展示 |
| `cleaningCurrent` / `cleaningTotal` | `int` | 清扫进度 |
| `leftTurnActive` / `rightTurnActive` / `brakeLightActive` / `workLightActive` / `headlightActive` / `warningLightActive` | `bool` | 灯光 UI 状态 |
| `sweepActive` / `waterSprayActive` / `suctionActive` / `dumpActive` / `hornActive` / `workLampActive` | `bool` | 清扫相关 UI 状态 |
| `emergencyStopPressed` | `bool` | 急停按钮状态 |
| `pendingConnectVideo` | `bool` | 连接流程中标志 |
| `streamStopped` | `bool` | 推流停止后按钮文案状态 |
| `isDebugMode` | `bool` | `--debug` |
| `lastDiagTime` | `int` | 诊断日志节流 |

### 3.4 根上函数与 signal（`teleop` 提供转发方法）

| 成员 | 类型 | 说明 |
|------|------|------|
| `sendControlCommand(type, payload)` | `function` | 统一 UI 指令出口（DataChannel 优先） |
| `gearChanged(gear)` | `signal` | 档位变化 |
| `lightCommandSent(lightType, active)` | `signal` | 灯光相关事件 |
| `sweepCommandSent(sweepType, active)` | `signal` | 清扫相关事件 |
| `speedCommandSent(speed)` | `signal` | 速度设定事件 |
| `openMqttDialogRequested()` | `signal` | 请求打开 MQTT 设置对话框 |

`components/driving` 内 **推荐** 使用 **`facade.teleop.sendControlCommand(...)`**、**`facade.teleop.lightCommandSent(...)`** 等（见 §3.5）；根上仍可直接调用以兼容键盘逻辑等。

### 3.5 `facade.teleop`（嵌套 `QtObject`）

| 项 | 说明 |
|----|------|
| 定义位置 | `DrivingInterface.qml` 内 `QtObject { id: teleopBinder }` |
| 暴露 | `readonly property var teleop: teleopBinder` |
| 成员形态 | 对 §3.3 各 `property` 的 **`property alias`**，指向 [`internal/TeleopPresentationState.qml`](../client/qml/components/driving/internal/TeleopPresentationState.qml) 实例；**无第二份状态** |
| 方法 | `sendControlCommand`、`lightCommandSent`、`sweepCommandSent`、`speedCommandSent`、`openMqttDialogRequested` → 转发至根上实现 |
| 未包装 signal | 如 `gearChanged`：子模块极少 emit；若需要仍可 **`facade.gearChanged(...)`**（根上 signal） |

**使用映射**：

| 子模块 | `facade.*` | `facade.teleop.*` |
|--------|------------|-------------------|
| `DrivingLayoutShell` / `DrivingLeftRail` / `DrivingRightRail` | 布局、主题、字体 | 不使用 |
| `DrivingTopChrome` | 主题、字体 | `pendingConnectVideo`、`streamStopped`、`currentGear`、MQTT 对话框请求等 |
| `DrivingCenterColumn` | 布局、主题、字体 | 全部 §3.3 状态、`sendControlCommand`、相关 signal 转发、`Connections.target` |

### 3.6 `facade.appServices`（DrivingFacade v3：五件套 + `internal` 统一服务窄面）

| 项 | 说明 |
|----|------|
| 定义位置 | [`DrivingInterface.qml`](../client/qml/DrivingInterface.qml) 内 `QtObject { id: appServicesBridge }`，根上 `readonly property var appServices: appServicesBridge` |
| 绑定来源 | 各字段 **readonly** 绑定至 **`AppContext`** 同名属性或计算属性；方法 **`reportVideoFlickerQmlLayerEvent`** 转发至 `AppContext`（**唯一**在 `DrivingInterface` 内直连单例的桥） |
| 当前成员（object） | `mqttController`、`vehicleStatus`、`safetyMonitor`、`webrtcStreamManager`、`vehicleControl`、`vehicleManager`、`systemStateMachine` |
| 当前成员（标量） | `videoStreamsConnected`（`bool`，与 `AppContext.videoStreamsConnected` 同源） |
| 当前成员（方法） | `reportVideoFlickerQmlLayerEvent(where, detail)`（与 `AppContext` 行为一致，供 §4.5 观测） |
| 消费者 | **`components/driving/*.qml`（五件套）** 与 **`internal/*`**；**禁止** `AppContext.`。**`internal/*` 禁止** `import RemoteDriving`。五件套 **可** `import RemoteDriving 1.0` **仅** 用于 C++ 类型名（如 `property WebRtcClient streamClient`），**不得**使用 `AppContext`（门禁见 `verify-client-ui-module-contract.sh`） |
| 测试与替换 | 未来可通过 C++ 注入不同 `appServices` 实现或 QML mock；子 QML 仅依赖 `facade`，便于替换与静态检查 |

---

## 4. 各 `driving/*` 子模块契约

约定：**输入** = 父级传入；**输出** = 对外暴露的 `readonly property` / 子项 `id`（供布局诊断使用：`DrivingInterface.logLayoutFull()` 转发至 `internal/DrivingLayoutDiagnostics.qml`，日志前缀仍为 `[Client][UI][Layout]`）。

### 4.1 `DrivingLayoutShell.qml`（v1.0）

| 方向 | 名称 | 类型 | 说明 |
|------|------|------|------|
| 输入 | `facade` | `Item`（required） | 必须为 `DrivingInterface` 根 |
| 输出 | `topBarRect` | `Item` | 顶栏实例（`DrivingTopChrome`） |
| 输出 | `mainRowLayout` | `GridLayout` | 三列主行 |
| 输出 | `leftColLayout` | `ColumnLayout` | 左列 |
| 输出 | `centerColLayout` | `ColumnLayout` | 中列 |
| 输出 | `rightColMeasurer` | `ColumnLayout` | 右列 |

**从 `facade` 读取**：`mainRowAvailH`、`mainRowSpacing`。

---

### 4.2 `DrivingTopChrome.qml`（v1.0）

| 方向 | 名称 | 类型 | 说明 |
|------|------|------|------|
| 输入 | `facade` | `Item`（required） | 见 §3 |

**从 `facade` 读取**：主题 `colorPanel`、`colorBorder`、`colorAccent`、`colorButton*`、`colorText*` 等。

**从 `facade.teleop` 读取/写入（§3.5）**：`pendingConnectVideo`、`streamStopped`（写）、`currentGear`（读）、`openMqttDialogRequested()`（方法转发）。

**从 `facade.appServices` 读取**：`videoStreamsConnected`、`mqttController`、`vehicleStatus`、`webrtcStreamManager`、`vehicleManager`、`systemStateMachine` 等。

**自身 Layout**：根 `Rectangle` 的 `Layout.*` 由 **`DrivingLayoutShell`** 在实例上设置；类型内不重复写死高度，避免双重 Layout。

---

### 4.3 `DrivingLeftRail.qml`（v1.0）

| 方向 | 名称 | 类型 | 说明 |
|------|------|------|------|
| 输入 | `facade` | `Item`（required） | 见 §3 |
| 输出 | `leftFrontPanel` | `VideoPanel` | 左视 |
| 输出 | `leftRearPanel` | `VideoPanel` | 后视 |

**从 `facade` 读取**：侧列布局与高度预算（`sideCol*`、`mainRowAvailH`、`leftVideoRatio`、`leftMapRatio`）。

**从 `facade.appServices` 读取**：`webrtcStreamManager.leftClient` / `rearClient`；组件类型为 `components/VideoPanel.qml`。

---

### 4.4 `DrivingRightRail.qml`（v1.0）

| 方向 | 名称 | 类型 | 说明 |
|------|------|------|------|
| 输入 | `facade` | `Item`（required） | 见 §3 |
| 输出 | `rightViewVideo` | `VideoPanel` | 右视 |
| 输出 | `hdMapRect` | `Rectangle` | 高精地图占位 |

**从 `facade` 读取**：与左列对称的布局字段 + 主题色 + `chineseFont`。

**从 `facade.appServices` 读取**：`webrtcStreamManager.rightClient`、`vehicleStatus`（地图 Canvas）。

---

### 4.5 `DrivingCenterColumn.qml`（v1.0）

| 方向 | 名称 | 类型 | 说明 |
|------|------|------|------|
| 输入 | `facade` | `Item`（required） | 见 §3 |
| 输出 | `centerCameraRect` | `Rectangle` | 主视外层 |
| 输出 | `centerControlsRect` | `Rectangle` | 控制区 |
| 输出 | `centerDashboardRect` | `Rectangle` | 仪表盘 |
| 输出 | `mainCameraView` | `Rectangle` | 主视核心区（含 `streamClient` 等） |

**从 `facade` 读取**：§3.1 中列布局字段；§3.2 主题与字体。

**从 `facade.teleop` 读取/写入**：§3.3 全部可写灯光/清扫与展示遥测；`sendControlCommand`；`lightCommandSent`/`sweepCommandSent` 等（§3.4–§3.5）。

**`Connections.target: facade.teleop`**：监听 §3.3 属性变化（如水位、车速用于重绘）。

**从 `facade.appServices` 读取**：`webrtcStreamManager.frontClient`（主视）、`vehicleStatus`、`systemStateMachine`、`vehicleControl` 等，与组件内注释一致。

**可观测性（非业务服务通道）**：允许调用 `facade.appServices.reportVideoFlickerQmlLayerEvent(...)` 等仅用于遥测/诊断，**不得**由此绕开 `facade.teleop.sendControlCommand` 发控车指令。

---

## 5. 与 `components/DrivingInterface.*.qml` 的取舍

### 5.1 遗留文件

| 文件 | 历史角色 | 当前状态 |
|------|----------|----------|
| `DrivingInterface.VideoPanels.qml` | 早期从 `DrivingInterface.qml` 抽出的「视频区」 | **未接入**主界面 import 链（仓库内无 `DrivingInterface.VideoPanels` 引用） |
| `DrivingInterface.Controls.qml` | 早期「控制区」切片 | **未接入** |
| `DrivingInterface.Dashboard.qml` | 早期「仪表盘」切片 | **未接入** |

### 5.2 正式取舍（团队约定）

| 决策 | 说明 |
|------|------|
| **唯一canonical 实现** | 远驾主界面布局与交互以 **`client/qml/components/driving/*` + `DrivingInterface.qml`（facade 根）** 为准。 |
| **`DrivingInterface.*.qml` 定位** | 视为 **归档/参考实现**：依赖旧式 `drivingInterface` id、自管主题副本、与当前 `facade` 契约 **不一致**。 |
| **新功能禁止** | 新需求 **不得** 只改 `DrivingInterface.*.qml` 而不改 `driving/*`；否则会出现双轨接口。 |
| **清理选项** | 确认无外部工具/脚本引用后，可 **删除** 或 移至 `docs/archive/` 并 README 标明废弃；若保留，须在文件头注释 **「未使用，勿同步改功能」**。 |

### 5.3 若需合并能力

- 应把 `DrivingInterface.*.qml` 中有价值的差异 **port 到** `DrivingCenterColumn.qml` / `DrivingLeftRail.qml` 等，并 **更新本文档 §3–§4** 与 `verify-driving-layout.sh` 所依赖的 id/约束。

---

## 6. 版本与演进策略

| 变更类型 | DrivingFacade / 文档 | 说明 |
|----------|----------------------|------|
| 仅增加 `appServices` 上 **只读** 字段（additive） | **v3**，文档修订号 +1 | 同步 `appServicesBridge`；`internal` 可用 |
| 仅增加根上可选 property，并 **同步** `teleopBinder` 的 `alias` | **v3**，文档修订号 +1 | 子模块经 `facade.teleop` 可见 |
| 仅增加子模块只读输出（`driving/*`） | **v3**，文档修订号 +1 | 不改变根契约形状 |
| 重命名/删除根、`teleop` 或 `appServices` 成员、或改变语义 | **v3 → v4**（或更高） | 主版本 +1 |
| 新增子 QML 文件、不改变 facade | **v3** | 文档修订号 +1 |

**扩展建议**：

- 远驾会话相关：在根上 **增加 property** → 在 **`teleopBinder` 增加 `property alias`** → 子模块只用 **`facade.teleop`**。
- **五件套 / `internal` 需新服务**：在 **`appServicesBridge`** 增加 **readonly** 或转发方法 → 子模块只用 **`facade.appServices.*`**（勿在 `driving/*` 恢复 `AppContext.`）。
- 中长期可将布局/主题进一步收为 **`facade.layout.*` / `facade.theme.*`**（另一次主版本 + 全量 `driving/*` 迁移）。

---

## 7. 相关脚本与验证

- `./scripts/verify-driving-layout.sh`：在 `DrivingInterface.qml`、`components/driving/*.qml` 与 `internal/DrivingLayoutDiagnostics.qml` 上联合静态检查布局 id 与关键约束。
- `./scripts/verify-client-ui-module-contract.sh`：DrivingFacade **v3**、**§1.3 `DrivingFacade` 子目录 `qmldir` + `DrivingStageHost` 显式 `import DrivingFacade 1.0`**、`appServicesBridge`/`appServices`、`teleopBinder`、`driving/qmldir` 五件套、`internal/qmldir`、**五件套 `*.qml` 禁止 `import RemoteDriving` 与 `AppContext.`**、**`internal/*.qml` 同上**、`TeleopPresentationState`/`TeleopKeyboardHandler` 含 `property Item facade`、**仅 `DrivingInterface.qml` 可 import `internal`**、`driving/**` **无 `rd_*`**、**禁止 canonical 链引用遗留 `DrivingInterface.*`**（已串联进 `verify-client-ui-and-video-coverage.sh`）。
- `./scripts/verify-qmltypes-match-generated.sh`：构建 `RemoteDrivingClient` 后 **`git diff --exit-code`** 检查 `remote-driving-cpp.qmltypes` 与 **`DrivingFacade/driving-facade.qmltypes`**；可选 `VERIFY_QMLTYPES_DRIFT=1` 串联进 `verify-client-ui-quality-chain.sh` / `verify-client-qt-ui-google-style.sh`。**`./scripts/verify-qml-changes.sh`** 编译成功后亦做同项检查。
- `./scripts/regenerate-client-qmltypes.sh`：刷新上述两份生成物（`regenerate-remote-driving-qmltypes.sh` 为兼容入口）。
- `./scripts/verify-qml-appcontext-imports.sh`：扫描 `shell/`、`components/`、`components/driving/`（一层）使用 `AppContext` 的文件；**`internal/` 不在此脚本扫描**（由上一脚本禁止 `AppContext`）。
- 模块化评估全文（5 Whys、Google 公开准则对齐、职责盘点、C++ 拆分草图）：[`CLIENT_MODULARIZATION_ASSESSMENT.md`](CLIENT_MODULARIZATION_ASSESSMENT.md)。
- 修改契约后：**至少**运行 `./scripts/build-and-verify.sh`（或项目规定的 client 构建 + 上述脚本）。

---

## 8. 修订记录

| 文档版本 | 日期 | 说明 |
|----------|------|------|
| 1.0 | 2026-04-10 | 首版：DrivingFacade v1 + `driving/*` 模块表 + `DrivingInterface.*.qml` 取舍 |
| 1.1 | 2026-04-10 | DrivingFacade **v2**：`facade.teleop`（嵌套 `QtObject` + alias）；§4.2/§4.5 与 `DrivingTopChrome`/`DrivingCenterColumn` 对齐；§6 演进表更新 |
| 1.2 | 2026-04-10 | §1.2/§2.1 纳入 `driving/internal/*` 边界；§3.5 明确 teleop 状态物理文件；§4.5 观测 API；§7 门禁与脚本说明对齐 `verify-client-ui-module-contract.sh` |
| 1.3 | 2026-04-10 | **DrivingFacade v3**：§3.6 `facade.appServices`；`internal/*` 禁 `RemoteDriving`/`AppContext`，经 `facade` 注入；§6/§7/门禁脚本对齐 |
| 1.4 | 2026-04-10 | §3.6 扩展 `videoStreamsConnected`、`vehicleManager`、`systemStateMachine`、`reportVideoFlickerQmlLayerEvent`；五件套收敛至 `facade.appServices`；门禁与 §4 对齐 |
| 1.6 | 2026-04-10 | **§1.3**：独立 URI `DrivingFacade 1.0` + `driving-facade.qmltypes`；§7 增加 `verify-qmltypes-match-generated` / `regenerate-client-qmltypes` / `verify-qml-changes` 联动说明 |
