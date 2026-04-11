import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "styles" as ThemeModule

/**
 * 控制面板组件
 * 包含车辆控制控件（方向盘，油门、刹车等）
 * 统一使用 AppContext 和 Theme
 */
Rectangle {
    id: controlPanel
    
    // ── 统一属性 ─────────────────────────────────────────────────────
    readonly property var vehicleControl: AppContext.vehicleControl
    readonly property var mqttController: AppContext.mqttController
    readonly property var vehicleStatus: AppContext.vehicleStatus
    readonly property string chineseFont: AppContext.chineseFont
    
    function videoStreamsConnected() {
        return AppContext.videoStreamsConnected
    }
    
    // ── 样式 ────────────────────────────────────────────────────────────
    gradient: Gradient {
        GradientStop { position: 0.0; color: ThemeModule.Theme.colorSurface }
        GradientStop { position: 1.0; color: ThemeModule.Theme.colorBackground }
    }
    border.color: ThemeModule.Theme.colorBorder
    border.width: 1
    
    // ── 控制指令节流 ──────────────────────────────────────────────
    property real  _pendingSteering: 0.0
    property real  _pendingThrottle: 0.0
    property real  _pendingBrake:    0.0
    property bool  _driveChanged:    false

    function _notifySafety() {
        var sm = AppContext.safetyMonitor
        if (sm && typeof sm.notifyOperatorActivity === "function")
            sm.notifyOperatorActivity()
    }

    function _canUseVCS() {
        return controlPanel.vehicleControl
            && typeof controlPanel.vehicleControl.sendDriveCommand === "function"
    }

    Timer {
        id: driveThrottleTimer
        interval: 50   // 20 Hz
        repeat:   true
        running:  false
        onTriggered: {
            controlPanel._notifySafety()
            if (!controlPanel._driveChanged) return
            controlPanel._driveChanged = false
            if (controlPanel._canUseVCS()) {
                controlPanel.vehicleControl.sendDriveCommand(
                    controlPanel._pendingSteering,
                    controlPanel._pendingThrottle,
                    controlPanel._pendingBrake)
                return
            }
            console.warn("[Client][Control][ControlPanel] vehicleControl missing; drive command dropped")
        }
    }

    function _onDriveSliderChanged(steering, throttle, brake) {
        _pendingSteering = steering
        _pendingThrottle = throttle
        _pendingBrake    = brake
        _driveChanged    = true
        if (!driveThrottleTimer.running) driveThrottleTimer.start()
    }

    function _sendDriveImmediate() {
        _driveChanged = false
        _notifySafety()
        if (_canUseVCS()) {
            controlPanel.vehicleControl.sendDriveCommand(_pendingSteering, _pendingThrottle, _pendingBrake)
            return
        }
        console.warn("[Client][Control][ControlPanel] vehicleControl missing; immediate drive dropped")
    }

    // 方向盘角度信号
    signal steeringAngleChanged(real angle)

    ScrollView {
        anchors.fill: parent
        anchors.margins: 20
        clip: true

        ColumnLayout {
            width: controlPanel.width - 40
            spacing: 24

            // 标题区域
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                
                Text {
                    Layout.fillWidth: true
                    text: "🚛 车辆控制"
                    color: ThemeModule.Theme.colorText
                    font.pixelSize: 24
                    font.bold: true
                    font.family: controlPanel.chineseFont || font.family
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    height: 2
                    radius: 1
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: ThemeModule.Theme.colorBorderActive }
                        GradientStop { position: 1.0; color: ThemeModule.Theme.colorAccent }
                    }
                }
            }

            // 连接状态
            Rectangle {
                Layout.fillWidth: true
                height: 80
                radius: 12
                gradient: Gradient {
                    GradientStop { 
                        position: 0.0
                        color: {
                            var vs = controlPanel.vehicleStatus
                            if (vs && vs.connectionStatus === "已连接") {
                                return ThemeModule.Theme.colorGood + "33"  // 带透明度
                            }
                            return ThemeModule.Theme.colorDanger + "33"
                        }
                    }
                    GradientStop { 
                        position: 1.0
                        color: ThemeModule.Theme.colorBackground
                    }
                }
                border.color: {
                    var vs = controlPanel.vehicleStatus
                    if (vs && vs.connectionStatus === "已连接") {
                        return ThemeModule.Theme.colorGood
                    }
                    return ThemeModule.Theme.colorDanger
                }
                border.width: 2

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        
                        Rectangle {
                            width: 12
                            height: 12
                            radius: 6
                            color: {
                                var vs = controlPanel.vehicleStatus
                                if (vs && vs.connectionStatus === "已连接") {
                                    return ThemeModule.Theme.colorGood
                                }
                                return ThemeModule.Theme.colorDanger
                            }
                        }
                        
                        Text {
                            Layout.fillWidth: true
                            text: "连接状态: " + ((controlPanel.vehicleStatus) ? controlPanel.vehicleStatus.connectionStatus : "未连接")
                            color: ThemeModule.Theme.colorText
                            font.pixelSize: 16
                            font.bold: true
                            font.family: controlPanel.chineseFont || font.family
                        }
                    }
                    
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15
                        
                        RowLayout {
                            spacing: 6
                            Text {
                                text: controlPanel.videoStreamsConnected() ? "✓" : "✗"
                                color: controlPanel.videoStreamsConnected() ? ThemeModule.Theme.colorGood : ThemeModule.Theme.colorDanger
                                font.pixelSize: 14
                                font.bold: true
                            }
                            Text {
                                text: "视频"
                                color: ThemeModule.Theme.colorTextDim
                                font.pixelSize: 12
                                font.family: controlPanel.chineseFont || font.family
                            }
                        }
                        
                        Rectangle {
                            width: 1
                            height: 16
                            color: ThemeModule.Theme.colorBorder
                        }
                        
                        RowLayout {
                            spacing: 6
                            Text {
                                text: {
                                    var mc = controlPanel.mqttController
                                    return (mc && mc.isConnected) ? "✓" : "✗"
                                }
                                color: {
                                    var mc = controlPanel.mqttController
                                    return (mc && mc.isConnected) ? ThemeModule.Theme.colorGood : ThemeModule.Theme.colorDanger
                                }
                                font.pixelSize: 14
                                font.bold: true
                            }
                            Text {
                                text: "控制"
                                color: ThemeModule.Theme.colorTextDim
                                font.pixelSize: 12
                                font.family: controlPanel.chineseFont || font.family
                            }
                        }
                    }
                }
            }

            // 车辆状态信息
            Rectangle {
                Layout.fillWidth: true
                height: statusGrid.implicitHeight + 30
                color: ThemeModule.Theme.colorBackground
                border.color: ThemeModule.Theme.colorBorderActive
                border.width: 1
                radius: 12
                
                ColumnLayout {
                    id: statusGrid
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    Text {
                        text: "📊 车辆状态"
                        color: ThemeModule.Theme.colorBorderActive
                        font.pixelSize: 16
                        font.bold: true
                        font.family: controlPanel.chineseFont || font.family
                    }
                    
                    // 速度
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15
                        Text { 
                            text: "速度:"
                            color: ThemeModule.Theme.colorTextDim
                            font.pixelSize: 14
                            font.family: controlPanel.chineseFont || font.family
                        }
                        Text { 
                            Layout.fillWidth: true
                            text: (controlPanel.vehicleStatus) ? controlPanel.vehicleStatus.speed.toFixed(1) + " km/h" : "0.0 km/h"
                            color: ThemeModule.Theme.colorGood
                            font.pixelSize: 18
                            font.bold: true
                            font.family: controlPanel.chineseFont || font.family
                        }
                    }
                    
                    // 电池
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15
                            Text { 
                                text: "电池:"
                                color: ThemeModule.Theme.colorTextDim
                                font.pixelSize: 14
                                font.family: controlPanel.chineseFont || font.family
                            }
                            Text {
                                Layout.fillWidth: true
                                text: (controlPanel.vehicleStatus) ? controlPanel.vehicleStatus.batteryLevel.toFixed(0) + "%" : "100%"
                                color: {
                                    var level = (controlPanel.vehicleStatus) ? controlPanel.vehicleStatus.batteryLevel : 100
                                    return level > 20 ? ThemeModule.Theme.colorGood : ThemeModule.Theme.colorDanger
                                }
                                font.pixelSize: 16
                                font.bold: true
                                font.family: controlPanel.chineseFont || font.family
                            }
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            height: 8
                            value: (controlPanel.vehicleStatus) ? (controlPanel.vehicleStatus.batteryLevel / 100.0) : 1.0
                            from: 0
                            to: 1
                            background: Rectangle {
                                radius: 4
                                color: ThemeModule.Theme.colorBackground
                                border.color: ThemeModule.Theme.colorBorder
                                border.width: 1
                            }
                            contentItem: Item {
                                Rectangle {
                                    width: parent.parent.visualPosition * parent.width
                                    height: parent.height
                                    radius: 4
                                    color: {
                                        var level = (controlPanel.vehicleStatus) ? controlPanel.vehicleStatus.batteryLevel : 100
                                        return level > 20 ? ThemeModule.Theme.colorGood : ThemeModule.Theme.colorDanger
                                    }
                                }
                            }
                        }
                    }
                    
                    // 里程
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15
                        Text { 
                            text: "里程:"
                            color: ThemeModule.Theme.colorTextDim
                            font.pixelSize: 14
                            font.family: controlPanel.chineseFont || font.family
                        }
                        Text { 
                            Layout.fillWidth: true
                            text: (controlPanel.vehicleStatus) ? controlPanel.vehicleStatus.odometer.toFixed(1) + " km" : "0.0 km"
                            color: ThemeModule.Theme.colorBorderActive
                            font.pixelSize: 16
                            font.bold: true
                            font.family: controlPanel.chineseFont || font.family
                        }
                    }
                    
                    // 电压和电流
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15
                        Text { 
                            text: "电压:"
                            color: ThemeModule.Theme.colorTextDim
                            font.pixelSize: 14
                            font.family: controlPanel.chineseFont || font.family
                        }
                        Text { 
                            Layout.fillWidth: true
                            text: (controlPanel.vehicleStatus) ? controlPanel.vehicleStatus.voltage.toFixed(1) + " V" : "48.0 V"
                            color: ThemeModule.Theme.colorGood
                            font.pixelSize: 16
                            font.bold: true
                            font.family: controlPanel.chineseFont || font.family
                        }
                        Text { 
                            text: "电流:"
                            color: ThemeModule.Theme.colorTextDim
                            font.pixelSize: 14
                            font.family: controlPanel.chineseFont || font.family
                        }
                        Text { 
                            Layout.fillWidth: true
                            text: (controlPanel.vehicleStatus) ? controlPanel.vehicleStatus.current.toFixed(1) + " A" : "0.0 A"
                            color: ThemeModule.Theme.colorCaution
                            font.pixelSize: 16
                            font.bold: true
                            font.family: controlPanel.chineseFont || font.family
                        }
                    }
                    
                    // 温度
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15
                        Text { 
                            text: "温度:"
                            color: ThemeModule.Theme.colorTextDim
                            font.pixelSize: 14
                            font.family: controlPanel.chineseFont || font.family
                        }
                        Text { 
                            Layout.fillWidth: true
                            text: (controlPanel.vehicleStatus) ? controlPanel.vehicleStatus.temperature.toFixed(1) + " °C" : "25.0 °C"
                            color: {
                                var temp = (controlPanel.vehicleStatus) ? controlPanel.vehicleStatus.temperature : 25
                                if (temp > 60) return ThemeModule.Theme.colorDanger
                                if (temp < 0) return ThemeModule.Theme.colorBorderActive
                                return ThemeModule.Theme.colorGood
                            }
                            font.pixelSize: 16
                            font.bold: true
                            font.family: controlPanel.chineseFont || font.family
                        }
                    }
                }
            }

            // 方向盘控制
            Rectangle {
                Layout.fillWidth: true
                height: steeringColumn.implicitHeight + 30
                color: ThemeModule.Theme.colorBackground
                border.color: ThemeModule.Theme.colorBorderActive
                border.width: 1
                radius: 12
                
                ColumnLayout {
                    id: steeringColumn
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    Text {
                        text: "🎮 方向盘"
                        color: ThemeModule.Theme.colorBorderActive
                        font.pixelSize: 16
                        font.bold: true
                        font.family: controlPanel.chineseFont || font.family
                    }

                    Slider {
                        id: steeringSlider
                        Layout.fillWidth: true
                        from: -1.0
                        to: 1.0
                        value: 0.0
                        stepSize: 0.01
                        
                        background: Rectangle {
                            x: steeringSlider.leftPadding
                            y: steeringSlider.topPadding + steeringSlider.availableHeight / 2 - height / 2
                            implicitWidth: 200
                            implicitHeight: 6
                            width: steeringSlider.availableWidth
                            height: implicitHeight
                            radius: 3
                            color: ThemeModule.Theme.colorBorder
                            Rectangle {
                                width: steeringSlider.visualPosition * parent.width
                                height: parent.height
                                radius: 3
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: ThemeModule.Theme.colorBorderActive }
                                    GradientStop { position: 1.0; color: ThemeModule.Theme.colorPrimary }
                                }
                            }
                        }
                        
                        handle: Rectangle {
                            x: steeringSlider.leftPadding + steeringSlider.visualPosition * (steeringSlider.availableWidth - width)
                            y: steeringSlider.topPadding + steeringSlider.availableHeight / 2 - height / 2
                            implicitWidth: 20
                            implicitHeight: 20
                            radius: 10
                            color: steeringSlider.pressed ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorPrimary
                            border.color: ThemeModule.Theme.colorBorderActive
                            border.width: 2
                        }
                        
                        onValueChanged: {
                            controlPanel.steeringAngleChanged(value * 100)
                            controlPanel._onDriveSliderChanged(value, throttleSlider.value, brakeSlider.value)
                        }
                        onPressedChanged: {
                            if (!pressed) controlPanel._sendDriveImmediate()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "角度: " + (steeringSlider.value * 100).toFixed(0) + "%"
                        color: ThemeModule.Theme.colorText
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        font.family: controlPanel.chineseFont || font.family
                    }
                }
            }

            // 油门控制
            Rectangle {
                Layout.fillWidth: true
                height: throttleColumn.implicitHeight + 30
                color: ThemeModule.Theme.colorBackground
                border.color: ThemeModule.Theme.colorGood
                border.width: 1
                radius: 12
                
                ColumnLayout {
                    id: throttleColumn
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    Text {
                        text: "⚡ 油门"
                        color: ThemeModule.Theme.colorGood
                        font.pixelSize: 16
                        font.bold: true
                        font.family: controlPanel.chineseFont || font.family
                    }

                    Slider {
                        id: throttleSlider
                        Layout.fillWidth: true
                        from: 0.0
                        to: 1.0
                        value: 0.0
                        stepSize: 0.01
                        
                        background: Rectangle {
                            x: throttleSlider.leftPadding
                            y: throttleSlider.topPadding + throttleSlider.availableHeight / 2 - height / 2
                            implicitWidth: 200
                            implicitHeight: 6
                            width: throttleSlider.availableWidth
                            height: implicitHeight
                            radius: 3
                            color: ThemeModule.Theme.colorBorder
                            Rectangle {
                                width: throttleSlider.visualPosition * parent.width
                                height: parent.height
                                radius: 3
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: ThemeModule.Theme.colorGood }
                                    GradientStop { position: 1.0; color: "#40B868" }
                                }
                            }
                        }
                        
                        handle: Rectangle {
                            x: throttleSlider.leftPadding + throttleSlider.visualPosition * (throttleSlider.availableWidth - width)
                            y: throttleSlider.topPadding + throttleSlider.availableHeight / 2 - height / 2
                            implicitWidth: 20
                            implicitHeight: 20
                            radius: 10
                            color: throttleSlider.pressed ? "#60D888" : ThemeModule.Theme.colorGood
                            border.color: "#60D888"
                            border.width: 2
                        }
                        
                        onValueChanged: {
                            controlPanel._onDriveSliderChanged(steeringSlider.value, value, brakeSlider.value)
                        }
                        onPressedChanged: {
                            if (!pressed) controlPanel._sendDriveImmediate()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "油门: " + (throttleSlider.value * 100).toFixed(0) + "%"
                        color: ThemeModule.Theme.colorGood
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        font.family: controlPanel.chineseFont || font.family
                    }
                }
            }

            // 刹车控制
            Rectangle {
                Layout.fillWidth: true
                height: brakeColumn.implicitHeight + 30
                color: ThemeModule.Theme.colorBackground
                border.color: ThemeModule.Theme.colorDanger
                border.width: 1
                radius: 12
                
                ColumnLayout {
                    id: brakeColumn
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    Text {
                        text: "🛑 刹车"
                        color: ThemeModule.Theme.colorDanger
                        font.pixelSize: 16
                        font.bold: true
                        font.family: controlPanel.chineseFont || font.family
                    }

                    Slider {
                        id: brakeSlider
                        Layout.fillWidth: true
                        from: 0.0
                        to: 1.0
                        value: 0.0
                        stepSize: 0.01
                        
                        background: Rectangle {
                            x: brakeSlider.leftPadding
                            y: brakeSlider.topPadding + brakeSlider.availableHeight / 2 - height / 2
                            implicitWidth: 200
                            implicitHeight: 6
                            width: brakeSlider.availableWidth
                            height: implicitHeight
                            radius: 3
                            color: ThemeModule.Theme.colorBorder
                            Rectangle {
                                width: brakeSlider.visualPosition * parent.width
                                height: parent.height
                                radius: 3
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: ThemeModule.Theme.colorDanger }
                                    GradientStop { position: 1.0; color: "#FF5555" }
                                }
                            }
                        }
                        
                        handle: Rectangle {
                            x: brakeSlider.leftPadding + brakeSlider.visualPosition * (brakeSlider.availableWidth - width)
                            y: brakeSlider.topPadding + brakeSlider.availableHeight / 2 - height / 2
                            implicitWidth: 20
                            implicitHeight: 20
                            radius: 10
                            color: brakeSlider.pressed ? "#FF8888" : ThemeModule.Theme.colorDanger
                            border.color: "#FF8888"
                            border.width: 2
                        }
                        
                        onValueChanged: {
                            controlPanel._onDriveSliderChanged(steeringSlider.value, throttleSlider.value, value)
                        }
                        onPressedChanged: {
                            if (!pressed) controlPanel._sendDriveImmediate()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "刹车: " + (brakeSlider.value * 100).toFixed(0) + "%"
                        color: ThemeModule.Theme.colorDanger
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        font.family: controlPanel.chineseFont || font.family
                    }
                }
            }

            // 档位控制
            Rectangle {
                Layout.fillWidth: true
                height: gearColumn.implicitHeight + 30
                color: ThemeModule.Theme.colorBackground
                border.color: ThemeModule.Theme.colorBorderActive
                border.width: 1
                radius: 12
                
                ColumnLayout {
                    id: gearColumn
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    Text {
                        text: "⚙️ 档位"
                        color: ThemeModule.Theme.colorBorderActive
                        font.pixelSize: 16
                        font.bold: true
                        font.family: controlPanel.chineseFont || font.family
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Button {
                            text: "R"
                            Layout.fillWidth: true
                            height: 50
                            enabled: controlPanel.vehicleControl && controlPanel.mqttController && controlPanel.mqttController.isConnected
                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: 18
                                font.bold: true
                                color: parent.enabled ? ThemeModule.Theme.colorText : "#666666"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 8
                                color: parent.enabled ? (parent.pressed ? ThemeModule.Theme.colorDanger : (parent.hovered ? "#FF8888" : "#FF5555")) : ThemeModule.Theme.colorBorder
                                border.color: parent.enabled ? "#FF8888" : ThemeModule.Theme.colorBorder
                                border.width: 1
                            }
                            onClicked: {
                                controlPanel._notifySafety()
                                var vc = controlPanel.vehicleControl
                                if (vc)
                                    vc.sendUiCommand("gear", {"value": -1})
                            }
                        }
                        
                        Button {
                            text: "N"
                            Layout.fillWidth: true
                            height: 50
                            enabled: controlPanel.vehicleControl && controlPanel.mqttController && controlPanel.mqttController.isConnected
                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: 18
                                font.bold: true
                                color: parent.enabled ? ThemeModule.Theme.colorText : "#666666"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 8
                                color: parent.enabled ? (parent.pressed ? "#C0C0C0" : (parent.hovered ? "#D0D0D0" : "#888888")) : ThemeModule.Theme.colorBorder
                                border.color: parent.enabled ? "#C0C0C0" : ThemeModule.Theme.colorBorder
                                border.width: 1
                            }
                            onClicked: {
                                controlPanel._notifySafety()
                                var vc = controlPanel.vehicleControl
                                if (vc)
                                    vc.sendUiCommand("gear", {"value": 0})
                            }
                        }
                        
                        Button {
                            text: "D"
                            Layout.fillWidth: true
                            height: 50
                            enabled: controlPanel.vehicleControl && controlPanel.mqttController && controlPanel.mqttController.isConnected
                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: 18
                                font.bold: true
                                color: parent.enabled ? ThemeModule.Theme.colorText : "#666666"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 8
                                color: parent.enabled ? (parent.pressed ? ThemeModule.Theme.colorPrimary : (parent.hovered ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorPrimary)) : ThemeModule.Theme.colorBorder
                                border.color: parent.enabled ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorBorder
                                border.width: 1
                            }
                            onClicked: {
                                controlPanel._notifySafety()
                                var vc = controlPanel.vehicleControl
                                if (vc)
                                    vc.sendUiCommand("gear", {"value": 1})
                            }
                        }
                    }
                }
            }

            // 紧急停止按钮
            Button {
                Layout.fillWidth: true
                height: 64
                text: "🛑 紧急停止"
                enabled: controlPanel.vehicleControl && controlPanel.mqttController && controlPanel.mqttController.isConnected
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 20
                    font.bold: true
                    font.family: controlPanel.chineseFont || font.family
                    color: parent.enabled ? ThemeModule.Theme.colorText : "#666666"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 12
                    gradient: Gradient {
                        GradientStop { 
                            position: 0.0
                            color: parent.parent.enabled ? (parent.parent.pressed ? "#CC0000" : (parent.parent.hovered ? "#FF3333" : ThemeModule.Theme.colorDanger))
                            : ThemeModule.Theme.colorBorder
                        }
                        GradientStop { 
                            position: 1.0
                            color: parent.parent.enabled ? (parent.parent.pressed ? "#AA0000" : (parent.parent.hovered ? "#FF1111" : "#DD0000"))
                            : ThemeModule.Theme.colorBackground
                        }
                    }
                    border.color: parent.parent.enabled ? "#FF3333" : ThemeModule.Theme.colorBorder
                    border.width: 2
                }
                onClicked: {
                    controlPanel._notifySafety()
                    brakeSlider.value = 1.0
                    throttleSlider.value = 0.0
                    var vc = controlPanel.vehicleControl
                    if (vc)
                        vc.requestEmergencyStop()
                }
            }
        }
    }
}
