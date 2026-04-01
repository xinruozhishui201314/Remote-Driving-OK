import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 车辆选择页面（嵌入主窗口）
 * 用于控制环卫扫地车的车辆选择界面
 * 参考设计图布局
 */
Rectangle {
    id: vehicleSelectionPage
    
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
    
    // 背景渐变
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#1E1E2E" }
        GradientStop { position: 0.5; color: "#2A2A3E" }
        GradientStop { position: 1.0; color: "#1E1E2E" }
    }
    
    // 装饰性背景元素
    Rectangle {
        anchors.fill: parent
        opacity: 0.05
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#4A90E2" }
            GradientStop { position: 1.0; color: "#50C878" }
        }
        rotation: 15
        transformOrigin: Item.TopRight
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 40
        spacing: 30
        
        // 顶部标题区域
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12
            
            RowLayout {
                Layout.fillWidth: true
                
                // 返回按钮（可选）
                Button {
                    text: "← 返回"
                    visible: false  // 暂时隐藏，因为登录后直接进入此页面
                    onClicked: {
                        if (typeof authManager !== "undefined" && authManager) {
                            authManager.logout()
                        }
                    }
                }
                
                Item { Layout.fillWidth: true }
                
                // 刷新按钮
                Button {
                    text: "🔄 刷新列表"
                    onClicked: {
                        if (typeof authManager !== "undefined" && authManager && authManager.isLoggedIn) {
                            var baseUrl = authManager.serverUrl && authManager.serverUrl.length > 0
                                           ? authManager.serverUrl
                                           : "http://localhost:8081"
                            vehicleManager.refreshVehicleList(baseUrl, authManager.authToken)
                        }
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 14
                        font.family: vehicleSelectionPage.chineseFont || font.family
                        color: parent.enabled ? "#FFFFFF" : "#666666"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    background: Rectangle {
                        radius: 6
                        color: parent.enabled ? (parent.pressed ? "#357ABD" : (parent.hovered ? "#5AA0F2" : "#4A90E2")) : "#2A2A3E"
                        border.color: parent.enabled ? "#5AA0F2" : "#444444"
                        border.width: 1
                    }
                }
            }
            
            // 标题
            Text {
                text: "选择车辆"
                color: "#FFFFFF"
                font.pixelSize: 36
                font.family: vehicleSelectionPage.chineseFont || font.family
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            
            // 副标题
            Text {
                text: "请选择要控制的环卫扫地车"
                color: "#B0B0B0"
                font.pixelSize: 16
                font.family: vehicleSelectionPage.chineseFont || font.family
                Layout.alignment: Qt.AlignHCenter
            }
            
            // 装饰线
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 120
                height: 4
                radius: 2
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#4A90E2" }
                    GradientStop { position: 1.0; color: "#50C878" }
                }
            }
        }
        
        // 车辆网格列表（卡片式布局）
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            
            GridView {
                id: vehicleGridView
                anchors.fill: parent
                anchors.margins: 10
                cellWidth: Math.min((parent.width - 20) / 3, 350)
                cellHeight: 240
                model: vehicleManager.vehicleList
                
                delegate: Rectangle {
                    width: vehicleGridView.cellWidth - 20
                    height: vehicleGridView.cellHeight - 20
                    anchors.horizontalCenter: undefined
                    anchors.verticalCenter: undefined
                    radius: 16
                    color: vehicleManager.currentVin === modelData ? "#3A4A6A" : (vehicleCardMouseArea.containsMouse ? "#2A3A4A" : "#1A1A2A")
                    border.color: vehicleManager.currentVin === modelData ? "#4A90E2" : "#444444"
                    border.width: vehicleManager.currentVin === modelData ? 3 : 1
                    
                    Behavior on color { ColorAnimation { duration: 200 } }
                    Behavior on border.color { ColorAnimation { duration: 200 } }
                    
                    // 选中状态的光晕效果
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: vehicleManager.currentVin === modelData ? "#304A90E2" : "#00000000" }
                            GradientStop { position: 1.0; color: "#00000000" }
                        }
                        visible: vehicleManager.currentVin === modelData
                    }
                    
                    // 简单阴影
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        z: -1
                        radius: parent.radius + 3
                        color: "#20000000"
                    }
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 15
                        
                        // 车辆图标/图片区域
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 100
                            radius: 12
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#2A3A4A" }
                                GradientStop { position: 1.0; color: "#1A2A3A" }
                            }
                            border.color: "#4A90E2"
                            border.width: 1
                            
                            // 车辆图标（使用emoji或图标）
                            Text {
                                anchors.centerIn: parent
                                text: "🚛"  // 环卫车图标
                                font.pixelSize: 50
                            }
                            
                            // 选中状态指示器
                            Rectangle {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: 8
                                width: 24
                                height: 24
                                radius: 12
                                color: vehicleManager.currentVin === modelData ? "#4A90E2" : "#444444"
                                visible: vehicleManager.currentVin === modelData
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: "✓"
                                    color: "#FFFFFF"
                                    font.pixelSize: 14
                                    font.bold: true
                                }
                            }
                        }
                        
                        // 车辆信息
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            
                            Text {
                                text: vehicleManager.getVehicleInfo(modelData).name || "环卫扫地车"
                                color: "#FFFFFF"
                                font.pixelSize: 18
                                font.family: vehicleSelectionPage.chineseFont || font.family
                                font.bold: true
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            
                            Text {
                                text: "VIN: " + modelData
                                color: "#B0B0B0"
                                font.pixelSize: 12
                                font.family: vehicleSelectionPage.chineseFont || font.family
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                        
                        // 选择按钮
                        Button {
                            Layout.fillWidth: true
                            height: 36
                            text: vehicleManager.currentVin === modelData ? "✓ 已选择" : "选择此车辆"
                            enabled: vehicleManager.currentVin !== modelData
                            
                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: 14
                                font.family: vehicleSelectionPage.chineseFont || font.family
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
                            }
                            
                            onClicked: {
                                vehicleManager.selectVehicle(modelData)
                            }
                        }
                    }
                    
                    MouseArea {
                        id: vehicleCardMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            vehicleManager.selectVehicle(modelData)
                        }
                    }
                }
            }
        }
        
        // 底部操作区域
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: bottomActions.implicitHeight + 40
            color: "#1A1A2A"
            border.color: "#4A90E2"
            border.width: 2
            radius: 12
            
            ColumnLayout {
                id: bottomActions
                anchors.fill: parent
                anchors.margins: 20
                spacing: 15
                
                // 当前选择的车辆信息
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 15
                    
                    Text {
                        text: "当前选择:"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.family: vehicleSelectionPage.chineseFont || font.family
                        font.bold: true
                    }
                    
                    Text {
                        Layout.fillWidth: true
                        text: vehicleManager.currentVehicleName || vehicleManager.currentVin || "未选择"
                        color: vehicleManager.currentVin.length > 0 ? "#50C878" : "#B0B0B0"
                        font.pixelSize: 16
                        font.family: vehicleSelectionPage.chineseFont || font.family
                        font.bold: true
                    }
                }
                
                // 错误信息
                Rectangle {
                    Layout.fillWidth: true
                    height: errorText.text.length > 0 ? errorText.implicitHeight + 12 : 0
                    color: "#3A1E1E"
                    border.color: "#FF6B6B"
                    border.width: 1
                    radius: 8
                    visible: errorText.text.length > 0
                    
                    Behavior on height { NumberAnimation { duration: 200 } }
                    
                    Text {
                        id: errorText
                        anchors.fill: parent
                        anchors.margins: 6
                        color: "#FF6B6B"
                        font.pixelSize: 12
                        font.family: vehicleSelectionPage.chineseFont || font.family
                        wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // 会话信息（如果已创建）
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
                        spacing: 6
                        
                        Text {
                            text: "📋 会话信息"
                            color: "#4A90E2"
                            font.pixelSize: 14
                            font.family: vehicleSelectionPage.chineseFont || font.family
                            font.bold: true
                        }
                        
                        Text {
                            Layout.fillWidth: true
                            text: "会话 ID: " + (vehicleManager.lastSessionId || "")
                            color: "#FFFFFF"
                            font.pixelSize: 11
                            font.family: vehicleSelectionPage.chineseFont || font.family
                            wrapMode: Text.Wrap
                        }
                        
                        Text {
                            Layout.fillWidth: true
                            text: "控制协议: " + (vehicleManager.lastControlConfig.algo || "N/A")
                            color: "#B0B0B0"
                            font.pixelSize: 10
                            font.family: vehicleSelectionPage.chineseFont || font.family
                        }
                    }
                }
                
                // 操作按钮行
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 15
                    
                    // 创建会话按钮
                    Button {
                        Layout.fillWidth: true
                        height: 52
                        text: creatingSession ? "创建中..." : "创建会话"
                        enabled: vehicleManager.currentVin.length > 0 && !creatingSession
                        
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 16
                            font.family: vehicleSelectionPage.chineseFont || font.family
                            font.bold: true
                            color: parent.enabled ? "#FFFFFF" : "#666666"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        
                        background: Rectangle {
                            radius: 10
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
                                creatingSession = true
                                var baseUrl = authManager.serverUrl && authManager.serverUrl.length > 0
                                               ? authManager.serverUrl
                                               : "http://localhost:8081"
                                vehicleManager.startSessionForCurrentVin(baseUrl, authManager.authToken)
                            }
                        }
                    }
                    
                    // 确认并进入驾驶按钮
                    Button {
                        Layout.fillWidth: true
                        height: 52
                        text: "确认并进入驾驶"
                        enabled: vehicleManager.currentVin.length > 0
                        
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 16
                            font.family: vehicleSelectionPage.chineseFont || font.family
                            font.bold: true
                            color: parent.enabled ? "#FFFFFF" : "#666666"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        
                        background: Rectangle {
                            radius: 10
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
                                if (creatingSession) return
                                console.log("[Client][选车] 确认并进入驾驶：先创建会话 vin=" + vehicleManager.currentVin)
                                creatingSession = true
                                var baseUrl = authManager.serverUrl && authManager.serverUrl.length > 0
                                               ? authManager.serverUrl
                                               : "http://localhost:8081"
                                vehicleManager.startSessionForCurrentVin(baseUrl, authManager.authToken)
                            }
                        }
                    }
                }
            }
        }
    }
    
    property bool creatingSession: false
    
    // 确认并进入驾驶信号（在会话创建成功后发出）
    signal confirmAndEnter()
    
    // 连接车辆管理器信号
    Connections {
        target: vehicleManager
        ignoreUnknownSignals: true
        
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
            creatingSession = false
            console.log("[Client][选车] 会话创建成功 vin=" + vehicleManager.currentVin + " 进入驾驶")
            // 延迟到下一事件循环再发出确认，避免在信号回调栈中同步切换页面导致主线程卡死
            Qt.callLater(function() { confirmAndEnter() })
        }
        
        function onSessionCreateFailed(error) {
            errorText.text = "创建会话失败: " + error
            creatingSession = false
            console.log("[Client][选车] 会话创建失败:", error)
        }
    }
    
    // 页面进入动画
    opacity: 0
    scale: 0.95
    
    Component.onCompleted: {
        opacityAnim.start()
        scaleAnim.start()
        
        // 延迟加载车辆列表
        loadListTimer.start()
    }
    
    NumberAnimation {
        id: opacityAnim
        target: vehicleSelectionPage
        property: "opacity"
        from: 0.0
        to: 1.0
        duration: 400
    }
    
    NumberAnimation {
        id: scaleAnim
        target: vehicleSelectionPage
        property: "scale"
        from: 0.95
        to: 1.0
        duration: 400
        easing.type: Easing.OutCubic
    }
    
    Timer {
        id: loadListTimer
        interval: 300
        onTriggered: {
            if (typeof authManager !== "undefined" && authManager && authManager.isLoggedIn) {
                var baseUrl = authManager.serverUrl && authManager.serverUrl.length > 0
                               ? authManager.serverUrl
                               : "http://localhost:8081"
                vehicleManager.loadVehicleList(baseUrl, authManager.authToken)
            }
        }
    }
}
