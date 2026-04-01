# 启动崩溃防护性修复说明

## 问题现象

程序在加载 QML 后立即崩溃（Segmentation fault），发生在访问 context properties 时。

## 原因分析

1. **ApplicationWindow title 立即访问 vehicleManager**：在 ApplicationWindow 创建时，title 属性立即计算并访问 `vehicleManager.currentVehicleName`，此时 context properties 可能尚未完全注册。

2. **组件创建时立即访问 context properties**：VideoView、StatusBar、ControlPanel 等组件在创建时立即访问 `webrtcClient`、`mqttController`、`vehicleStatus` 等，如果这些对象未完全初始化会导致崩溃。

3. **缺少空值检查**：QML 中直接访问 context properties 而没有检查它们是否存在。

## 修复方案

### 1. main.qml：延迟计算 title

**修改前**：
```qml
title: "远程驾驶客户端 - " + (vehicleManager.currentVehicleName || "未选择车辆")
```

**修改后**：
```qml
title: windowTitleText
property string windowTitleText: "远程驾驶客户端"

Component.onCompleted: {
    updateTitle()
}

function updateTitle() {
    if (typeof vehicleManager !== "undefined" && vehicleManager) {
        windowTitleText = "远程驾驶客户端 - " + (vehicleManager.currentVehicleName || "未选择车辆")
    } else {
        windowTitleText = "远程驾驶客户端"
    }
}
```

### 2. 所有 QML 组件：添加空值检查

为所有 context property 访问添加 `typeof` 检查：

**修改前**：
```qml
text: webrtcClient.isConnected ? "已连接" : "未连接"
```

**修改后**：
```qml
text: {
    var connected = typeof webrtcClient !== "undefined" && webrtcClient && webrtcClient.isConnected
    return connected ? "已连接" : "未连接"
}
```

### 3. 延迟初始化时间增加

将 `initTimer` 的延迟从 100ms 增加到 200ms，确保所有 context properties 都已注册。

## 修改的文件

1. **client/qml/main.qml**
   - 延迟计算 title
   - 添加空值检查到所有 context property 访问
   - 增加初始化延迟时间

2. **client/qml/VideoView.qml**
   - 所有 `webrtcClient` 访问添加空值检查

3. **client/qml/StatusBar.qml**
   - 所有 context property 访问添加空值检查

4. **client/qml/ControlPanel.qml**
   - 所有 context property 访问添加空值检查

## 防护性检查模式

```qml
// 标准模式
if (typeof obj !== "undefined" && obj && obj.property) {
    // 使用 obj.property
} else {
    // 使用默认值
}

// 简化模式（用于简单属性访问）
var value = (typeof obj !== "undefined" && obj) ? obj.property : defaultValue
```

## 验证方法

```bash
cd /workspaces/Remote-Driving/client
bash run.sh
```

预期：程序启动后不再崩溃，显示登录对话框。

## 总结

✅ **title 延迟计算** - 避免在 ApplicationWindow 创建时立即访问 vehicleManager
✅ **空值检查** - 所有 context property 访问都添加了检查
✅ **延迟初始化** - 增加延迟时间到 200ms
✅ **防护性编程** - 使用 typeof 检查确保对象存在

**所有修改已保存并编译完成！** 🎉
