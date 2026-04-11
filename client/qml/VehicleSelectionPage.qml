import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "styles" as ThemeModule

/**
 * 车辆选择页面（嵌入主窗口）
 * 用于控制环卫扫地车的车辆选择界面
 * 统一使用 AppContext 和 Theme
 */
Rectangle {
    id: vehicleSelectionPage
    
    // ── 统一属性 ─────────────────────────────────────────────────────
    readonly property string chineseFont: AppContext ? AppContext.chineseFont : ""
    readonly property var authManager: AppContext ? AppContext.authManager : null
    readonly property var vehicleManager: AppContext ? AppContext.vehicleManager : null
    readonly property var theme: ThemeModule.Theme
    
    // 背景渐变
    gradient: Gradient {
        GradientStop { position: 0.0; color: theme.colorBackground }
        GradientStop { position: 0.5; color: theme.colorSurface }
        GradientStop { position: 1.0; color: theme.colorBackground }
    }
    
    // 装饰性背景元素
    Rectangle {
        anchors.fill: parent
        opacity: 0.05
        gradient: Gradient {
            GradientStop { position: 0.0; color: theme.colorBorderActive }
            GradientStop { position: 1.0; color: theme.colorAccent }
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
                
                Button {
                    text: "← 返回"
                    visible: false
                    font.family: vehicleSelectionPage.chineseFont || font.family
                    background: Rectangle {
                        radius: 6
                        color: parent.pressed ? theme.colorPrimary : (parent.hovered ? theme.colorBorderActive : theme.colorBorderActive)
                        border.color: parent.hovered ? theme.colorBorderActive : theme.colorBorder
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: parent.enabled ? theme.colorText : theme.colorTextDim
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        var am = vehicleSelectionPage.authManager
                        if (am) am.logout()
                    }
                }
                
                Item { Layout.fillWidth: true }
                
                Button {
                    text: "🔄 刷新列表"
                    font.family: vehicleSelectionPage.chineseFont || font.family
                    background: Rectangle {
                        radius: 6
                        color: parent.enabled ? (parent.pressed ? theme.colorPrimary : (parent.hovered ? theme.colorBorderActive : theme.colorBorderActive)) : theme.colorBorder
                        border.color: parent.enabled ? theme.colorBorderActive : theme.colorBorder
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: parent.enabled ? theme.colorText : theme.colorTextDim
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        var am = vehicleSelectionPage.authManager
                        var vm = vehicleSelectionPage.vehicleManager
                        if (am && am.isLoggedIn && vm) {
                            var baseUrl = (am.serverUrl && am.serverUrl.length > 0) ? am.serverUrl : "http://localhost:8081"
                            vm.refreshVehicleList(baseUrl, am.authToken)
                        }
                    }
                }
            }
            
            // 标题
            Text {
                text: "选择车辆"
                color: theme.colorText
                font.pixelSize: 36
                font.family: vehicleSelectionPage.chineseFont || font.family
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            
            // 副标题
            Text {
                text: "请选择要控制的环卫扫地车"
                color: theme.colorTextDim
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
                    GradientStop { position: 0.0; color: theme.colorBorderActive }
                    GradientStop { position: 1.0; color: theme.colorAccent }
                }
            }
        }
        
        // 车辆网格列表
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
                model: vehicleSelectionPage.vehicleManager ? vehicleSelectionPage.vehicleManager.vehicleList : []
                
                delegate: Rectangle {
                    width: vehicleGridView.cellWidth - 20
                    height: vehicleGridView.cellHeight - 20
                    property var vm: vehicleSelectionPage.vehicleManager
                    property bool isSelected: vm && vm.currentVin === modelData
                    radius: 16
                    color: isSelected ? theme.colorBorderActive + "44" : (vehicleCardMouseArea.containsMouse ? theme.colorButtonBgHover : theme.colorButtonBg)
                    border.color: isSelected ? theme.colorBorderActive : theme.colorBorder
                    border.width: isSelected ? 3 : 1
                    
                    Behavior on color { ColorAnimation { duration: 200 } }
                    Behavior on border.color { ColorAnimation { duration: 200 } }
                    
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: isSelected ? "#304A90E2" : "#00000000" }
                            GradientStop { position: 1.0; color: "#00000000" }
                        }
                        visible: isSelected
                    }
                    
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
                        
                        // 车辆图标区域
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 100
                            radius: 12
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: theme.colorButtonBgHover }
                                GradientStop { position: 1.0; color: theme.colorButtonBg }
                            }
                            border.color: theme.colorBorderActive
                            border.width: 1
                            
                            Text {
                                anchors.centerIn: parent
                                text: "🚛"
                                font.pixelSize: 50
                            }
                            
                            Rectangle {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: 8
                                width: 24
                                height: 24
                                radius: 12
                                color: isSelected ? theme.colorBorderActive : theme.colorBorder
                                visible: isSelected
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: "✓"
                                    color: theme.colorText
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
                                text: {
                                    var vm = vehicleSelectionPage.vehicleManager
                                    return vm ? (vm.getVehicleInfo(modelData).name || "环卫扫地车") : "环卫扫地车"
                                }
                                color: theme.colorText
                                font.pixelSize: 18
                                font.family: vehicleSelectionPage.chineseFont || font.family
                                font.bold: true
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            
                            Text {
                                text: "VIN: " + modelData
                                color: theme.colorTextDim
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
                            text: isSelected ? "✓ 已选择" : "选择此车辆"
                            enabled: !isSelected
                            font.family: vehicleSelectionPage.chineseFont || font.family
                            background: Rectangle {
                                radius: 8
                                gradient: Gradient {
                                    GradientStop { 
                                        position: 0.0
                                        color: parent.enabled ? (parent.pressed ? theme.colorPrimary : (parent.hovered ? theme.colorBorderActive : theme.colorBorderActive))
                                        : theme.colorBorder
                                    }
                                    GradientStop { 
                                        position: 1.0
                                        color: parent.enabled ? (parent.pressed ? theme.colorPrimary : (parent.hovered ? theme.colorBorderActive : theme.colorPrimary))
                                        : theme.colorSurface
                                    }
                                }
                                border.color: parent.enabled ? theme.colorBorderActive : theme.colorBorder
                                border.width: 1
                            }
                            contentItem: Text {
                                text: parent.text
                                font: parent.font
                                font.bold: true
                                color: parent.enabled ? theme.colorText : theme.colorTextDim
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: {
                                var vm = vehicleSelectionPage.vehicleManager
                                if (vm) vm.selectVehicle(modelData)
                            }
                        }
                    }
                    
                    MouseArea {
                        id: vehicleCardMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            var vm = vehicleSelectionPage.vehicleManager
                            if (vm) vm.selectVehicle(modelData)
                        }
                    }
                }
            }
        }
        
        // 底部操作区域
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: bottomActions.implicitHeight + 40
            color: theme.colorButtonBg
            border.color: theme.colorBorderActive
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
                        color: theme.colorText
                        font.pixelSize: 14
                        font.family: vehicleSelectionPage.chineseFont || font.family
                        font.bold: true
                    }
                    
                    Text {
                        Layout.fillWidth: true
                        text: {
                            var vm = vehicleSelectionPage.vehicleManager
                            return vm ? (vm.currentVehicleName || vm.currentVin || "未选择") : "未选择"
                        }
                        color: {
                            var vm = vehicleSelectionPage.vehicleManager
                            return (vm && vm.currentVin && vm.currentVin.length > 0) ? theme.colorAccent : theme.colorTextDim
                        }
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
                    border.color: theme.colorDanger
                    border.width: 1
                    radius: 8
                    visible: errorText.text.length > 0
                    
                    Behavior on height { NumberAnimation { duration: 200 } }
                    
                    Text {
                        id: errorText
                        anchors.fill: parent
                        anchors.margins: 6
                        color: theme.colorDanger
                        font.pixelSize: 12
                        font.family: vehicleSelectionPage.chineseFont || font.family
                        wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // 会话信息
                Rectangle {
                    Layout.fillWidth: true
                    height: sessionInfoColumn.implicitHeight + 24
                    color: theme.colorButtonBg
                    border.color: theme.colorBorderActive
                    border.width: 1
                    radius: 8
                    property var vm: vehicleSelectionPage.vehicleManager
                    visible: vm && vm.lastSessionId && vm.lastSessionId.length > 0
                    
                    ColumnLayout {
                        id: sessionInfoColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        
                        Text {
                            text: "📋 会话信息"
                            color: theme.colorBorderActive
                            font.pixelSize: 14
                            font.family: vehicleSelectionPage.chineseFont || font.family
                            font.bold: true
                        }
                        
                        Text {
                            Layout.fillWidth: true
                            text: "会话 ID: " + (vehicleSelectionPage.vehicleManager ? vehicleSelectionPage.vehicleManager.lastSessionId || "" : "")
                            color: theme.colorText
                            font.pixelSize: 11
                            font.family: vehicleSelectionPage.chineseFont || font.family
                            wrapMode: Text.Wrap
                        }
                        
                        Text {
                            Layout.fillWidth: true
                            text: "控制协议: " + (vehicleSelectionPage.vehicleManager && vehicleSelectionPage.vehicleManager.lastControlConfig ? vehicleSelectionPage.vehicleManager.lastControlConfig.algo || "N/A" : "N/A")
                            color: theme.colorTextDim
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
                        property var vm: vehicleSelectionPage.vehicleManager
                        enabled: (vm && vm.currentVin && vm.currentVin.length > 0) && !creatingSession
                        font.family: vehicleSelectionPage.chineseFont || font.family
                        background: Rectangle {
                            radius: 10
                            gradient: Gradient {
                                GradientStop { 
                                    position: 0.0
                                    color: parent.enabled ? (parent.pressed ? theme.colorPrimary : (parent.hovered ? theme.colorBorderActive : theme.colorBorderActive))
                                    : theme.colorBorder
                                }
                                GradientStop { 
                                    position: 1.0
                                    color: parent.enabled ? (parent.pressed ? theme.colorPrimary : (parent.hovered ? theme.colorBorderActive : theme.colorPrimary))
                                    : theme.colorSurface
                                }
                            }
                            border.color: parent.enabled ? theme.colorBorderActive : theme.colorBorder
                            border.width: 1
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -2
                                z: -1
                                radius: parent.radius + 2
                                color: parent.parent.enabled ? "#30000000" : "#00000000"
                            }
                        }
                        contentItem: Text {
                            text: parent.text
                            font: parent.font
                            font.bold: true
                            color: parent.enabled ? theme.colorText : theme.colorTextDim
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            var am = vehicleSelectionPage.authManager
                            var vm = vehicleSelectionPage.vehicleManager
                            if (vm && vm.currentVin && vm.currentVin.length > 0 && am) {
                                creatingSession = true
                                var baseUrl = (am.serverUrl && am.serverUrl.length > 0) ? am.serverUrl : "http://localhost:8081"
                                vm.startSessionForCurrentVin(baseUrl, am.authToken)
                            }
                        }
                    }
                    
                    // 确认并进入驾驶按钮
                    Button {
                        Layout.fillWidth: true
                        height: 52
                        text: "确认并进入驾驶"
                        property var vm: vehicleSelectionPage.vehicleManager
                        enabled: vm && vm.currentVin && vm.currentVin.length > 0
                        font.family: vehicleSelectionPage.chineseFont || font.family
                        background: Rectangle {
                            radius: 10
                            gradient: Gradient {
                                GradientStop { 
                                    position: 0.0
                                    color: parent.enabled ? (parent.pressed ? theme.colorAccent : (parent.hovered ? theme.colorAccent : theme.colorAccent))
                                    : theme.colorBorder
                                }
                                GradientStop { 
                                    position: 1.0
                                    color: parent.enabled ? (parent.pressed ? "#1A6D3A" : (parent.hovered ? "#60D888" : "#40B868"))
                                    : theme.colorSurface
                                }
                            }
                            border.color: parent.enabled ? "#60D888" : theme.colorBorder
                            border.width: 1
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -2
                                z: -1
                                radius: parent.radius + 2
                                color: parent.parent.enabled ? "#30000000" : "#00000000"
                            }
                        }
                        contentItem: Text {
                            text: parent.text
                            font: parent.font
                            font.bold: true
                            color: parent.enabled ? theme.colorText : theme.colorTextDim
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            var am = vehicleSelectionPage.authManager
                            var vm = vehicleSelectionPage.vehicleManager
                            if (vm && vm.currentVin && vm.currentVin.length > 0 && !creatingSession) {
                                console.log("[Client][UI][VehicleSelection] 确认并进入驾驶")
                                creatingSession = true
                                var baseUrl = (am && am.serverUrl && am.serverUrl.length > 0) ? am.serverUrl : "http://localhost:8081"
                                vm.startSessionForCurrentVin(baseUrl, am ? am.authToken : "")
                            }
                        }
                    }
                }
            }
        }
    }
    
    property bool creatingSession: false
    signal confirmAndEnter()
    
    Connections {
        target: vehicleSelectionPage.vehicleManager
        ignoreUnknownSignals: true
        
        function onVehicleListLoadFailed(error) {
            errorText.text = error || "加载车辆列表失败"
        }
        
        function onVehicleListLoaded(vehicles) {
            errorText.text = ""
            if (vehicles && vehicles.length === 0) {
                errorText.text = "没有可用的车辆"
            }
        }
        
        function onSessionCreated(sessionVin, sessionId, whipUrl, whepUrl, controlConfig) {
            errorText.text = ""
            creatingSession = false
            console.log("[Client][UI][VehicleSelection] 会话创建成功 sessionVin=" + sessionVin + "，进入驾驶")
            Qt.callLater(function() { confirmAndEnter() })
        }
        
        function onSessionCreateFailed(error) {
            errorText.text = "创建会话失败: " + (error || "未知错误")
            creatingSession = false
            console.log("[Client][UI][VehicleSelection] 会话创建失败:", error)
        }
    }
    
    // 页面进入动画
    opacity: 0
    scale: 0.95
    
    Component.onCompleted: {
        opacityAnim.start()
        scaleAnim.start()
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
            var am = vehicleSelectionPage.authManager
            var vm = vehicleSelectionPage.vehicleManager
            if (am && am.isLoggedIn && vm) {
                var baseUrl = (am.serverUrl && am.serverUrl.length > 0) ? am.serverUrl : "http://localhost:8081"
                vm.loadVehicleList(baseUrl, am.authToken)
            }
        }
    }
}
