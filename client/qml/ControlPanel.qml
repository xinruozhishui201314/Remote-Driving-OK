import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 控制面板组件
 * 包含车辆控制控件（方向盘、油门、刹车等）
 */
Rectangle {
    id: controlPanel
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#1E1E2E" }
        GradientStop { position: 1.0; color: "#0F0F1A" }
    }
    border.color: "#2A2A3E"
    border.width: 1
    
    // 方向盘角度信号（用于同步到DrivingInterface）
    signal steeringAngleChanged(real angle)

    function videoStreamsConnected() {
        if (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected)
            return true
        if (typeof webrtcClient !== "undefined" && webrtcClient && webrtcClient.isConnected)
            return true
        return false
    }

    // ── 控制指令节流 ──────────────────────────────────────────────
    // 三路 Slider 共用一个 50ms (20Hz) 节流定时器，发送合并 driveCommand。
    // 避免 stepSize=0.01 拖动时产生 200+条/秒 MQTT 发布导致主线程卡死。
    property real  _pendingSteering: 0.0
    property real  _pendingThrottle: 0.0
    property real  _pendingBrake:    0.0
    property bool  _driveChanged:    false

    function _notifySafety() {
        if (typeof safetyMonitor !== "undefined" && safetyMonitor && typeof safetyMonitor.notifyOperatorActivity === "function")
            safetyMonitor.notifyOperatorActivity()
    }

    // 是否可以通过 VehicleControlService 发送指令
    function _canUseVCS() {
        return typeof vehicleControl !== "undefined" && vehicleControl
            && typeof vehicleControl.sendDriveCommand === "function"
    }

    Timer {
        id: driveThrottleTimer
        interval: 50   // 20 Hz，满足远驾实时性要求
        repeat:   true
        running:  false
        onTriggered: {
            // 无论数值是否变化，持续握持时仍须刷新死手计时器
            controlPanel._notifySafety()
            if (!controlPanel._driveChanged) return
            controlPanel._driveChanged = false
            if (controlPanel._canUseVCS()) {
                vehicleControl.sendDriveCommand(
                    controlPanel._pendingSteering,
                    controlPanel._pendingThrottle,
                    controlPanel._pendingBrake
                )
                return
            }
            // 降级路径：直接走 mqttController（保留向后兼容）
            if (typeof mqttController === "undefined" || !mqttController) return
            if (!mqttController.isConnected) return
            mqttController.sendDriveCommand(
                controlPanel._pendingSteering,
                controlPanel._pendingThrottle,
                controlPanel._pendingBrake
            )
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
            vehicleControl.sendDriveCommand(_pendingSteering, _pendingThrottle, _pendingBrake)
            return
        }
        if (typeof mqttController === "undefined" || !mqttController) return
        if (!mqttController.isConnected) return
        mqttController.sendDriveCommand(_pendingSteering, _pendingThrottle, _pendingBrake)
    }
    // ─────────────────────────────────────────────────────────────

    // 中文字体（从主窗口继承或使用默认）
    property string chineseFont: {
        if (typeof window !== "undefined" && window.chineseFont) {
            return window.chineseFont
        }
        var fonts = ["WenQuanYi Zen Hei", "WenQuanYi Micro Hei", "Noto Sans CJK SC"]
        var availableFonts = Qt.fontFamilies()
        for (var i = 0; i < fonts.length; i++) {
            if (availableFonts.indexOf(fonts[i]) !== -1) {
                return fonts[i]
            }
        }
        return ""
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 20
        clip: true

        ColumnLayout {
            width: controlPanel.width - 40
            spacing: 24

            // 标题区域（美化）
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                
                Text {
                    Layout.fillWidth: true
                    text: "🚛 车辆控制"
                    color: "#FFFFFF"
                    font.pixelSize: 24
                    font.bold: true
                    font.family: controlPanel.chineseFont || font.family
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    height: 2
                    radius: 1
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#4A90E2" }
                        GradientStop { position: 1.0; color: "#50C878" }
                    }
                }
            }

            // 连接状态（美化）
            Rectangle {
                Layout.fillWidth: true
                height: 80
                radius: 12
                gradient: Gradient {
                    GradientStop { 
                        position: 0.0
                        color: {
                            if (typeof vehicleStatus !== "undefined" && vehicleStatus && vehicleStatus.connectionStatus === "已连接") {
                                return "#1A4A2A"
                            }
                            return "#4A1A1A"
                        }
                    }
                    GradientStop { 
                        position: 1.0
                        color: {
                            if (typeof vehicleStatus !== "undefined" && vehicleStatus && vehicleStatus.connectionStatus === "已连接") {
                                return "#0A2A1A"
                            }
                            return "#2A0A0A"
                        }
                    }
                }
                border.color: {
                    if (typeof vehicleStatus !== "undefined" && vehicleStatus && vehicleStatus.connectionStatus === "已连接") {
                        return "#50C878"
                    }
                    return "#FF6B6B"
                }
                border.width: 2

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        
                        // 状态指示器
                        Rectangle {
                            width: 12
                            height: 12
                            radius: 6
                            color: {
                                if (typeof vehicleStatus !== "undefined" && vehicleStatus && vehicleStatus.connectionStatus === "已连接") {
                                    return "#50C878"
                                }
                                return "#FF6B6B"
                            }
                        }
                        
                        Text {
                            Layout.fillWidth: true
                            text: "连接状态: " + ((typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.connectionStatus : "未连接")
                            color: "#FFFFFF"
                            font.pixelSize: 16
                            font.bold: true
                            font.family: controlPanel.chineseFont || font.family
                        }
                    }
                    
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15
                        
                        // 视频状态
                        RowLayout {
                            spacing: 6
                            
                            Text {
                                text: {
                                    return controlPanel.videoStreamsConnected() ? "✓" : "✗"
                                }
                                color: {
                                    return controlPanel.videoStreamsConnected() ? "#50C878" : "#FF6B6B"
                                }
                                font.pixelSize: 14
                                font.bold: true
                            }
                            
                            Text {
                                text: "视频"
                                color: "#B0B0B0"
                                font.pixelSize: 12
                                font.family: controlPanel.chineseFont || font.family
                            }
                        }
                        
                        Rectangle {
                            width: 1
                            height: 16
                            color: "#444444"
                        }
                        
                        // MQTT 状态
                        RowLayout {
                            spacing: 6
                            
                            Text {
                                text: {
                                    var mqtt = typeof mqttController !== "undefined" && mqttController && mqttController.isConnected
                                    return mqtt ? "✓" : "✗"
                                }
                                color: {
                                    var mqtt = typeof mqttController !== "undefined" && mqttController && mqttController.isConnected
                                    return mqtt ? "#50C878" : "#FF6B6B"
                                }
                                font.pixelSize: 14
                                font.bold: true
                            }
                            
                            Text {
                                text: "控制"
                                color: "#B0B0B0"
                                font.pixelSize: 12
                                font.family: controlPanel.chineseFont || font.family
                            }
                        }
                    }
                }
            }

            // 车辆状态信息（美化）
            Rectangle {
                Layout.fillWidth: true
                height: statusGrid.implicitHeight + 30
                color: "#1A1A2A"
                border.color: "#4A90E2"
                border.width: 1
                radius: 12
                
                ColumnLayout {
                    id: statusGrid
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    Text {
                        text: "📊 车辆状态"
                        color: "#4A90E2"
                        font.pixelSize: 16
                        font.bold: true
                        font.family: controlPanel.chineseFont || font.family
                    }
                    
                    // 速度显示
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15
                        
                        Text { 
                            text: "速度:"
                            color: "#B0B0B0"
                            font.pixelSize: 14
                            font.family: controlPanel.chineseFont || font.family
                        }
                        
                        Text { 
                            Layout.fillWidth: true
                            text: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.speed.toFixed(1) + " km/h" : "0.0 km/h"
                            color: "#50C878"
                            font.pixelSize: 18
                            font.bold: true
                            font.family: controlPanel.chineseFont || font.family
                        }
                    }
                    
                    // 电池显示
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15
                            
                            Text { 
                                text: "电池:"
                                color: "#B0B0B0"
                                font.pixelSize: 14
                                font.family: controlPanel.chineseFont || font.family
                            }
                            
                            Text {
                                Layout.fillWidth: true
                                text: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.batteryLevel.toFixed(0) + "%" : "100%"
                                color: {
                                    if (typeof vehicleStatus !== "undefined" && vehicleStatus && vehicleStatus.batteryLevel > 20) {
                                        return "#50C878"
                                    }
                                    return "#FF6B6B"
                                }
                                font.pixelSize: 16
                                font.bold: true
                                font.family: controlPanel.chineseFont || font.family
                            }
                        }
                        
                        ProgressBar {
                            Layout.fillWidth: true
                            height: 8
                            value: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? (vehicleStatus.batteryLevel / 100.0) : 1.0
                            from: 0
                            to: 1
                            
                            background: Rectangle {
                                radius: 4
                                color: "#1A1A2A"
                                border.color: "#444444"
                                border.width: 1
                            }
                            
                            contentItem: Item {
                                Rectangle {
                                    width: parent.parent.visualPosition * parent.width
                                    height: parent.height
                                    radius: 4
                                    gradient: Gradient {
                                        GradientStop { 
                                            position: 0.0
                                            color: {
                                                var level = (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.batteryLevel : 100
                                                return level > 20 ? "#50C878" : "#FF6B6B"
                                            }
                                        }
                                        GradientStop { 
                                            position: 1.0
                                            color: {
                                                var level = (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.batteryLevel : 100
                                                return level > 20 ? "#40B868" : "#FF5555"
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    // 里程显示
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15
                        
                        Text { 
                            text: "里程:"
                            color: "#B0B0B0"
                            font.pixelSize: 14
                            font.family: controlPanel.chineseFont || font.family
                        }
                        
                        Text { 
                            Layout.fillWidth: true
                            text: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.odometer.toFixed(1) + " km" : "0.0 km"
                            color: "#4A90E2"
                            font.pixelSize: 16
                            font.bold: true
                            font.family: controlPanel.chineseFont || font.family
                        }
                    }
                    
                    // 电压和电流（同一行）
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15
                        
                        Text { 
                            text: "电压:"
                            color: "#B0B0B0"
                            font.pixelSize: 14
                            font.family: controlPanel.chineseFont || font.family
                        }
                        
                        Text { 
                            Layout.fillWidth: true
                            text: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.voltage.toFixed(1) + " V" : "48.0 V"
                            color: "#50C878"
                            font.pixelSize: 16
                            font.bold: true
                            font.family: controlPanel.chineseFont || font.family
                        }
                        
                        Text { 
                            text: "电流:"
                            color: "#B0B0B0"
                            font.pixelSize: 14
                            font.family: controlPanel.chineseFont || font.family
                        }
                        
                        Text { 
                            Layout.fillWidth: true
                            text: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.current.toFixed(1) + " A" : "0.0 A"
                            color: "#FFAA00"
                            font.pixelSize: 16
                            font.bold: true
                            font.family: controlPanel.chineseFont || font.family
                        }
                    }
                    
                    // 温度显示
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15
                        
                        Text { 
                            text: "温度:"
                            color: "#B0B0B0"
                            font.pixelSize: 14
                            font.family: controlPanel.chineseFont || font.family
                        }
                        
                        Text { 
                            Layout.fillWidth: true
                            text: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.temperature.toFixed(1) + " °C" : "25.0 °C"
                            color: {
                                if (typeof vehicleStatus !== "undefined" && vehicleStatus) {
                                    var temp = vehicleStatus.temperature
                                    if (temp > 60) return "#FF6B6B"  // 高温红色
                                    if (temp < 0) return "#4A90E2"   // 低温蓝色
                                    return "#50C878"  // 正常绿色
                                }
                                return "#50C878"
                            }
                            font.pixelSize: 16
                            font.bold: true
                            font.family: controlPanel.chineseFont || font.family
                        }
                    }
                }
            }

            // 方向盘控制（美化）
            Rectangle {
                Layout.fillWidth: true
                height: steeringColumn.implicitHeight + 30
                color: "#1A1A2A"
                border.color: "#4A90E2"
                border.width: 1
                radius: 12
                
                ColumnLayout {
                    id: steeringColumn
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    Text {
                        text: "🎮 方向盘"
                        color: "#4A90E2"
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
                            color: "#2A2A3E"
                            border.color: "#444444"
                            border.width: 1
                            
                            Rectangle {
                                width: steeringSlider.visualPosition * parent.width
                                height: parent.height
                                radius: 3
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#4A90E2" }
                                    GradientStop { position: 1.0; color: "#357ABD" }
                                }
                            }
                        }
                        
                        handle: Rectangle {
                            x: steeringSlider.leftPadding + steeringSlider.visualPosition * (steeringSlider.availableWidth - width)
                            y: steeringSlider.topPadding + steeringSlider.availableHeight / 2 - height / 2
                            implicitWidth: 20
                            implicitHeight: 20
                            radius: 10
                            color: steeringSlider.pressed ? "#5AA0F2" : "#4A90E2"
                            border.color: "#5AA0F2"
                            border.width: 2
                        }
                        
                        onValueChanged: {
                            // 方向盘角度变化信号（供 DrivingInterface 内部使用，无 MQTT，即时响应）
                            controlPanel.steeringAngleChanged(value * 100)
                            // 节流发送合并驱动指令
                            controlPanel._onDriveSliderChanged(value,
                                throttleSlider.value, brakeSlider.value)
                        }
                        onPressedChanged: {
                            // 松手时立即发送最终值，避免定时器延迟
                            if (!pressed) controlPanel._sendDriveImmediate()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "角度: " + (steeringSlider.value * 100).toFixed(0) + "%"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        font.family: controlPanel.chineseFont || font.family
                    }
                }
            }

            // 油门控制（美化）
            Rectangle {
                Layout.fillWidth: true
                height: throttleColumn.implicitHeight + 30
                color: "#1A1A2A"
                border.color: "#50C878"
                border.width: 1
                radius: 12
                
                ColumnLayout {
                    id: throttleColumn
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    Text {
                        text: "⚡ 油门"
                        color: "#50C878"
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
                            color: "#2A2A3E"
                            border.color: "#444444"
                            border.width: 1
                            
                            Rectangle {
                                width: throttleSlider.visualPosition * parent.width
                                height: parent.height
                                radius: 3
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#50C878" }
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
                            color: throttleSlider.pressed ? "#60D888" : "#50C878"
                            border.color: "#60D888"
                            border.width: 2
                        }
                        
                        onValueChanged: {
                            controlPanel._onDriveSliderChanged(
                                steeringSlider.value, value, brakeSlider.value)
                        }
                        onPressedChanged: {
                            if (!pressed) controlPanel._sendDriveImmediate()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "油门: " + (throttleSlider.value * 100).toFixed(0) + "%"
                        color: "#50C878"
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        font.family: controlPanel.chineseFont || font.family
                    }
                }
            }

            // 刹车控制（美化）
            Rectangle {
                Layout.fillWidth: true
                height: brakeColumn.implicitHeight + 30
                color: "#1A1A2A"
                border.color: "#FF6B6B"
                border.width: 1
                radius: 12
                
                ColumnLayout {
                    id: brakeColumn
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    Text {
                        text: "🛑 刹车"
                        color: "#FF6B6B"
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
                            color: "#2A2A3E"
                            border.color: "#444444"
                            border.width: 1
                            
                            Rectangle {
                                width: brakeSlider.visualPosition * parent.width
                                height: parent.height
                                radius: 3
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#FF6B6B" }
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
                            color: brakeSlider.pressed ? "#FF8888" : "#FF6B6B"
                            border.color: "#FF8888"
                            border.width: 2
                        }
                        
                        onValueChanged: {
                            controlPanel._onDriveSliderChanged(
                                steeringSlider.value, throttleSlider.value, value)
                        }
                        onPressedChanged: {
                            if (!pressed) controlPanel._sendDriveImmediate()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "刹车: " + (brakeSlider.value * 100).toFixed(0) + "%"
                        color: "#FF6B6B"
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        font.family: controlPanel.chineseFont || font.family
                    }
                }
            }

            // 档位控制（美化）
            Rectangle {
                Layout.fillWidth: true
                height: gearColumn.implicitHeight + 30
                color: "#1A1A2A"
                border.color: "#4A90E2"
                border.width: 1
                radius: 12
                
                ColumnLayout {
                    id: gearColumn
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    Text {
                        text: "⚙️ 档位"
                        color: "#4A90E2"
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
                            enabled: typeof mqttController !== "undefined" && mqttController && mqttController.isConnected
                            
                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: 18
                                font.bold: true
                                color: parent.enabled ? "#FFFFFF" : "#666666"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            
                            background: Rectangle {
                                radius: 8
                                color: parent.enabled ? (parent.pressed ? "#FF6B6B" : (parent.hovered ? "#FF8888" : "#FF5555")) : "#2A2A3E"
                                border.color: parent.enabled ? "#FF8888" : "#444444"
                                border.width: 1
                            }
                            
                            onClicked: {
                                controlPanel._notifySafety()
                                if (typeof vehicleControl !== "undefined" && vehicleControl) {
                                    vehicleControl.sendUiCommand("gear", {"value": -1})
                                } else if (typeof mqttController !== "undefined" && mqttController) {
                                    mqttController.sendGearCommand(-1)
                                }
                            }
                        }
                        
                        Button {
                            text: "N"
                            Layout.fillWidth: true
                            height: 50
                            enabled: typeof mqttController !== "undefined" && mqttController && mqttController.isConnected
                            
                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: 18
                                font.bold: true
                                color: parent.enabled ? "#FFFFFF" : "#666666"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            
                            background: Rectangle {
                                radius: 8
                                color: parent.enabled ? (parent.pressed ? "#B0B0B0" : (parent.hovered ? "#C0C0C0" : "#888888")) : "#2A2A3E"
                                border.color: parent.enabled ? "#C0C0C0" : "#444444"
                                border.width: 1
                            }
                            
                            onClicked: {
                                controlPanel._notifySafety()
                                if (typeof vehicleControl !== "undefined" && vehicleControl) {
                                    vehicleControl.sendUiCommand("gear", {"value": 0})
                                } else if (typeof mqttController !== "undefined" && mqttController) {
                                    mqttController.sendGearCommand(0)
                                }
                            }
                        }
                        
                        Button {
                            text: "D"
                            Layout.fillWidth: true
                            height: 50
                            enabled: typeof mqttController !== "undefined" && mqttController && mqttController.isConnected
                            
                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: 18
                                font.bold: true
                                color: parent.enabled ? "#FFFFFF" : "#666666"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            
                            background: Rectangle {
                                radius: 8
                                color: parent.enabled ? (parent.pressed ? "#357ABD" : (parent.hovered ? "#5AA0F2" : "#4A90E2")) : "#2A2A3E"
                                border.color: parent.enabled ? "#5AA0F2" : "#444444"
                                border.width: 1
                            }
                            
                            onClicked: {
                                controlPanel._notifySafety()
                                if (typeof vehicleControl !== "undefined" && vehicleControl) {
                                    vehicleControl.sendUiCommand("gear", {"value": 1})
                                } else if (typeof mqttController !== "undefined" && mqttController) {
                                    mqttController.sendGearCommand(1)
                                }
                            }
                        }
                    }
                }
            }

            // 紧急停止按钮（美化）
            Button {
                Layout.fillWidth: true
                height: 64
                text: "🛑 紧急停止"
                
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 20
                    font.bold: true
                    font.family: controlPanel.chineseFont || font.family
                    color: parent.enabled ? "#FFFFFF" : "#666666"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                background: Rectangle {
                    radius: 12
                    gradient: Gradient {
                        GradientStop { 
                            position: 0.0
                            color: parent.parent.enabled ? (parent.parent.pressed ? "#CC0000" : (parent.parent.hovered ? "#FF3333" : "#FF0000"))
                            : "#2A2A3E"
                        }
                        GradientStop { 
                            position: 1.0
                            color: parent.parent.enabled ? (parent.parent.pressed ? "#AA0000" : (parent.parent.hovered ? "#FF1111" : "#DD0000"))
                            : "#1E1E2E"
                        }
                    }
                    border.color: parent.parent.enabled ? "#FF3333" : "#444444"
                    border.width: 2
                    
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        z: -1
                        radius: parent.radius + 3
                        color: parent.parent.parent.enabled ? "#40000000" : "#00000000"
                    }
                }
                
                enabled: (typeof vehicleControl !== "undefined" && vehicleControl)
                      || (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected)
                onClicked: {
                    controlPanel._notifySafety()
                    brakeSlider.value = 1.0
                    throttleSlider.value = 0.0
                    if (typeof vehicleControl !== "undefined" && vehicleControl) {
                        vehicleControl.requestEmergencyStop()
                    } else if (typeof mqttController !== "undefined" && mqttController) {
                        mqttController.sendBrakeCommand(1.0)
                        mqttController.sendThrottleCommand(0.0)
                    }
                }
            }
        }
    }
}
