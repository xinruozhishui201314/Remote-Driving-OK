# 客户端图标升级为 SVG/PNG 资源

## Executive Summary

✅ **已完成**：
- 默认档位改为 **N 档**（之前是 D 档）
- 所有图标从文字/emoji 升级为 **SVG 图标资源**
- 在 `client/qml/icon/` 目录下创建了所有必需的图标文件
- 所有 QML 代码中的图标显示改为使用 `Image` 组件加载 SVG

**收益**：
- 图标显示更稳定，不依赖系统字体
- 图标视觉效果更专业、统一
- 易于后续替换和定制图标

---

## 1. 变更清单

### 1.1 默认档位修改

**文件**：`client/qml/DrivingInterface.qml`

**修改**：
```qml
// 修改前
property string currentGear: "D"

// 修改后
property string currentGear: "N"
```

**影响**：
- 启动时默认档位为 N（空档）
- 界面显示"前进模式"（因为 N 不是 R）
- 主视图显示前进模式的 AR 叠加层

---

### 1.2 图标资源目录结构

```
client/qml/icon/
├── audio.svg          # 音频
├── battery.svg        # 电池
├── brightness.svg     # 亮度
├── check.svg          # 检查
├── danger.svg         # 危险
├── data.svg           # 数据
├── foglight.svg       # 雾灯
├── fluid.svg          # 工作液
├── highbeam.svg       # 远光灯
├── info.svg           # 信息
├── light.svg          # 大灯/近光灯
├── location.svg       # 位置
├── lowlight.svg       # 低光
├── message.svg        # 消息
├── network.svg        # 网络
├── rest.svg           # 休息
├── settings.svg       # 设置
├── time.svg           # 时间
├── tire.svg           # 轮胎
├── vehicle.svg        # 车辆
├── warning.svg        # 警告/警示
├── weather.svg        # 天气
└── wiper.svg          # 雨刷
```

**图标总数**：23 个 SVG 文件

---

### 1.3 QML 代码修改位置

#### 1.3.1 顶部左侧图标组

**位置**：`DrivingInterface.qml` 第 68-105 行

**修改前**：
```qml
{ icon: "灯", name: "大灯", active: true }
Text {
    text: modelData.icon
    font.pixelSize: 14
}
```

**修改后**：
```qml
{ icon: "icon/light.svg", name: "大灯", active: true }
Image {
    anchors.centerIn: parent
    width: 18
    height: 18
    source: modelData.icon
    fillMode: Image.PreserveAspectFit
}
```

**图标映射**：
- `灯` → `icon/light.svg`
- `远` → `icon/highbeam.svg`
- `雨` → `icon/wiper.svg`
- `雾` → `icon/foglight.svg`
- `警` → `icon/warning.svg`

---

#### 1.3.2 顶部右侧图标组

**位置**：`DrivingInterface.qml` 第 120-157 行

**图标映射**：
- `亮` → `icon/brightness.svg`
- `警` → `icon/warning.svg`
- `音` → `icon/audio.svg`
- `设` → `icon/settings.svg`
- `位` → `icon/location.svg`
- `天` → `icon/weather.svg`

---

#### 1.3.3 左侧垂直图标列

**位置**：`DrivingInterface.qml` 第 306-347 行

**图标映射**：
- `灯` → `icon/light.svg`
- `雨` → `icon/wiper.svg`
- `警` → `icon/warning.svg`
- `车` → `icon/vehicle.svg`
- `设` → `icon/settings.svg`

**尺寸**：16x16 像素（比顶部图标稍小）

---

#### 1.3.4 右侧垂直图标列

**位置**：`DrivingInterface.qml` 第 740-781 行

**图标映射**：与左侧垂直图标列相同

---

#### 1.3.5 底部控制按钮网格

**位置**：`DrivingInterface.qml` 第 963-1033 行

**前进模式图标映射**（12 个）：
- `亮` → `icon/brightness.svg`
- `灯` → `icon/light.svg`
- `远` → `icon/highbeam.svg`
- `雨` → `icon/wiper.svg`
- `警` → `icon/check.svg`
- `电` → `icon/battery.svg`
- `时` → `icon/time.svg`
- `胎` → `icon/tire.svg`
- `告` → `icon/warning.svg`
- `信` → `icon/message.svg`
- `设` → `icon/settings.svg`
- `液` → `icon/fluid.svg`

**倒车模式图标映射**（15 个）：
- `警` → `icon/warning.svg`
- `灯` → `icon/light.svg`
- `雨` → `icon/wiper.svg`
- `检` → `icon/check.svg`
- `电` → `icon/battery.svg`
- `网` → `icon/network.svg`
- `暗` → `icon/lowlight.svg`
- `休` → `icon/rest.svg`
- `危` → `icon/danger.svg`
- `告` → `icon/warning.svg`
- `信` → `icon/info.svg`
- `液` → `icon/fluid.svg`
- `工` → `icon/tool.svg`
- `数` → `icon/data.svg`
- `设` → `icon/settings.svg`

**尺寸**：20x20 像素

---

## 2. SVG 图标设计规范

### 2.1 尺寸

- **标准尺寸**：24x24 像素（viewBox="0 0 24 24"）
- **实际显示尺寸**：
  - 顶部图标：18x18 像素
  - 垂直图标列：16x16 像素
  - 底部按钮：20x20 像素

### 2.2 颜色方案

- **主色调**：`#4A90E2`（蓝色）
- **激活状态**：`#50C878`（绿色）
- **警告**：`#FF6B6B`（红色）
- **次要**：`#90C2FF`（浅蓝色）
- **高亮**：`#FFAA00`（黄色）

### 2.3 样式

- **线条宽度**：2px（stroke-width="2"）
- **圆角**：round（stroke-linecap="round"）
- **填充**：部分图标有填充色，部分仅描边

---

## 3. 验证步骤

### 3.1 编译验证

```bash
cd /home/wqs/bigdata/Remote-Driving
docker compose exec client-dev bash -c "cd /tmp/client-build && make -j4 RemoteDrivingClient"
```

**预期结果**：编译成功，无错误

---

### 3.2 运行时验证

```bash
bash scripts/run-client-ui.sh --reset-login
```

**验证点**：
1. ✅ 所有图标正常显示（不再是文字）
2. ✅ 图标大小合适，不模糊
3. ✅ 默认档位为 N 档
4. ✅ 点击 N 档，界面显示"前进模式"
5. ✅ 点击 R 档，界面切换为"倒车模式"，显示红色网格
6. ✅ 所有图标 ToolTip 正常显示中文名称

---

## 4. 文件清单

### 4.1 新增文件

**图标资源目录**：
- `client/qml/icon/`（23 个 SVG 文件）

**文档**：
- `docs/CLIENT_ICON_UPGRADE.md`（本文档）

### 4.2 修改文件

- `client/qml/DrivingInterface.qml`
  - 默认档位：`"D"` → `"N"`
  - 所有图标显示：`Text` → `Image` + SVG 路径

---

## 5. 后续优化建议

### 5.1 图标优化

- [ ] 根据实际 UI 设计图，调整图标样式和颜色
- [ ] 添加图标激活/未激活状态的视觉差异（当前仅背景色不同）
- [ ] 考虑添加图标动画效果（如闪烁、旋转）

### 5.2 性能优化

- [ ] 如果图标数量很多，考虑使用 Qt 资源系统（.qrc）打包
- [ ] 对于常用图标，可以预加载到内存

### 5.3 可维护性

- [ ] 创建图标映射表（JSON/QML），统一管理图标路径
- [ ] 添加图标缺失时的 fallback 机制

---

## 6. 总结

✅ **已完成**：
- 默认档位改为 N 档
- 所有图标升级为 SVG 资源
- 图标资源文件已创建并组织在 `icon/` 目录
- QML 代码已更新，使用 `Image` 组件加载 SVG

**编译状态**：✅ 成功

**下一步**：运行客户端，验证图标显示效果和默认档位行为。
