import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "styles" as ThemeModule
import "components"

/**
 * 状态栏组件
 * 显示系统状态信息
 * 统一使用 AppContext 和 Theme
 */
Rectangle {
    id: statusBar
    
    // ── 统一属性 ─────────────────────────────────────────────────────
    readonly property var vehicleManager: AppContext.vehicleManager
    readonly property var vehicleStatus: AppContext.vehicleStatus
    readonly property var mqttController: AppContext.mqttController
    readonly property var authManager: AppContext.authManager
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

    RowLayout {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 20

        // 连接状态指示器
        Rectangle {
            width: 14
            height: 14
            radius: 7
            color: {
                var webrtc = statusBar.videoStreamsConnected()
                var mqtt = statusBar.mqttController && statusBar.mqttController.mqttBrokerConnected
                if (webrtc && mqtt) {
                    return ThemeModule.Theme.colorGood  // 绿色
                } else if (webrtc || mqtt) {
                    return ThemeModule.Theme.colorCaution  // 橙色
                } else {
                    return ThemeModule.Theme.colorDanger  // 红色
                }
            }
            border.color: ThemeModule.Theme.colorText
            border.width: 1
            
            SequentialAnimation on opacity {
                running: {
                    var webrtc = statusBar.videoStreamsConnected()
                    var mqtt = statusBar.mqttController && statusBar.mqttController.mqttBrokerConnected
                    return webrtc && mqtt
                }
                loops: Animation.Infinite
                NumberAnimation { to: 0.5; duration: 1000 }
                NumberAnimation { to: 1.0; duration: 1000 }
            }
        }

        Text {
            text: (statusBar.vehicleStatus) ? statusBar.vehicleStatus.connectionStatus : "未连接"
            color: ThemeModule.Theme.colorText
            font.pixelSize: 13
            font.bold: true
            font.family: statusBar.chineseFont || font.family
        }

        // 分割线
        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: ThemeModule.Theme.colorBorder
        }

        // 视频状态
        RowLayout {
            spacing: 6
            
            Text {
                text: statusBar.videoStreamsConnected() ? "✓" : "✗"
                color: statusBar.videoStreamsConnected() ? ThemeModule.Theme.colorGood : ThemeModule.Theme.colorDanger
                font.pixelSize: 14
                font.bold: true
            }
            
            Text {
                text: "视频"
                color: ThemeModule.Theme.colorTextDim
                font.pixelSize: 12
                font.family: statusBar.chineseFont || font.family
            }
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: ThemeModule.Theme.colorBorder
        }

        // MQTT 状态
        RowLayout {
            spacing: 6
            
            Text {
                text: {
                    var connected = statusBar.mqttController && statusBar.mqttController.controlChannelReady
                    return connected ? "✓" : "✗"
                }
                color: {
                    var connected = statusBar.mqttController && statusBar.mqttController.controlChannelReady
                    return connected ? ThemeModule.Theme.colorGood : ThemeModule.Theme.colorDanger
                }
                font.pixelSize: 14
                font.bold: true
            }
            
            Text {
                text: "控制"
                color: ThemeModule.Theme.colorTextDim
                font.pixelSize: 12
                font.family: statusBar.chineseFont || font.family
            }
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: ThemeModule.Theme.colorBorder
        }

        // 当前车辆
        RowLayout {
            spacing: 6
            
            Text {
                text: "🚛"
                font.pixelSize: 14
            }
            
            Text {
                text: {
                    var vm = statusBar.vehicleManager
                    if (vm) {
                        return vm.currentVehicleName || vm.currentVin || "未选择"
                    }
                    return "未选择"
                }
                color: {
                    var vm = statusBar.vehicleManager
                    if (vm && vm.currentVin && vm.currentVin.length > 0) {
                        return ThemeModule.Theme.colorGood
                    }
                    return ThemeModule.Theme.colorDanger
                }
                font.pixelSize: 13
                font.bold: true
                font.family: statusBar.chineseFont || font.family
            }
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: ThemeModule.Theme.colorBorder
        }

        // 车辆速度
        RowLayout {
            spacing: 6
            
            Text {
                text: "⚡"
                font.pixelSize: 14
            }
            
            Text {
                text: {
                    var vs = statusBar.vehicleStatus
                    if (vs) {
                        return vs.speed.toFixed(1) + " km/h"
                    }
                    return "0.0 km/h"
                }
                color: ThemeModule.Theme.colorText
                font.pixelSize: 13
                font.bold: true
                font.family: statusBar.chineseFont || font.family
            }
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: ThemeModule.Theme.colorBorder
        }

        // 电池电量
        RowLayout {
            spacing: 6
            
            Text {
                text: "🔋"
                font.pixelSize: 14
            }
            
            Text {
                text: {
                    var vs = statusBar.vehicleStatus
                    if (vs) {
                        return vs.batteryLevel.toFixed(0) + "%"
                    }
                    return "100%"
                }
                color: {
                    var vs = statusBar.vehicleStatus
                    if (vs && vs.batteryLevel > 20) {
                        return ThemeModule.Theme.colorGood
                    }
                    return ThemeModule.Theme.colorDanger
                }
                font.pixelSize: 13
                font.bold: true
                font.family: statusBar.chineseFont || font.family
            }
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: ThemeModule.Theme.colorBorder
        }

        // 故障码指示器
        FaultIndicator {
            Layout.alignment: Qt.AlignVCenter
        }

        Item {
            Layout.fillWidth: true
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: ThemeModule.Theme.colorBorder
        }

        // 退出登录
        Button {
            text: "退出登录"
            font.pixelSize: 12
            font.family: statusBar.chineseFont || font.family
            visible: statusBar.authManager && statusBar.authManager.isLoggedIn
            onClicked: {
                console.log("[Client][UI][StatusBar] 点击退出登录")
                var am = statusBar.authManager
                if (am) {
                    am.logout()
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: parent.enabled ? ThemeModule.Theme.colorText : "#666666"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 4
                color: parent.pressed ? "#5A2A2A" : (parent.hovered ? "#6B3333" : "transparent")
                border.color: parent.hovered ? "#884444" : ThemeModule.Theme.colorBorder
                border.width: 1
            }
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: ThemeModule.Theme.colorBorder
        }

        // 时间
        Text {
            text: "🕐 " + Qt.formatDateTime(new Date(), "hh:mm:ss")
            color: ThemeModule.Theme.colorTextDim
            font.pixelSize: 12
            font.family: statusBar.chineseFont || font.family
        }
    }
}
