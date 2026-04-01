import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 状态栏组件
 * 显示系统状态信息
 */
Rectangle {
    id: statusBar
    // 四路视频以 webrtcStreamManager.anyConnected 为准；独立 webrtcClient 仅兼容单路演示
    function videoStreamsConnected() {
        if (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected)
            return true
        if (typeof webrtcClient !== "undefined" && webrtcClient && webrtcClient.isConnected)
            return true
        return false
    }
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#1E1E2E" }
        GradientStop { position: 1.0; color: "#0F0F1A" }
    }
    border.color: "#2A2A3E"
    border.width: 1
    
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

    RowLayout {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 20

        // 连接状态指示器（美化）
        Rectangle {
            width: 14
            height: 14
            radius: 7
            color: {
                var webrtc = statusBar.videoStreamsConnected()
                var mqtt = typeof mqttController !== "undefined" && mqttController ? mqttController.isConnected : false
                if (webrtc && mqtt) {
                    return "#50C878"  // 绿色：完全连接
                } else if (webrtc || mqtt) {
                    return "#FFAA00"  // 橙色：部分连接
                } else {
                    return "#FF6B6B"  // 红色：未连接
                }
            }
            border.color: "#FFFFFF"
            border.width: 1
            
            // 脉冲动画（如果连接）
            SequentialAnimation on opacity {
                running: {
                    var webrtc = statusBar.videoStreamsConnected()
                    var mqtt = typeof mqttController !== "undefined" && mqttController ? mqttController.isConnected : false
                    return webrtc && mqtt
                }
                loops: Animation.Infinite
                NumberAnimation { to: 0.5; duration: 1000 }
                NumberAnimation { to: 1.0; duration: 1000 }
            }
        }

        Text {
            text: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.connectionStatus : "未连接"
            color: "#FFFFFF"
            font.pixelSize: 13
            font.bold: true
            font.family: statusBar.chineseFont || font.family
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: "#444444"
        }

        // 视频状态
        RowLayout {
            spacing: 6
            
            Text {
                text: {
                    var connected = statusBar.videoStreamsConnected()
                    return connected ? "✓" : "✗"
                }
                color: {
                    var connected = statusBar.videoStreamsConnected()
                    return connected ? "#50C878" : "#FF6B6B"
                }
                font.pixelSize: 14
                font.bold: true
            }
            
            Text {
                text: "视频"
                color: "#B0B0B0"
                font.pixelSize: 12
                font.family: statusBar.chineseFont || font.family
            }
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: "#444444"
        }

        // MQTT 状态
        RowLayout {
            spacing: 6
            
            Text {
                text: {
                    var connected = typeof mqttController !== "undefined" && mqttController && mqttController.isConnected
                    return connected ? "✓" : "✗"
                }
                color: {
                    var connected = typeof mqttController !== "undefined" && mqttController && mqttController.isConnected
                    return connected ? "#50C878" : "#FF6B6B"
                }
                font.pixelSize: 14
                font.bold: true
            }
            
            Text {
                text: "控制"
                color: "#B0B0B0"
                font.pixelSize: 12
                font.family: statusBar.chineseFont || font.family
            }
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: "#444444"
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
                    if (typeof vehicleManager !== "undefined" && vehicleManager) {
                        return vehicleManager.currentVehicleName || vehicleManager.currentVin || "未选择"
                    }
                    return "未选择"
                }
                color: {
                    if (typeof vehicleManager !== "undefined" && vehicleManager && vehicleManager.currentVin.length > 0) {
                        return "#50C878"
                    }
                    return "#FF6B6B"
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
            color: "#444444"
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
                    if (typeof vehicleStatus !== "undefined" && vehicleStatus) {
                        return vehicleStatus.speed.toFixed(1) + " km/h"
                    }
                    return "0.0 km/h"
                }
                color: "#FFFFFF"
                font.pixelSize: 13
                font.bold: true
                font.family: statusBar.chineseFont || font.family
            }
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: "#444444"
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
                    if (typeof vehicleStatus !== "undefined" && vehicleStatus) {
                        return vehicleStatus.batteryLevel.toFixed(0) + "%"
                    }
                    return "100%"
                }
                color: {
                    if (typeof vehicleStatus !== "undefined" && vehicleStatus && vehicleStatus.batteryLevel > 20) {
                        return "#50C878"
                    }
                    return "#FF6B6B"
                }
                font.pixelSize: 13
                font.bold: true
                font.family: statusBar.chineseFont || font.family
            }
        }

        Item {
            Layout.fillWidth: true
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: "#444444"
        }

        // 退出登录（主界面顶栏）
        Button {
            text: "退出登录"
            font.pixelSize: 12
            font.family: statusBar.chineseFont || font.family
            visible: typeof authManager !== "undefined" && authManager && authManager.isLoggedIn
            onClicked: {
                if (typeof authManager !== "undefined" && authManager) {
                    console.log("[Client][UI] 状态栏点击「退出登录」")
                    authManager.logout()
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: parent.enabled ? "#E0E0E0" : "#666666"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 4
                color: parent.pressed ? "#5A2A2A" : (parent.hovered ? "#6B3333" : "transparent")
                border.color: parent.hovered ? "#884444" : "#444444"
                border.width: 1
            }
        }

        Rectangle {
            width: 2
            height: parent.height * 0.6
            radius: 1
            color: "#444444"
        }

        // 时间
        Text {
            text: "🕐 " + Qt.formatDateTime(new Date(), "hh:mm:ss")
            color: "#B0B0B0"
            font.pixelSize: 12
            font.family: statusBar.chineseFont || font.family
        }
    }
}
