# 客户端字体问题修复

## 问题描述

客户端启动时出现警告：
```
No Chinese font found, Chinese text may not display correctly
Available fonts: QList("DejaVu Sans", "DejaVu Sans Mono", "DejaVu Serif", "Monospace", "Sans Serif", "Serif")
```

## 原因分析

Qt6 容器镜像（`docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt`）默认没有安装中文字体，导致中文文本无法正确显示。

## 解决方案

### 方案 1：在运行中的容器内安装字体（推荐）

使用提供的脚本自动安装：

```bash
bash scripts/install-client-fonts.sh
```

或者手动安装：

```bash
# 安装中文字体
docker compose exec -u root client-dev bash -c "
    apt-get update && \
    apt-get install -y fonts-noto-cjk fonts-wqy-microhei fontconfig && \
    fc-cache -fv
"
```

### 方案 2：创建自定义 Dockerfile（永久方案）

如果需要永久解决，可以创建自定义 Dockerfile：

```dockerfile
FROM docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt

# 安装中文字体
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        fonts-noto-cjk \
        fonts-wqy-microhei \
        fontconfig \
    && rm -rf /var/lib/apt/lists/*

# 更新字体缓存
RUN fc-cache -fv
```

然后修改 `docker-compose.yml` 使用自定义镜像。

## 安装的字体

- **Noto Sans CJK**：Google Noto 字体，支持中日韩文字
- **WenQuanYi Micro Hei**：文泉驿微米黑，开源中文字体

## 验证字体安装

### 检查字体列表

```bash
# 查看中文字体
docker compose exec client-dev bash -c "fc-list :lang=zh | head -5"

# 查看所有字体
docker compose exec client-dev bash -c "fc-list | grep -i 'noto\|wqy' | head -5"
```

### 验证客户端字体检测

重新启动客户端，应该看到：

```
Using Chinese font: Noto Sans CJK SC
```

或者

```
Using Chinese font: WenQuanYi Micro Hei
```

## QML 字体配置

客户端 QML 代码已经配置了中文字体检测逻辑（`client/qml/main.qml`）：

```qml
property string chineseFont: {
    var fonts = ["WenQuanYi Zen Hei", "WenQuanYi Micro Hei", "Noto Sans CJK SC", 
                 "Noto Sans CJK TC", "Source Han Sans SC", "Droid Sans Fallback",
                 "SimHei", "Microsoft YaHei"]
    var availableFonts = Qt.fontFamilies()
    for (var i = 0; i < fonts.length; i++) {
        if (availableFonts.indexOf(fonts[i]) !== -1) {
            console.log("Using Chinese font:", fonts[i])
            return fonts[i]
        }
    }
    console.warn("No Chinese font found, Chinese text may not display correctly")
    return ""
}
```

安装字体后，客户端会自动检测并使用可用的中文字体。

## 相关文件

- `scripts/install-client-fonts.sh`：字体安装脚本
- `client/Dockerfile.fonts`：自定义 Dockerfile（可选）
- `client/qml/main.qml`：主窗口字体配置

## 注意事项

1. **容器重启后字体仍然存在**：字体安装在容器的文件系统中，只要容器不删除，字体就会保留。

2. **如果容器被删除重建**：需要重新运行字体安装脚本，或者使用自定义 Dockerfile。

3. **字体文件大小**：中文字体包较大（约 100-200MB），首次安装可能需要一些时间。

4. **字体缓存**：安装后需要运行 `fc-cache -fv` 更新字体缓存，Qt 才能检测到新字体。
