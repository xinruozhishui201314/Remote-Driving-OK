import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "styles" as ThemeModule

/**
 * 车辆选择对话框
 * 统一使用 AppContext 和 Theme
 */
Popup {
    id: vehicleSelectionDialog
    
    QtObject {
        id: sessionState
        property bool creating: false
    }
    
    // ── 统一属性 ─────────────────────────────────────────────────────
    readonly property string chineseFont: AppContext ? AppContext.chineseFont : ""
    readonly property var authManager: AppContext ? AppContext.authManager : null
    readonly property var vehicleManager: AppContext ? AppContext.vehicleManager : null
    
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
            GradientStop { position: 0.0; color: ThemeModule.Theme.colorSurface }
            GradientStop { position: 1.0; color: ThemeModule.Theme.colorBackground }
        }
        border.color: ThemeModule.Theme.colorBorderActive
        border.width: 2
        radius: 12
        
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
                    color: ThemeModule.Theme.colorText
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
                        GradientStop { position: 0.0; color: ThemeModule.Theme.colorBorderActive }
                        GradientStop { position: 1.0; color: ThemeModule.Theme.colorAccent }
                    }
                }
            }

            // 刷新列表 + 退出登录
            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: "刷新列表"
                    font.family: vehicleSelectionDialog.chineseFont || font.family
                    background: Rectangle {
                        radius: 6
                        color: parent.pressed ? ThemeModule.Theme.colorPrimary : (parent.hovered ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorBorderActive)
                        border.color: parent.hovered ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorBorder
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 12
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                        color: parent.enabled ? ThemeModule.Theme.colorText : ThemeModule.Theme.colorTextDim
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        var am = vehicleSelectionDialog.authManager
                        var vm = vehicleSelectionDialog.vehicleManager
                        if (am && am.isLoggedIn && vm) {
                            var baseUrl = (am.serverUrl && am.serverUrl.length > 0) ? am.serverUrl : "http://localhost:8081"
                            vm.refreshVehicleList(baseUrl, am.authToken)
                        }
                    }
                }

                Button {
                    text: "退出登录"
                    font.family: vehicleSelectionDialog.chineseFont || font.family
                    background: Rectangle {
                        radius: 6
                        color: parent.pressed ? "#8B3A3A" : (parent.hovered ? "#A04444" : "#6B2A2A")
                        border.color: parent.hovered ? "#CC6666" : ThemeModule.Theme.colorBorder
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 12
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                        color: parent.enabled ? ThemeModule.Theme.colorText : ThemeModule.Theme.colorTextDim
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        var am = vehicleSelectionDialog.authManager
                        if (am) {
                            console.log("[Client][UI][VehicleSelection] 退出登录")
                            am.logout()
                        }
                        vehicleSelectionDialog.close()
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
                    model: vehicleSelectionDialog.vehicleManager ? vehicleSelectionDialog.vehicleManager.vehicleList : []
                    spacing: 5

                    delegate: Rectangle {
                        width: vehicleListView.width
                        height: 70
                        property var vm: vehicleSelectionDialog.vehicleManager
                        property bool isSelected: vm && vm.currentVin === modelData
                        color: isSelected ? ThemeModule.Theme.colorBorderActive + "44" : (mouseArea.containsMouse ? ThemeModule.Theme.colorButtonBgHover : ThemeModule.Theme.colorButtonBg)
                        border.color: isSelected ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorBorder
                        border.width: isSelected ? 2 : 1
                        radius: 8
                        
                        Behavior on color { ColorAnimation { duration: 200 } }
                        Behavior on border.color { ColorAnimation { duration: 200 } }
                        
                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: isSelected ? "#204A90E2" : "#00000000" }
                                GradientStop { position: 1.0; color: "#00000000" }
                            }
                            visible: isSelected
                        }
                        
                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                var vm = vehicleSelectionDialog.vehicleManager
                                if (vm) {
                                    vm.selectVehicle(modelData)
                                }
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                property var vm: vehicleSelectionDialog.vehicleManager

                                Text {
                                    text: {
                                        var vm = vehicleSelectionDialog.vehicleManager
                                        return vm ? (vm.getVehicleInfo(modelData).name || modelData) : modelData
                                    }
                                    color: ThemeModule.Theme.colorText
                                    font.pixelSize: 14
                                    font.family: vehicleSelectionDialog.chineseFont || font.family
                                    font.bold: true
                                }

                                Text {
                                    text: "VIN: " + modelData
                                    color: ThemeModule.Theme.colorTextDim
                                    font.pixelSize: 12
                                    font.family: vehicleSelectionDialog.chineseFont || font.family
                                }
                            }

                            Button {
                                property var vm: vehicleSelectionDialog.vehicleManager
                                text: (vm && vm.currentVin === modelData) ? "✓ 已选择" : "选择"
                                enabled: !(vm && vm.currentVin === modelData)
                                background: Rectangle {
                                    radius: 6
                                    gradient: Gradient {
                                        GradientStop { 
                                            position: 0.0
                                            color: parent.enabled ? (parent.pressed ? ThemeModule.Theme.colorPrimary : (parent.hovered ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorBorderActive))
                                            : ThemeModule.Theme.colorBorder
                                        }
                                        GradientStop { 
                                            position: 1.0
                                            color: parent.enabled ? (parent.pressed ? ThemeModule.Theme.colorPrimary : (parent.hovered ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorPrimary))
                                            : ThemeModule.Theme.colorSurface
                                        }
                                    }
                                    border.color: parent.enabled ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorBorder
                                    border.width: 1
                                }
                                contentItem: Text {
                                    text: parent.text
                                    font.pixelSize: 13
                                    font.family: vehicleSelectionDialog.chineseFont || font.family
                                    font.bold: true
                                    color: parent.enabled ? ThemeModule.Theme.colorText : ThemeModule.Theme.colorTextDim
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                onClicked: {
                                    var vm = vehicleSelectionDialog.vehicleManager
                                    if (vm) {
                                        vm.selectVehicle(modelData)
                                    }
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
                color: ThemeModule.Theme.colorButtonBg
                border.color: ThemeModule.Theme.colorBorder
                border.width: 1
                radius: 3
                property var vm: vehicleSelectionDialog.vehicleManager
                visible: vm && vm.currentVin && vm.currentVin.length > 0

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10

                    Text {
                        text: "当前车辆:"
                        color: ThemeModule.Theme.colorText
                        font.pixelSize: 12
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                    }

                    Text {
                        Layout.fillWidth: true
                        text: {
                            var vm = vehicleSelectionDialog.vehicleManager
                            return vm ? (vm.currentVehicleName || vm.currentVin) : ""
                        }
                        color: ThemeModule.Theme.colorGood
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
                color: ThemeModule.Theme.colorDanger
                font.pixelSize: 12
                font.family: vehicleSelectionDialog.chineseFont || font.family
                visible: text.length > 0
                wrapMode: Text.WordWrap
            }

            // 会话信息显示区域
            Rectangle {
                Layout.fillWidth: true
                height: sessionInfoColumn.implicitHeight + 24
                color: ThemeModule.Theme.colorButtonBg
                border.color: ThemeModule.Theme.colorBorderActive
                border.width: 1
                radius: 8
                property var vm: vehicleSelectionDialog.vehicleManager
                visible: vm && vm.lastSessionId && vm.lastSessionId.length > 0
                
                ColumnLayout {
                    id: sessionInfoColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    
                    Text {
                        text: "📋 会话信息"
                        color: ThemeModule.Theme.colorBorderActive
                        font.pixelSize: 14
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                        font.bold: true
                    }
                
                    Text {
                        Layout.fillWidth: true
                        text: "会话 ID: " + (vehicleSelectionDialog.vehicleManager ? vehicleSelectionDialog.vehicleManager.lastSessionId || "" : "")
                        color: ThemeModule.Theme.colorText
                        font.pixelSize: 11
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                        wrapMode: Text.Wrap
                    }
                    
                    Text {
                        Layout.fillWidth: true
                        text: "WHIP URL: " + (vehicleSelectionDialog.vehicleManager ? vehicleSelectionDialog.vehicleManager.lastWhipUrl || "" : "")
                        color: ThemeModule.Theme.colorTextDim
                        font.pixelSize: 10
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                        wrapMode: Text.Wrap
                    }
                    
                    Text {
                        Layout.fillWidth: true
                        text: "WHEP URL: " + (vehicleSelectionDialog.vehicleManager ? vehicleSelectionDialog.vehicleManager.lastWhepUrl || "" : "")
                        color: ThemeModule.Theme.colorTextDim
                        font.pixelSize: 10
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                        wrapMode: Text.Wrap
                    }
                    
                    Text {
                        Layout.fillWidth: true
                        text: "控制协议: " + (vehicleSelectionDialog.vehicleManager && vehicleSelectionDialog.vehicleManager.lastControlConfig ? vehicleSelectionDialog.vehicleManager.lastControlConfig.algo || "N/A" : "N/A")
                        color: ThemeModule.Theme.colorTextDim
                        font.pixelSize: 10
                        font.family: vehicleSelectionDialog.chineseFont || font.family
                    }
                }
            }
            
            // 创建会话按钮
            Button {
                Layout.fillWidth: true
                height: 48
                text: sessionState.creating ? "创建中..." : "创建会话"
                property var vm: vehicleSelectionDialog.vehicleManager
                enabled: (vm && vm.currentVin && vm.currentVin.length > 0) && !sessionState.creating
                background: Rectangle {
                    radius: 8
                    gradient: Gradient {
                        GradientStop { 
                            position: 0.0
                            color: parent.enabled ? (parent.pressed ? ThemeModule.Theme.colorPrimary : (parent.hovered ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorBorderActive))
                            : ThemeModule.Theme.colorBorder
                        }
                        GradientStop { 
                            position: 1.0
                            color: parent.enabled ? (parent.pressed ? ThemeModule.Theme.colorPrimary : (parent.hovered ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorPrimary))
                            : ThemeModule.Theme.colorSurface
                        }
                    }
                    border.color: parent.enabled ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorBorder
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
                    font.pixelSize: 16
                    font.family: vehicleSelectionDialog.chineseFont || font.family
                    font.bold: true
                    color: parent.enabled ? ThemeModule.Theme.colorText : ThemeModule.Theme.colorTextDim
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    var am = vehicleSelectionDialog.authManager
                    var vm = vehicleSelectionDialog.vehicleManager
                    if (vm && vm.currentVin && vm.currentVin.length > 0 && am) {
                        sessionState.creating = true
                        var baseUrl = (am.serverUrl && am.serverUrl.length > 0) ? am.serverUrl : "http://localhost:8081"
                        vm.startSessionForCurrentVin(baseUrl, am.authToken)
                    }
                }
            }
            
            // 确认并进入驾驶按钮
            Button {
                Layout.fillWidth: true
                height: 48
                text: sessionState.creating ? "创建中..." : "确认并进入驾驶"
                property var vm: vehicleSelectionDialog.vehicleManager
                enabled: (vm && vm.currentVin && vm.currentVin.length > 0) && !sessionState.creating
                background: Rectangle {
                    radius: 8
                    gradient: Gradient {
                        GradientStop { 
                            position: 0.0
                            color: parent.enabled ? (parent.pressed ? ThemeModule.Theme.colorAccent : (parent.hovered ? ThemeModule.Theme.colorAccent : ThemeModule.Theme.colorAccent))
                            : ThemeModule.Theme.colorBorder
                        }
                        GradientStop { 
                            position: 1.0
                            color: parent.enabled ? (parent.pressed ? "#1A6D3A" : (parent.hovered ? "#60D888" : "#40B868"))
                            : ThemeModule.Theme.colorSurface
                        }
                    }
                    border.color: parent.enabled ? "#60D888" : ThemeModule.Theme.colorBorder
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
                    font.pixelSize: 16
                    font.family: vehicleSelectionDialog.chineseFont || font.family
                    font.bold: true
                    color: parent.enabled ? ThemeModule.Theme.colorText : ThemeModule.Theme.colorTextDim
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    var am = vehicleSelectionDialog.authManager
                    var vm = vehicleSelectionDialog.vehicleManager
                    if (vm && vm.currentVin && vm.currentVin.length > 0 && !sessionState.creating) {
                        console.log("[Client][UI][VehicleSelection] 确认并进入驾驶：vin=" + vm.currentVin)
                        sessionState.creating = true
                        var baseUrl = (am && am.serverUrl && am.serverUrl.length > 0) ? am.serverUrl : "http://localhost:8081"
                        vm.startSessionForCurrentVin(baseUrl, am ? am.authToken : "")
                    }
                }
            }
        }
    }

    Connections {
        target: vehicleSelectionDialog.vehicleManager
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
            sessionState.creating = false
            console.log("[Client][UI][VehicleSelection] 会话创建成功 sessionVin=" + sessionVin + " sessionId=" + sessionId)
            Qt.callLater(function() { vehicleSelectionDialog.close() })
        }
        
        function onSessionCreateFailed(error) {
            errorText.text = "创建会话失败: " + (error || "未知错误")
            sessionState.creating = false
            console.log("[Client][UI][VehicleSelection] 会话创建失败:", error)
        }
    }

    Component.onCompleted: {
        loadListTimer.start()
    }
    Timer {
        id: loadListTimer
        interval: 200
        onTriggered: {
            var am = vehicleSelectionDialog.authManager
            var vm = vehicleSelectionDialog.vehicleManager
            if (am && am.isLoggedIn && vm) {
                var baseUrl = (am.serverUrl && am.serverUrl.length > 0) ? am.serverUrl : "http://localhost:8081"
                vm.loadVehicleList(baseUrl, am.authToken)
            }
        }
    }
}
