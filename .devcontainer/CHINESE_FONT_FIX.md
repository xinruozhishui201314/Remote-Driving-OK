# 中文字体显示问题修复指南

## 问题描述

界面上的中文字体没有显示出来，显示为方块或空白。

## 原因分析

1. **容器中缺少中文字体**
   - 容器镜像可能只包含基础字体
   - 没有安装中文字体包

2. **Qt/QML 未配置使用中文字体**
   - 即使系统有字体，Qt 也可能使用默认字体
   - QML 组件没有指定字体族

## 解决方案

### ✅ 已完成的修复

1. **setup.sh 自动安装中文字体**
   - `fonts-wqy-zenhei` - 文泉驿正黑
   - `fonts-wqy-microhei` - 文泉驿微米黑
   - `fonts-noto-cjk` - Noto CJK 字体

2. **main.cpp 配置默认字体**
   - 自动检测可用中文字体
   - 设置应用默认字体

3. **QML 文件字体配置**
   - `main.qml` - 添加全局中文字体属性
   - `StatusBar.qml` - 所有 Text 组件使用中文字体
   - `ControlPanel.qml` - 所有 Text 组件使用中文字体
   - `VideoView.qml` - Text 组件使用中文字体

### 字体优先级列表

```javascript
var fonts = [
    "WenQuanYi Zen Hei",      // 文泉驿正黑
    "WenQuanYi Micro Hei",     // 文泉驿微米黑
    "Noto Sans CJK SC",        // Noto Sans 简体中文
    "Noto Sans CJK TC",        // Noto Sans 繁体中文
    "Source Han Sans SC",      // 思源黑体 简体
    "Droid Sans Fallback",     // Droid 回退字体
    "SimHei",                  // 黑体
    "Microsoft YaHei"          // 微软雅黑
]
```

## 验证方法

### 检查字体是否安装

```bash
# 检查字体包
dpkg -l | grep fonts-wqy

# 检查字体缓存
fc-list :lang=zh

# 刷新字体缓存
fc-cache -fv
```

### 检查程序字体配置

运行程序后，查看日志：
```bash
bash run.sh 2>&1 | grep -i font
```

应该看到：
```
Using Chinese font: WenQuanYi Zen Hei
```

## 如果字体仍然不显示

### 方案 1: 手动安装字体

```bash
apt-get update
apt-get install -y fonts-wqy-zenhei fonts-wqy-microhei fonts-noto-cjk
fc-cache -fv
```

### 方案 2: 从宿主机复制字体

```bash
# 在宿主机上
cp /usr/share/fonts/truetype/wqy/*.ttf /workspaces/Remote-Driving/fonts/

# 在容器内
mkdir -p ~/.fonts
cp /workspaces/Remote-Driving/fonts/*.ttf ~/.fonts/
fc-cache -fv
```

### 方案 3: 使用字体文件

在 QML 中使用 FontLoader：
```qml
FontLoader {
    id: chineseFont
    source: "fonts/WenQuanYiZenHei.ttf"
}

Text {
    font.family: chineseFont.name
    text: "中文测试"
}
```

## 修改的文件

1. **setup.sh** - 添加中文字体安装
2. **main.cpp** - 添加字体检测和配置
3. **main.qml** - 添加全局字体属性
4. **StatusBar.qml** - 所有 Text 使用中文字体
5. **ControlPanel.qml** - 所有 Text 使用中文字体
6. **VideoView.qml** - Text 使用中文字体

## 持久化

- ✅ 字体安装已添加到 `setup.sh`
- ✅ 每次容器启动时自动安装
- ✅ QML 字体配置已保存

## 总结

✅ **字体安装已配置** - setup.sh 自动安装
✅ **字体检测已实现** - main.cpp 自动检测
✅ **QML 字体已配置** - 所有组件使用中文字体

**重新构建容器后，中文字体应该可以正常显示！** 🎉
