import ".."
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "../styles" as ThemeModule
import "." as CustomComponents

/**
 * 驾驶界面 - 仪表盘面板部分（归档 / 未接入主界面 import 链）
 *
 * 从 DrivingInterface.qml 提取，包含档位显示、箱体状态、速度控制、设备状态、清扫状态、档位选择。
 *
 * Canonical：client/qml/components/driving/* + DrivingInterface（DrivingFacade v2）。
 * 禁止仅在本文交付远驾功能；应 port 至 DrivingCenterColumn 等并更新契约文档。
 * 见 docs/CLIENT_MODULARIZATION_ASSESSMENT.md §5、docs/CLIENT_UI_MODULE_CONTRACT.md §5。
 */
Rectangle {
    id: dashboardPanel
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 主题颜色
    // ═══════════════════════════════════════════════════════════════════════════
    readonly property color colorPanel: ThemeModule.Theme.drivingColorPanel
    readonly property color colorBorder: ThemeModule.Theme.drivingColorBorder
    readonly property color colorTextPrimary: ThemeModule.Theme.drivingColorTextPrimary
    readonly property color colorTextSecondary: ThemeModule.Theme.drivingColorTextSecondary
    readonly property color colorAccent: ThemeModule.Theme.drivingColorAccent
    readonly property color colorWarning: ThemeModule.Theme.drivingColorWarning
    readonly property color colorDanger: ThemeModule.Theme.drivingColorDanger
    readonly property string chineseFont: AppContext.chineseFont || ""
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 布局常量
    // ═══════════════════════════════════════════════════════════════════════════
    readonly property int dashboardMargin: 10
    readonly property int dashboardSpacing: 12
    readonly property int dashboardGearWidth: 104
    readonly property int dashboardTankWidth: 148
    readonly property int dashboardSpeedWidth: 164
    readonly property int dashboardStatusWidth: 156
    readonly property int dashboardProgressWidth: 158
    readonly property int dashboardGearSelectWidth: 220
    readonly property int dashboardSplitterMargin: 8
    property int cardHeight: Math.max(104, height - dashboardMargin * 2)
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 状态属性
    // ═══════════════════════════════════════════════════════════════════════════
    property string displayGear: "N"
    property real waterTankLevel: 75
    property real trashBinLevel: 40
    property int cleaningCurrent: 400
    property int cleaningTotal: 500
    property real targetSpeed: 0.0
    property bool emergencyStopPressed: false
    property var drivingInterface: null
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 发送控制指令
    // ═══════════════════════════════════════════════════════════════════════════
    function sendControlCommand(type, payload) {
        AppContext.sendUiCommand(type, payload)
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 仪表盘布局
    // ═══════════════════════════════════════════════════════════════════════════
    radius: 14
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#1D2538" }
        GradientStop { position: 1.0; color: "#131B2D" }
    }
    border.color: "#324466"
    border.width: 1
    
    // 顶部渐变装饰
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 1
        height: 2
        radius: 12
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.2; color: "#3388FF" }
            GradientStop { position: 0.5; color: "#55AAFF" }
            GradientStop { position: 0.8; color: "#3388FF" }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
    
    RowLayout {
        anchors.fill: parent
        anchors.margins: dashboardMargin
        spacing: dashboardSpacing
        
        // ═══════════════════════════════════════════════════════════════════════
        // 档位显示
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.preferredWidth: dashboardGearWidth
            Layout.fillHeight: true
            
            CustomComponents.DashboardCard {
                anchors.centerIn: parent
                width: parent.width - 4
                height: cardHeight
                Column {
                    anchors.centerIn: parent
                    spacing: 7
                    
                    Item {
                        width: 56; height: 56
                        anchors.horizontalCenter: parent.horizontalCenter
                        
                        Rectangle {
                            anchors.centerIn: parent
                            width: 58; height: 58; radius: 29
                            color: "transparent"
                            border.width: 1
                            border.color: Qt.rgba(0.33, 0.6, 1.0, 0.15)
                        }
                        
                        Rectangle {
                            anchors.centerIn: parent
                            width: 52; height: 52; radius: 26
                            color: "transparent"
                            border.width: 3
                            border.color: "#3A6FC4"
                            
                            Canvas {
                                anchors.fill: parent
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)
                                    
                                    ctx.strokeStyle = Qt.rgba(0.2, 0.35, 0.6, 0.3)
                                    ctx.lineWidth = 4
                                    ctx.lineCap = "round"
                                    ctx.beginPath()
                                    ctx.arc(width/2, height/2, 21, -Math.PI * 0.75, Math.PI * 0.75)
                                    ctx.stroke()
                                    
                                    ctx.strokeStyle = "#55AAFF"
                                    ctx.lineWidth = 4
                                    ctx.lineCap = "round"
                                    ctx.shadowColor = "#55AAFF"
                                    ctx.shadowBlur = 6
                                    ctx.beginPath()
                                    ctx.arc(width/2, height/2, 21, -Math.PI * 0.75, Math.PI * 0.25)
                                    ctx.stroke()
                                }
                            }
                            
                            Text {
                                anchors.centerIn: parent
                                text: displayGear
                                color: "#E8F0FF"
                                font.pixelSize: 24
                                font.bold: true
                                font.family: "Consolas"
                            }
                        }
                    }
                    
                    Text {
                        text: "档位反馈"
                        color: "#9CB2DF"
                        font.pixelSize: 11
                        font.bold: true
                        font.family: chineseFont || font.family
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }
        }
        
        VerticalDivider {}
        
        // ═══════════════════════════════════════════════════════════════════════
        // 水箱 + 垃圾箱
        // ═══════════════════════════════════════════════════════════════════════
        ColumnLayout {
            Layout.preferredWidth: dashboardTankWidth
            Layout.fillHeight: true
            spacing: 6
            
            Item { Layout.fillHeight: true }
            
            Text {
                text: "箱体状态"
                color: "#9CB2DF"
                font.pixelSize: 11
                font.bold: true
                font.family: chineseFont || font.family
                Layout.alignment: Qt.AlignHCenter
            }
            
            // 水箱
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                radius: 8
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#18263C" }
                    GradientStop { position: 1.0; color: "#122236" }
                }
                border.color: waterTankLevel < 20 ? "#C05A5A" : "#2F4D75"
                border.width: 1
                
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 4
                    
                    Text {
                        text: "水箱"
                        color: "#8899BB"
                        font.pixelSize: 11
                        font.family: chineseFont || font.family
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    Text {
                        id: waterTankPercentText
                        text: Math.round(waterTankLevel) + "%"
                        color: waterTankLevel < 20 ? "#FF6B6B" : "#55BBFF"
                        font.pixelSize: 11
                        font.bold: true
                        font.family: "Consolas"
                    }
                }
                
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottomMargin: 1
                    anchors.leftMargin: 1
                    anchors.rightMargin: 1
                    width: parent.width * Math.min(waterTankLevel / 100.0, 1.0)
                    height: 3
                    radius: 2
                    color: waterTankLevel < 20 ? "#FF6B6B" : "#3388FF"
                    opacity: 0.8
                    
                    Behavior on width {
                        NumberAnimation { duration: 500; easing.type: Easing.OutCubic }
                    }
                }
            }
            
            // 垃圾箱
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                radius: 8
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#1D2436" }
                    GradientStop { position: 1.0; color: "#171D2D" }
                }
                border.color: trashBinLevel > 80 ? Qt.rgba(1, 0.4, 0.4, 0.6) : "#2F4D75"
                border.width: 1
                
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 4
                    
                    Text {
                        text: "垃圾箱"
                        color: "#8899BB"
                        font.pixelSize: 11
                        font.family: chineseFont || font.family
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    Text {
                        id: trashBinPercentText
                        text: Math.round(trashBinLevel) + "%"
                        color: trashBinLevel > 80 ? "#FF6B6B" : "#66DDAA"
                        font.pixelSize: 11
                        font.bold: true
                        font.family: "Consolas"
                    }
                }
                
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottomMargin: 1
                    anchors.leftMargin: 1
                    anchors.rightMargin: 1
                    width: parent.width * Math.min(trashBinLevel / 100.0, 1.0)
                    height: 3
                    radius: 2
                    color: trashBinLevel > 80 ? "#FF6B6B" : "#44BB88"
                    opacity: 0.8
                    
                    Behavior on width {
                        NumberAnimation { duration: 500; easing.type: Easing.OutCubic }
                    }
                }
            }
            
            Item { Layout.fillHeight: true }
        }
        
        VerticalDivider {}
        
        // ═══════════════════════════════════════════════════════════════════════
        // 速度控制 + 急停
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.preferredWidth: dashboardSpeedWidth
            Layout.fillHeight: true
            
            CustomComponents.DashboardCard {
                anchors.centerIn: parent
                width: Math.max(0, parent.width - 2)
                height: cardHeight
                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    
                    Text {
                        text: "目标车速 km/h"
                        color: "#9CB2DF"
                        font.pixelSize: 11
                        font.bold: true
                        font.family: chineseFont || font.family
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 8
                        
                        // 急停按钮
                        Rectangle {
                            id: emergencyStopButton
                            width: 52; height: 46; radius: 12
                            
                            gradient: Gradient {
                                GradientStop {
                                    position: 0.0
                                    color: emergencyStopPressed ? "#FF3A3A" : "#2C344C"
                                }
                                GradientStop {
                                    position: 1.0
                                    color: emergencyStopPressed ? "#C91010" : "#212A40"
                                }
                            }
                            
                            border.width: 2
                            border.color: emergencyStopPressed ? "#FF7B7B" : "#4B5F87"
                            
                            Column {
                                anchors.centerIn: parent
                                spacing: 1
                                
                                Text {
                                    text: "⛔"
                                    font.pixelSize: emergencyStopPressed ? 10 : 12
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                                Text {
                                    text: "急停"
                                    color: emergencyStopPressed ? "#FFFFFF" : "#CCDDEE"
                                    font.pixelSize: 10
                                    font.bold: true
                                    font.family: chineseFont || font.family
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                            }
                            
                            SequentialAnimation on opacity {
                                running: emergencyStopPressed
                                loops: Animation.Infinite
                                NumberAnimation { to: 1.0; duration: 600 }
                                NumberAnimation { to: 0.3; duration: 600 }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    emergencyStopPressed = true
                                    targetSpeed = 0.0
                                    targetSpeedInput.text = "0.0"
                                    
                                    if (AppContext.systemStateMachine && typeof systemStateMachine.fireByName === "function")
                                        systemStateMachine.fireByName("EMERGENCY_STOP")
                                    if (AppContext.vehicleControl && typeof vehicleControl.requestEmergencyStop === "function")
                                        vehicleControl.requestEmergencyStop()
                                    else {
                                        sendControlCommand("brake", { value: 1.0 })
                                        sendControlCommand("speed", { value: 0.0 })
                                    }
                                }
                            }
                        }
                        
                        // 目标速度输入
                        Rectangle {
                            width: 96; height: 42; radius: 10
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#182236" }
                                GradientStop { position: 1.0; color: "#111A2C" }
                            }
                            border.width: 2
                            border.color: targetSpeedInput.activeFocus ? "#5E93FF" : "#36507D"
                            
                            TextField {
                                id: targetSpeedInput
                                anchors.fill: parent
                                anchors.margins: 2
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                text: targetSpeed.toFixed(1)
                                color: "#E8F0FF"
                                font.pixelSize: 17
                                font.bold: true
                                font.family: "Consolas"
                                
                                background: Rectangle { color: "transparent" }
                                
                                validator: DoubleValidator {
                                    bottom: 0.0
                                    top: 100.0
                                    decimals: 1
                                }
                                
                                onEditingFinished: {
                                    var newSpeed = parseFloat(text)
                                    if (isNaN(newSpeed)) {
                                        text = targetSpeed.toFixed(1)
                                        return
                                    }
                                    newSpeed = Math.max(0.0, Math.min(100.0, newSpeed))
                                    
                                    // [Fix] 仅在变化超过 0.05 时更新，并消除 onTextChanged 导致的 Binding Loop
                                    if (Math.abs(targetSpeed - newSpeed) > 0.05) {
                                        targetSpeed = newSpeed
                                    }
                                    text = targetSpeed.toFixed(1)
                                    
                                    if (targetSpeed > 0.0) {
                                        emergencyStopPressed = false
                                    }
                                    
                                    var remoteControlEnabled = false
                                    if (AppContext.vehicleStatus) {
                                        remoteControlEnabled = (vehicleStatus.drivingMode === "远驾")
                                    }
                                    if (!remoteControlEnabled) return
                                    
                                    sendControlCommand("speed", { value: targetSpeed })
                                }
                                
                                // [Fix] 移除 onTextChanged 以防止与键盘定时器冲突导致的输入抖动
                                }
                        }
                    }
                }
            }
        }
        
        VerticalDivider {}
        
        // ═══════════════════════════════════════════════════════════════════════
        // 设备状态
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.preferredWidth: dashboardStatusWidth
            Layout.fillHeight: true
            
            Column {
                anchors.centerIn: parent
                spacing: 6
                
                Text {
                    text: "设备状态"
                    color: "#7B8DB8"
                    font.pixelSize: 11
                    font.bold: true
                    font.family: chineseFont || font.family
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                
                Grid {
                    columns: 2
                    rowSpacing: 4
                    columnSpacing: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    
                    property var deviceList: [
                        { name: "左盘刷", status: true },
                        { name: "右盘刷", status: true },
                        { name: "主刷", status: true },
                        { name: "吸嘴", status: true }
                    ]
                    
                    Repeater {
                        model: parent.deviceList
                        
                        Rectangle {
                            width: 52; height: 24; radius: 6
                            color: modelData.status ? Qt.rgba(0.2, 0.8, 0.5, 0.1) : Qt.rgba(1, 0.3, 0.3, 0.1)
                            border.width: 1
                            border.color: modelData.status ? Qt.rgba(0.2, 0.8, 0.5, 0.25) : Qt.rgba(1, 0.3, 0.3, 0.25)
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 3
                                
                                Rectangle {
                                    width: 6; height: 6; radius: 3
                                    color: modelData.status ? "#44CC88" : "#FF5555"
                                    anchors.verticalCenter: parent.verticalCenter
                                    
                                    SequentialAnimation on opacity {
                                        running: modelData.status
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.5; duration: 1500 }
                                        NumberAnimation { to: 1.0; duration: 1500 }
                                    }
                                }
                                
                                Text {
                                    text: modelData.name
                                    color: modelData.status ? "#88CCAA" : "#CC8888"
                                    font.pixelSize: 9
                                    font.family: chineseFont || font.family
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }
            }
        }
        
        VerticalDivider {}
        
        // ═══════════════════════════════════════════════════════════════════════
        // 清扫进度
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.preferredWidth: dashboardProgressWidth
            Layout.fillHeight: true
            
            CustomComponents.DashboardCard {
                anchors.centerIn: parent
                width: parent.width - 2
                height: cardHeight
                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 6
                        
                        Text { text: "🧹"; font.pixelSize: 16 }
                        Text {
                            text: "清扫状态"
                            color: "#9CB2DF"
                            font.pixelSize: 13
                            font.bold: true
                            font.family: chineseFont || font.family
                        }
                    }
                    
                    Item {
                        width: 52; height: 52
                        anchors.horizontalCenter: parent.horizontalCenter
                        
                        property real progressValue: cleaningTotal > 0 ? (cleaningCurrent / cleaningTotal) : 0
                        
                        Canvas {
                            id: progressCanvas
                            anchors.fill: parent
                            
                            property real progress: parent.progressValue
                            onProgressChanged: requestPaint()
                            
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                var cx = width / 2
                                var cy = height / 2
                                var r = 21
                                var startAngle = -Math.PI / 2
                                
                                ctx.strokeStyle = "#1E2538"
                                ctx.lineWidth = 5
                                ctx.lineCap = "round"
                                ctx.beginPath()
                                ctx.arc(cx, cy, r, 0, Math.PI * 2)
                                ctx.stroke()
                                
                                if (progress > 0) {
                                    ctx.strokeStyle = "#44BBFF"
                                    ctx.lineWidth = 5
                                    ctx.lineCap = "round"
                                    ctx.shadowColor = "#44BBFF"
                                    ctx.shadowBlur = 4
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, r, startAngle, startAngle + Math.PI * 2 * Math.min(progress, 1.0))
                                    ctx.stroke()
                                }
                            }
                        }
                        
                        Text {
                            id: cleaningPercentText
                            anchors.centerIn: parent
                            text: {
                                var percent = cleaningTotal > 0 ? Math.round(cleaningCurrent * 100 / cleaningTotal) : 0
                                return percent + "%"
                            }
                            color: "#55CCFF"
                            font.pixelSize: 14
                            font.bold: true
                            font.family: "Consolas"
                        }
                    }
                }
            }
        }
        
        Item { Layout.fillWidth: true }
        
        VerticalDivider {}
        
        // ═══════════════════════════════════════════════════════════════════════
        // 档位选择
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.preferredWidth: dashboardGearSelectWidth
            Layout.fillHeight: true
            
            CustomComponents.DashboardCard {
                anchors.centerIn: parent
                width: parent.width - 2
                height: cardHeight
                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    
                    Text {
                        text: "档位选择"
                        color: "#9CB2DF"
                        font.pixelSize: 11
                        font.bold: true
                        font.family: chineseFont || font.family
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    
                    Row {
                        spacing: 10
                        anchors.horizontalCenter: parent.horizontalCenter
                        
                        Repeater {
                            model: ["P", "N", "R", "D"]
                            
                            Rectangle {
                                id: gearButton
                                width: 40; height: 40; radius: 20
                                
                                property bool isSelected: modelData === displayGear
                                property color gearBaseColor: {
                                    if (modelData === "R") return "#E85D5D"
                                    if (modelData === "D") return "#4F8DFF"
                                    if (modelData === "P") return "#7D6BFF"
                                    return "#8A96B8"
                                }
                                
                                gradient: Gradient {
                                    GradientStop {
                                        position: 0.0
                                        color: {
                                            if (gearButton.isSelected) return Qt.lighter(gearButton.gearBaseColor, 1.12)
                                            if (gearMouseArea.pressed) return "#1D2640"
                                            if (gearMouseArea.containsMouse) return "#2A3553"
                                            return "#232B42"
                                        }
                                    }
                                    GradientStop {
                                        position: 1.0
                                        color: {
                                            if (gearButton.isSelected) return Qt.darker(gearButton.gearBaseColor, 1.18)
                                            if (gearMouseArea.pressed) return "#141B2E"
                                            if (gearMouseArea.containsMouse) return "#1C2440"
                                            return "#171E33"
                                        }
                                    }
                                }
                                
                                border.width: 2
                                border.color: gearButton.isSelected ? Qt.lighter(gearButton.gearBaseColor, 1.35) : 
                                              (gearMouseArea.containsMouse ? "#4E6188" : "#3A4868")
                                
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 48; height: 48; radius: 24
                                    color: "transparent"
                                    border.width: gearButton.isSelected ? 1 : 0
                                    border.color: gearButton.isSelected ? Qt.rgba(0.9, 0.95, 1.0, 0.45) : "transparent"
                                    visible: gearButton.isSelected
                                }
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: gearButton.isSelected ? "#FFFFFF" : "#9CA9C8"
                                    font.pixelSize: 16
                                    font.bold: true
                                    font.family: "Consolas"
                                }
                                
                                scale: gearMouseArea.pressed ? 0.95 : (gearMouseArea.containsMouse ? 1.08 : 1.0)
                                
                                Behavior on scale {
                                    NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                                }
                                
                                MouseArea {
                                    id: gearMouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        displayGear = modelData
                                        if (drivingInterface) {
                                            drivingInterface.currentGear = modelData
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 垂直分隔线组件
    component VerticalDivider: Rectangle {
        Layout.preferredWidth: 1
        Layout.fillHeight: true
        Layout.topMargin: dashboardSplitterMargin
        Layout.bottomMargin: dashboardSplitterMargin
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.3; color: "#2A3456" }
            GradientStop { position: 0.7; color: "#2A3456" }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
}