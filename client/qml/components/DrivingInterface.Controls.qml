import ".."
import QtQuick 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "../styles" as ThemeModule
import "." as CustomComponents

/**
 * 驾驶界面 - 控制面板部分（归档 / 未接入主界面 import 链）
 *
 * 从 DrivingInterface.qml 提取，包含灯光、清扫、速度控制。
 * 共享状态通过 AppContext 传递。
 *
 * Canonical：client/qml/components/driving/* + DrivingInterface（DrivingFacade v2）。
 * 禁止仅在本文交付远驾功能；应 port 至 DrivingCenterColumn 等并更新 docs/CLIENT_UI_MODULE_CONTRACT.md。
 * 见 docs/CLIENT_MODULARIZATION_ASSESSMENT.md §5、docs/CLIENT_UI_MODULE_CONTRACT.md §5。
 */
Rectangle {
    id: controlsPanel
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 主题颜色
    // ═══════════════════════════════════════════════════════════════════════════
    readonly property color colorPanel: ThemeModule.Theme.drivingColorPanel
    readonly property color colorBorder: ThemeModule.Theme.drivingColorBorder
    readonly property color colorBorderActive: ThemeModule.Theme.drivingColorBorderActive
    readonly property color colorTextPrimary: ThemeModule.Theme.drivingColorTextPrimary
    readonly property color colorTextSecondary: ThemeModule.Theme.drivingColorTextSecondary
    readonly property color colorAccent: ThemeModule.Theme.drivingColorAccent
    readonly property color colorWarning: ThemeModule.Theme.drivingColorWarning
    readonly property color colorDanger: ThemeModule.Theme.drivingColorDanger
    readonly property color colorButtonBg: ThemeModule.Theme.drivingColorButtonBg
    readonly property color colorButtonBorder: ThemeModule.Theme.drivingColorButtonBorder
    readonly property string chineseFont: AppContext.chineseFont || ""
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 布局常量
    // ═══════════════════════════════════════════════════════════════════════════
    readonly property real mainRowAvailH: 400
    property var centerColAllocW: 600
    readonly property int controlAreaMargin: 8
    readonly property int controlAreaSpacing: 8
    readonly property real controlSideColumnRatio: 0.35
    readonly property int controlSpeedometerSize: 140
    readonly property int centerControlsRatio: 0.12
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 控制状态属性（从 DrivingInterface.qml 引用）
    // ═══════════════════════════════════════════════════════════════════════════
    property var drivingInterface: null
    property bool leftTurnActive: false
    property bool rightTurnActive: false
    property bool brakeLightActive: false
    property bool workLightActive: false
    property bool headlightActive: true
    property bool warningLightActive: false
    property bool sweepActive: true
    property bool waterSprayActive: false
    property bool suctionActive: false
    property bool dumpActive: false
    property bool hornActive: false
    property bool workLampActive: false
    property real targetSpeed: 0.0
    property real displaySpeed: 35
    property real displaySteering: 0
    property string displayGear: "N"
    
    // ═���═════════════════════════════════════════════════════════════════════════
    // 统一控制指令发送
    // ═══════════════════════════════════════════════════════════════════════════
    function sendControlCommand(type, payload) {
        AppContext.sendUiCommand(type, payload)
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 控制面板布局
    // ═══════════════════════════════════════════════════════════════════════════
    RowLayout {
        anchors.fill: parent
        anchors.margins: controlAreaMargin
        spacing: controlAreaSpacing
        
        // 左侧：车辆灯光控制
        ColumnLayout {
            Layout.preferredWidth: centerColAllocW * controlSideColumnRatio
            Layout.fillHeight: true
            spacing: 0
            Item { Layout.fillHeight: true }
            
            GridLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(parent.height * 0.85, 190)
                Layout.alignment: Qt.AlignVCenter
                columns: 3
                rowSpacing: 16
                columnSpacing: 16
                
                CustomComponents.ControlButton {
                    label: "刹车灯"
                    active: brakeLightActive
                    onClicked: {
                        brakeLightActive = !brakeLightActive
                        sendControlCommand("light", { name: "brakeLight", active: brakeLightActive })
                    }
                }
                CustomComponents.ControlButton {
                    label: "工作灯"
                    active: workLightActive
                    onClicked: {
                        workLightActive = !workLightActive
                        sendControlCommand("light", { name: "workLight", active: workLightActive })
                    }
                }
                CustomComponents.ControlButton {
                    label: "左转灯"
                    active: leftTurnActive
                    onClicked: {
                        leftTurnActive = !leftTurnActive
                        if (rightTurnActive) rightTurnActive = false
                        sendControlCommand("light", { name: "leftTurn", active: leftTurnActive })
                    }
                }
                CustomComponents.ControlButton {
                    label: "右转灯"
                    active: rightTurnActive
                    onClicked: {
                        rightTurnActive = !rightTurnActive
                        if (leftTurnActive) leftTurnActive = false
                        sendControlCommand("light", { name: "rightTurn", active: rightTurnActive })
                    }
                }
                CustomComponents.ControlButton {
                    label: "近光"
                    active: headlightActive
                    onClicked: {
                        headlightActive = !headlightActive
                        sendControlCommand("light", { name: "headlight", active: headlightActive })
                    }
                }
                CustomComponents.ControlButton {
                    label: "警示"
                    active: warningLightActive
                    onClicked: {
                        warningLightActive = !warningLightActive
                        sendControlCommand("light", { name: "warning", active: warningLightActive })
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
        
        // 中间：速度圆圈
        Item {
            id: speedometerContainer
            Layout.preferredWidth: Math.min(Math.max(0, mainRowAvailH * centerControlsRatio - controlAreaMargin * 2), controlSpeedometerSize)
            Layout.preferredHeight: Layout.preferredWidth
            Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
            
            Rectangle {
                id: speedometerBg
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height) * 0.95
                height: width
                radius: width / 2
                color: "transparent"
                border.width: 3
                border.color: colorBorder
                
                Canvas {
                    id: speedArc
                    anchors.fill: parent
                    
                    property real speedValue: displaySpeed
                    property real steerValue: displaySteering
                    
                    onSpeedValueChanged: requestPaint()
                    onSteerValueChanged: requestPaint()
                    
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        
                        var cx = width / 2
                        var cy = height / 2
                        var outerRadius = width / 2 - 4
                        var innerRadius = outerRadius - 8
                        
                        var speedRatio = Math.min(1, Math.max(0, speedValue / 100))
                        
                        var startAngle = -225 * Math.PI / 180
                        var endAngle = 45 * Math.PI / 180
                        var totalAngle = endAngle - startAngle
                        var currentAngle = startAngle + totalAngle * speedRatio
                        
                        // 背景弧
                        ctx.strokeStyle = "#2A2A3E"
                        ctx.lineWidth = 8
                        ctx.lineCap = "round"
                        ctx.beginPath()
                        ctx.arc(cx, cy, outerRadius - 4, startAngle, endAngle)
                        ctx.stroke()
                        
                        // 速度弧
                        var arcColor = speedRatio <= 0.6 ? colorAccent : 
                                       (speedRatio <= 0.85 ? colorWarning : colorDanger)
                        ctx.strokeStyle = arcColor
                        ctx.lineWidth = 8
                        ctx.lineCap = "round"
                        ctx.beginPath()
                        ctx.arc(cx, cy, outerRadius - 4, startAngle, currentAngle)
                        ctx.stroke()
                        
                        // 速度指针
                        ctx.save()
                        ctx.translate(cx, cy)
                        ctx.rotate(currentAngle + Math.PI / 2)
                        
                        ctx.fillStyle = colorTextPrimary
                        ctx.beginPath()
                        ctx.moveTo(0, -innerRadius + 15)
                        ctx.lineTo(-4, 8)
                        ctx.lineTo(4, 8)
                        ctx.closePath()
                        ctx.fill()
                        
                        ctx.beginPath()
                        ctx.arc(0, 0, 6, 0, Math.PI * 2)
                        ctx.fillStyle = colorBorderActive
                        ctx.fill()
                        
                        ctx.restore()
                        
                        // 转向指示
                        if (Math.abs(steerValue) > 1) {
                            var steerAngle = steerValue * Math.PI / 180 * 0.5
                            ctx.save()
                            ctx.translate(cx, cy + 25)
                            ctx.rotate(steerAngle)
                            
                            ctx.fillStyle = "#70B0FF"
                            ctx.beginPath()
                            ctx.moveTo(0, -12)
                            ctx.lineTo(-6, 4)
                            ctx.lineTo(6, 4)
                            ctx.closePath()
                            ctx.fill()
                            ctx.restore()
                        }
                    }
                }
                
                Column {
                    anchors.centerIn: parent
                    spacing: 0
                    
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Math.round(displaySpeed).toString()
                        color: colorTextPrimary
                        font.pixelSize: speedometerBg.width * 0.28
                        font.bold: true
                    }
                    
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "km/h"
                        color: colorTextSecondary
                        font.pixelSize: speedometerBg.width * 0.1
                    }
                    
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "↻ " + displaySteering.toFixed(0) + "°"
                        color: "#70B0FF"
                        font.pixelSize: speedometerBg.width * 0.09
                        font.family: chineseFont || font.family
                        visible: Math.abs(displaySteering) > 1
                    }
                    
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "档位 " + displayGear
                        color: "#9CB2DF"
                        font.pixelSize: speedometerBg.width * 0.09
                        font.bold: true
                        font.family: chineseFont || font.family
                    }
                }
            }
        }
        
        // 右侧：清扫功能控制
        ColumnLayout {
            Layout.preferredWidth: centerColAllocW * controlSideColumnRatio
            Layout.fillHeight: true
            spacing: 0
            Item { Layout.fillHeight: true }
            
            GridLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(parent.height * 0.85, 190)
                Layout.alignment: Qt.AlignVCenter
                columns: 3
                rowSpacing: 4
                columnSpacing: 4
                
                CustomComponents.ControlButton {
                    label: "清扫"
                    active: sweepActive
                    onClicked: {
                        var remoteControlEnabled = false
                        if (AppContext.vehicleStatus) {
                            remoteControlEnabled = (vehicleStatus.drivingMode === "远驾")
                        }
                        if (!remoteControlEnabled) {
                            console.log("[Client][UI][Sweep] ⚠ 远驾接管未启用，无法发送清扫命令")
                            return
                        }
                        sweepActive = !sweepActive
                        sendControlCommand("sweep", { name: "sweep", active: sweepActive })
                    }
                }
                CustomComponents.ControlButton {
                    label: "洒水"
                    active: waterSprayActive
                    onClicked: {
                        waterSprayActive = !waterSprayActive
                        sendControlCommand("sweep", { name: "waterSpray", active: waterSprayActive })
                    }
                }
                CustomComponents.ControlButton {
                    label: "吸污"
                    active: suctionActive
                    onClicked: {
                        suctionActive = !suctionActive
                        sendControlCommand("sweep", { name: "suction", active: suctionActive })
                    }
                }
                CustomComponents.ControlButton {
                    label: "卸料"
                    active: dumpActive
                    onClicked: {
                        dumpActive = !dumpActive
                        sendControlCommand("sweep", { name: "dump", active: dumpActive })
                    }
                }
                CustomComponents.ControlButton {
                    label: "喇叭"
                    active: hornActive
                    onClicked: {
                        hornActive = !hornActive
                        sendControlCommand("sweep", { name: "horn", active: hornActive })
                    }
                }
                CustomComponents.ControlButton {
                    label: "灯光"
                    active: workLampActive
                    onClicked: {
                        workLampActive = !workLampActive
                        sendControlCommand("sweep", { name: "workLamp", active: workLampActive })
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}