# 主驾驶界面图标 (client/qml/icon)

本目录为界面使用的 SVG 图标，**统一用 SVG 替代 emoji/Unicode**，避免 Linux 下缺字体出现黑框。

| 文件 | 用途 | 引用位置 |
|------|------|----------|
| `eye.svg` | 密码可见（睁眼） | 登录页/登录框密码框内 |
| `eye-off.svg` | 密码隐藏（闭眼） | 登录页/登录框密码框内 |

其余为 `DrivingInterface.qml` 等使用。若界面与设计图不一致，可在此替换或新增 SVG，或从设计图裁剪控件图片后放入此处并修改 QML 引用。

## 顶栏左侧（灯光/警示）

| 文件 | 用途 | 备注 |
|------|------|------|
| `light.svg` | 近光、大灯 | 激活时绿框 |
| `highbeam.svg` | 远光 | 激活时绿框 |
| `foglight.svg` | 雾灯 | |
| `wiper.svg` | 雨刷 | |
| `warning.svg` | 警示 | 激活时红框 |

## 顶栏右侧（工具）

| 文件 | 用途 | 备注 |
|------|------|------|
| `sun.svg` | 太阳 | 与设计图「太阳」对应 |
| `warning.svg` | 警告 | |
| `settings.svg` | 齿轮 | |
| `location.svg` | 位置 | |
| `weather.svg` | 云 | |

## 高精地图

| 文件 | 用途 | 备注 |
|------|------|------|
| `zoom.svg` | 右下角缩放（放大镜） | 蓝色 `#4A90E2` |

## 替换为网上下载或设计图裁剪图

1. **下载 SVG**：可从 [Feather Icons](https://feathericons.com/)、[Heroicons](https://heroicons.com/) 等下载，保持 24×24 或统一 viewBox，颜色可用 `currentColor` 或与界面一致的色值（如 `#4A90E2`、`#B0B0B0`）。
2. **裁剪设计图**：从设计图截取对应控件，导出为 PNG 或描摹为 SVG，放入本目录并在 `DrivingInterface.qml` 中把 `source` 改为新文件名。
3. **再次验证**：运行 `bash scripts/run-client-ui.sh --reset-login`，按 `docs/CLIENT_UI_VERIFICATION_CHECKLIST.md` 逐项核对。

## 彻底解决图标/emoji 显示黑框

Linux 下系统缺 emoji 或图标字体时，用 Unicode/emoji 会显示为黑框。**做法**：在本目录增加对应 SVG，在 QML 中用 `Image { source: "icon/xxx.svg" }` 替代 `Text { text: "emoji" }`。路径相对当前 QML 文件所在目录（即 `qml/`），故写 `icon/xxx.svg` 即可。

已用 SVG 替代的位置：登录页/登录框 logo（`vehicle.svg`）、密码框眼睛（`eye.svg` / `eye-off.svg`）。若其他处仍有黑框（如 ControlPanel 的 ⚡/⚙️、StatusBar、VideoView 全屏按钮等），可在此目录新增 SVG 并在 QML 中改为 `Image` 引用。
