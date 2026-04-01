import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 车辆选择对话框
 */
Popup {
    id: vehicleSelectionDialog
    
    // 用 QtObject 持有状态，避免 Popup 嵌套层级中 id 作用域导致的 "Cannot assign to non-existent property" 及重复触发
    QtObject {
        id: sessionState
        property bool creating: false
    }
    
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
    width: 600
    height: 650
    modal: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: parent
    
    // 弹出动画
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 300 }
        NumberAnimation { property: "scale"; from: 0.9; to: 1.0; duration: 300; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 200 }
        NumberAnimation { property: "scale"; from: 1.0; to: 0.9; duration: 200 }
    }

    // 背景遮罩
    Rectangle {
        anchors.fill: parent
        color: "#80000000"
    }
    
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#2A2A3E" }
            GradientStop { position: 1.0; color: "#1E1E2E" }
        }
        border.color: "#4A90E2"
        border.width: 2
        radius: 12
        
        // 简单阴影效果
        Rectangle {
            anchors.fill: parent
            anchors.margins: -5
            z: -1
            radius: parent.radius + 5
            color: "#20000000"
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            z: -1
            radius: parent.radius + 3
            color: "#30000000"
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15

            // 标题区域
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8
                
                Text {
                    text: "选择车辆"
                    color: "#FFFFFF"
                    font.pixelSize: 22
                    font.family: vehicleSelectionDialog.chineseFont || font.family
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }
                
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 60
                    height: 3
                    radius: 2
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#4A90E2" }
                        GradientStop { position: 1.0; color: "#50C878" }
                    }
                }
            }

            // 刷新列表 + 退出登录
            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: "刷新列表"
                    onClicked: {
                        if (authManager.isLoggedIn) {
                            var baseUrl = authManager.serverUrl && authManager.serverUrl.length > 0
                                           ? authManager.serverUrl
                                           : "http://localhost:8081"
                            vehicleManager.refreshVehicleList(
                                baseUrl,
                                authManager.authToken
                            )
                        }
                    }
                }

                Button {
                    text: "退出登录"
                    font.family: vehicleSelectionDialog.chineseFont || font.family
                    onClicked: {
                        if (typeof authManager !== "undefined" && authManager) {
                            console.log("[Client][UI] 车辆选择页点击「退出登录」")
                            authManager.logout()
                        }
                        vehicleSelectionDialog.close()
                    }
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: parent.enabled ? "#FFFFFF" : "#666666"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 6
                        color: parent.pressed ? "#8B3A3A" : (parent.hovered ? "#A04444" : "#6B2A2A")
                        border.color: parent.hovered ? "#CC6666" : "#555555"
                        border.width: 1
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            // 车辆列表
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: vehicleListView
                    model: vehicleManager.vehicleList
                    spacing: 5

                    delegate: Rectangle {
                        width: vehicleListView.width
                        height: 70
                        color: vehicleManager.currentVin === modelData ? "#3A4A6A" : (mouseArea.containsMouse ? "#2A3A4A" : "#1A1A2A")
                        border.color: vehicleManager.currentVin === modelData ? "#4A90E2" : "#444444"
                        border.width: vehicleManager.currentVin === modelData ? 2 : 1
                        radius: 8
                        
                        Behavior on color { ColorAnimation { duration: 200 } }
                        Behavior on border.color { ColorAnimation { duration: 200 } }
                        
                        // 选中状态的光晕效果
                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: vehicleManager.currentVin === modelData ? "#204A90E2" : "#00000000" }
                                GradientStop { position: 1.0; color: "#00000000" }
                            }
                            visible: vehicleManager.currentVin === modelData
                        }
                        
                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                vehicleManager.selectVehicle(modelData)
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5

                                Text {
                                    text: vehicleManager.getVehicleInfo(modelData).name || modelData
                                    color: "#ffffff"
                                    font.pixelSize: 14
                font.family: vehicleSelectionDialog.chineseFont || font.family
                                    font.bold: true
                                }

                                Text {
                                    text: "VIN: " + modelData
                                    color: "#aaaaaa"
                                    font.pixelSize: 12
                font.family: vehicleSelectionDialog.chineseFont || font.family
                                }
                            }

                            Button {
                                text: vehicleManager.currentVin === modelData ? "✓ 已选择" : "选择"
                                enabled: vehicleManager.currentVin !== modelData
                                
                                contentItem: Text {
                                    text: parent.text
                                    font.pixelSize: 13
                                    font.family: vehicleSelectionDialog.chineseFont || font.family
                                    font.bold: true
                                    color: parent.enabled ? "#FFFFFF" : "#666666"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                
                                background: Rectangle {
                                    radius: 6
                                    gradient: Gradient {
                                        GradientStop { 
                                            position: 0.0
                                            color: parent.parent.enabled ? (parent.parent.pressed ? "#357ABD" : (parent.parent.hovered ? "#5AA0F2" : "#4A90E2"))
                                            : "#2A2A3E"
                                        }
                                        GradientStop { 
                                            position: 1.0
                                            color: parent.parent.enabled ? (parent.parent.pressed ? "#2A5A8D" : (parent.parent.hovered ? "#4A80D2" : "#357ABD"))
                                            : "#1E1E2E"
                                        }
                                    }
                                    border.color: parent.parent.enabled ? "#5AA0F2" : "#444444"
                                    border.width: 1
                                }
                                
                                onClicked: {
                                    vehicleManager.selectVehicle(modelData)
                                }
                            }
                        }

                    }
                }
            }

            // 当前选择的车辆信息
            Rectangle {
                Layout.fillWidth: true
                height: 50
                color: "#1a1a1a"
                border.color: "#444444"
                border.width: 1
                radius: 3
                visible: vehicleManager.currentVin.length > 0

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10

                    Text {
                        text: "当前车辆:"
                        color: "#ffffff"
                        font.pixelSize: 12
                font.family: vehicleSelectionDialog.chineseFont || font.family
                    }

                    Text {
                        Layout.fillWidth: true
                        text: vehicleManager.currentVehicleName || vehicleManager.currentVin
                        color: "#00ff00"
                        font.pixelSize: 12
                font.family: vehicleSelectionDialog.chineseFont || font.family
                        font.bold: true
                    }
                }
            }

            // 错误信息
            Text {
                id: errorText
                Layout.fillWidth: true
                color: "#ff0000"
                font.pixelSize: 12
                font.family: vehicleSelectionDialog.chineseFont || font.family
                visible: text.length > 0
                wrapMode: Text.WordWrap
            }

            // 会话信息显示区域（美化样式）
            Rectangle {
                Layout.fillWidth: true
                height: sessionInfoColumn.implicitHeight + 24
                color: "#1A2A3A"
                border.color: "#4A90E2"
                border.width: 1
                radius: 8
                visible: vehicleManager.lastSessionId && vehicleManager.lastSessionId.length > 0
                
                ColumnLayout {
                    id: sessionInfoColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    
                    Text {
                        text: "📋 会话信息"
                        color: "#4A90E2"
                        font.pixelSize: 14
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                        font.bold: true
                    }
                
                    Text {
                        Layout.fillWidth: true
                        text: "会话 ID: " + (vehicleManager.lastSessionId || "")
                        color: "#FFFFFF"
                        font.pixelSize: 11
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                        wrapMode: Text.Wrap
                    }
                    
                    Text {
                        Layout.fillWidth: true
                        text: "WHIP URL: " + (vehicleManager.lastWhipUrl || "")
                        color: "#B0B0B0"
                        font.pixelSize: 10
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                        wrapMode: Text.Wrap
                    }
                    
                    Text {
                        Layout.fillWidth: true
                        text: "WHEP URL: " + (vehicleManager.lastWhepUrl || "")
                        color: "#B0B0B0"
                        font.pixelSize: 10
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                        wrapMode: Text.Wrap
                    }
                    
                    Text {
                        Layout.fillWidth: true
                        text: "控制协议: " + (vehicleManager.lastControlConfig.algo || "N/A")
                        color: "#B0B0B0"
                        font.pixelSize: 10
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                    }
                }
            }
            
            // 创建会话按钮（美化样式）
            Button {
                Layout.fillWidth: true
                height: 48
                text: sessionState.creating ? "创建中..." : "创建会话"
                enabled: vehicleManager.currentVin.length > 0 && !sessionState.creating
                
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 16
                    font.family: vehicleSelectionDialog.chineseFont || font.family
                    font.bold: true
                    color: parent.enabled ? "#FFFFFF" : "#666666"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                background: Rectangle {
                    radius: 8
                    gradient: Gradient {
                        GradientStop { 
                            position: 0.0
                            color: parent.parent.enabled ? (parent.parent.pressed ? "#357ABD" : (parent.parent.hovered ? "#5AA0F2" : "#4A90E2"))
                            : "#2A2A3E"
                        }
                        GradientStop { 
                            position: 1.0
                            color: parent.parent.enabled ? (parent.parent.pressed ? "#2A5A8D" : (parent.parent.hovered ? "#4A80D2" : "#357ABD"))
                            : "#1E1E2E"
                        }
                    }
                    border.color: parent.parent.enabled ? "#5AA0F2" : "#444444"
                    border.width: 1
                    
                    // 简单阴影
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        z: -1
                        radius: parent.radius + 2
                        color: parent.parent.parent.enabled ? "#30000000" : "#00000000"
                    }
                }
                
                onClicked: {
                    if (vehicleManager.currentVin.length > 0) {
                        sessionState.creating = true
                        var baseUrl = authManager.serverUrl && authManager.serverUrl.length > 0
                                       ? authManager.serverUrl
                                       : "http://localhost:8081"
                        vehicleManager.startSessionForCurrentVin(baseUrl, authManager.authToken)
                    }
                }
            }
            
            // 确认按钮：进入远程驾驶主页面（美化样式）
            Button {
                Layout.fillWidth: true
                height: 48
                text: sessionState.creating ? "创建中..." : "确认并进入驾驶"
                enabled: vehicleManager.currentVin.length > 0 && !sessionState.creating
                
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 16
                    font.family: vehicleSelectionDialog.chineseFont || font.family
                    font.bold: true
                    color: parent.enabled ? "#FFFFFF" : "#666666"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                background: Rectangle {
                    radius: 8
                    gradient: Gradient {
                        GradientStop { 
                            position: 0.0
                            color: parent.parent.enabled ? (parent.parent.pressed ? "#2A8D5A" : (parent.parent.hovered ? "#60D888" : "#50C878"))
                            : "#2A2A3E"
                        }
                        GradientStop { 
                            position: 1.0
                            color: parent.parent.enabled ? (parent.parent.pressed ? "#1A6D3A" : (parent.parent.hovered ? "#50C868" : "#40B868"))
                            : "#1E1E2E"
                        }
                    }
                    border.color: parent.parent.enabled ? "#60D888" : "#444444"
                    border.width: 1
                    
                    // 简单阴影
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        z: -1
                        radius: parent.radius + 2
                        color: parent.parent.parent.enabled ? "#30000000" : "#00000000"
                    }
                }
                
                onClicked: {
                    if (vehicleManager.currentVin.length > 0) {
                        if (sessionState.creating) return
                        console.log("[Client][选车] 确认并进入驾驶：先创建会话 vin=" + vehicleManager.currentVin)
                        sessionState.creating = true
                        var baseUrl = authManager.serverUrl && authManager.serverUrl.length > 0
                                       ? authManager.serverUrl
                                       : "http://localhost:8081"
                        vehicleManager.startSessionForCurrentVin(baseUrl, authManager.authToken)
                    }
                }
            }
        }
    }

    Connections {
        target: vehicleManager
        
        function onVehicleListLoadFailed(error) {
            errorText.text = error
        }
        
        function onVehicleListLoaded(vehicles) {
            errorText.text = ""
            if (vehicles.length === 0) {
                errorText.text = "没有可用的车辆"
            }
        }
        
        function onSessionCreated(sessionId, whipUrl, whepUrl, controlConfig) {
            errorText.text = ""
            sessionState.creating = false
            console.log("[Client][选车] 会话创建成功 vin=" + vehicleManager.currentVin + " sessionId=" + sessionId)
            console.log("[Client][选车] whepUrl 已设置，连接车端时将用此拉流；mqtt_broker 来自 controlConfig")
            // 延迟到下一事件循环再关闭，避免在 C++ 信号回调栈中同步 close 导致重入/布局重算卡死主线程
            Qt.callLater(function() { vehicleSelectionDialog.close() })
        }
        
        function onSessionCreateFailed(error) {
            errorText.text = "创建会话失败: " + error
            sessionState.creating = false
            console.log("[Client][选车] 会话创建失败:", error)
        }
    }

    Component.onCompleted: {
        // 延迟加载车辆列表，避免初始化阶段调用 C++ 导致崩溃
        loadListTimer.start()
    }
    Timer {
        id: loadListTimer
        interval: 200
        onTriggered: {
            if (authManager.isLoggedIn) {
                var baseUrl = authManager.serverUrl && authManager.serverUrl.length > 0
                               ? authManager.serverUrl
                               : "http://localhost:8081"
                vehicleManager.loadVehicleList(
                    baseUrl,
                    authManager.authToken
                )
            }
        }
    }
}
