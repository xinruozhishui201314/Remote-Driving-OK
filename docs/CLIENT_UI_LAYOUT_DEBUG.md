# 客户端 UI 布局诊断

## 直接查看远驾操作界面

```bash
# 仅显示远驾操作界面，跳过登录/选车，约 2 秒后自动进入
bash scripts/show-driving-ui.sh
```

前置：镜像 `remote-driving-client-dev:full` 已构建；宿主机有 X11 显示。

若未起效：脚本使用 `docker-compose.layout-preview.yml` 跳过 vehicle 依赖；客户端从 `/workspace/client` 运行以正确加载 QML。检查 `echo $DISPLAY` 与 `xhost +local:docker`。

## 快速验证脚本

```bash
# 快速模式（约 14ms，仅 QML 静态检查）
bash scripts/verify-driving-layout.sh

# 完整模式（含编译）
bash scripts/verify-driving-layout.sh --compile
```

## 日志前缀

所有布局相关日志使用统一前缀 `[Client][UI][Layout]`，便于精准定位：

```bash
# 过滤布局日志
docker compose logs client-dev 2>&1 | grep "\[Client\]\[UI\]\[Layout\]"

# 或客户端直接运行时
./RemoteDrivingClient 2>&1 | grep "\[Client\]\[UI\]\[Layout\]"
```

## 日志含义

| 日志内容 | 含义 |
|----------|------|
| `drivingInterface=WxH` | 主界面总宽高 |
| `mainRowH=N` | 主内容区高度（扣除顶部栏） |
| `rightColPrefW=N` | 右列期望宽度（比例计算） |
| `右列=WxH` | 右列实际宽高 |
| `右视图=WxH vis=true/false` | 右视图 VideoPanel 尺寸与可见性 |
| `高精地图=WxH vis=true/false` | 高精地图面板尺寸与可见性 |

## 诊断流程

1. **rightCol=Wx0（右列高度为 0）**：根因是 `Item` 无 `implicitHeight`，RowLayout 按 preferred 比例分配时得到 0。修复：为 `rightColMeasurer` 添加 `Layout.minimumHeight: 224` 和 `Layout.preferredHeight: 400`。另：`componentsReady=false` 时父 ColumnLayout 为 invisible，布局预览模式应提前设 `componentsReady=true`。
2. **右视图=0x0**：右视图未获得高度，检查 `Layout.preferredHeight` 与父容器高度
3. **高精地图=0x0**：高精地图未获得空间，检查 `Layout.fillHeight` 与右列总高度
4. **vis=false**：组件被隐藏，检查 `visible` 绑定

## 布局策略：先整体分配再填充

1. **顶层分配**：`mainRowAvailW` = 总宽 - 边距 - 列间距
2. **三列先分配**：`leftColAllocW`(22%)、`centerColAllocW`(55%)、`rightColAllocW`(23%)，比例和=1
3. **右列内部分配**：按 `rightColMeasurer.height` 实际高度 58:42 分配，避免高精地图被压到 120px（原 rightColAllocH 用 drivingInterface.height 导致超分配）
4. **每列使用**：`Layout.preferredWidth: xxxColAllocW`，`Layout.fillWidth: false`，避免互相挤压
5. **最小保护**：左 200、中 400、右 260；右视图 100、高精地图 120

## 启用方式

布局日志在 DrivingInterface 加载时自动输出，无需额外配置。定时器在 500ms、1000ms、1500ms、2000ms、2500ms 各输出一次，便于观察布局稳定后的最终状态。
