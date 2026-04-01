# 启动崩溃 (Segmentation fault) 修复说明

## 问题现象

程序在加载 QML 后立即崩溃：
```
Found QML file at: QUrl("file:///workspaces/Remote-Driving/client/qml/main.qml")
qml: Using Chinese font: WenQuanYi Zen Hei
Segmentation fault (core dumped)
```

## 原因分析

常见原因：
1. **QML 组件未完全构建**：在 `Component.onCompleted` 中立即打开对话框或访问子组件时，组件树可能尚未就绪。
2. **过早调用 C++**：VehicleSelectionDialog 的 `Component.onCompleted` 中立即调用 `vehicleManager.loadVehicleList()`，在 QML 引擎初始化阶段可能引发问题。

## 修复方案

### 1. main.qml：延迟打开登录/车辆选择对话框

**修改前**：在 `Component.onCompleted` 中直接根据登录状态打开对话框。

**修改后**：使用 `Timer` 延迟约 100ms 再执行，确保 ApplicationWindow 及子组件（含 LoginDialog、VehicleSelectionDialog）已创建完成。

```qml
Component.onCompleted: {
    initTimer.start()
}
Timer {
    id: initTimer
    interval: 100
    onTriggered: {
        if (!authManager.isLoggedIn) {
            loginDialog.open()
        } else {
            vehicleSelectionDialog.open()
        }
    }
}
```

### 2. VehicleSelectionDialog.qml：延迟加载车辆列表

**修改前**：在 `Component.onCompleted` 中直接调用 `vehicleManager.loadVehicleList()`。

**修改后**：使用 `Timer` 延迟约 200ms 再调用，避免在 QML 创建阶段同步调用 C++ 接口。

```qml
Component.onCompleted: {
    loadListTimer.start()
}
Timer {
    id: loadListTimer
    interval: 200
    onTriggered: {
        if (authManager.isLoggedIn) {
            vehicleManager.loadVehicleList(...)
        }
    }
}
```

## 修改文件

1. **client/qml/main.qml**：增加 `initTimer`，延迟执行打开登录/车辆选择对话框。
2. **client/qml/VehicleSelectionDialog.qml**：增加 `loadListTimer`，延迟执行 `loadVehicleList`。

## 验证

```bash
cd /workspaces/Remote-Driving/client
bash run.sh
```

预期：程序启动后约 100ms 出现登录对话框，不再出现 Segmentation fault。

## 说明

- 延迟时间（100ms / 200ms）仅为保证组件树和引擎就绪，对用户几乎无感知。
- 若仍崩溃，需用 gdb 或 Qt 调试进一步定位栈（是否在 WebRTC/网络/其他 C++ 回调中）。
