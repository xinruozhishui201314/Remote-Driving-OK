import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 连接配置对话框
 */
Popup {
    id: dialog
    
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
    width: 500
    height: 400
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: parent

    property string serverUrl: "http://localhost:8080"
    property string app: "live"
    property string stream: "test"
    property string mqttBroker: "mqtt://localhost:1883"
    property string mqttClientId: "remote_driving_client"

    Rectangle {
        anchors.fill: parent
        color: "#2a2a2a"
        border.color: "#444444"
        border.width: 1
        radius: 5

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15

            Text {
                text: "连接设置"
                color: "#ffffff"
                font.pixelSize: 18
                font.family: dialog.chineseFont || font.family
                font.bold: true
            }

            GroupBox {
                Layout.fillWidth: true
                title: "WebRTC 视频流设置"

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 10

                    Text { text: "服务器地址:"; color: "#ffffff" }
                    TextField {
                        id: serverUrlField
                        Layout.fillWidth: true
                        text: dialog.serverUrl
                        placeholderText: "http://192.168.1.100:8080"
                        color: "#ffffff"
                        background: Rectangle {
                            color: "#1a1a1a"
                            border.color: "#444444"
                            border.width: 1
                            radius: 3
                        }
                    }

                    Text { text: "应用名称:"; color: "#ffffff" }
                    TextField {
                        id: appField
                        Layout.fillWidth: true
                        text: dialog.app
                        placeholderText: "live"
                        color: "#ffffff"
                        background: Rectangle {
                            color: "#1a1a1a"
                            border.color: "#444444"
                            border.width: 1
                            radius: 3
                        }
                    }

                    Text { text: "流名称:"; color: "#ffffff" }
                    TextField {
                        id: streamField
                        Layout.fillWidth: true
                        text: vehicleManager.currentVin || dialog.stream
                        placeholderText: vehicleManager.currentVin || "test"
                        enabled: false
                        color: "#888888"
                        background: Rectangle {
                            color: "#1a1a1a"
                            border.color: "#444444"
                            border.width: 1
                            radius: 3
                        }
                    }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: "MQTT 控制设置"

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 10

                    Text { text: "MQTT Broker:"; color: "#ffffff" }
                    TextField {
                        id: mqttBrokerField
                        Layout.fillWidth: true
                        text: dialog.mqttBroker
                        placeholderText: "mqtt://192.168.1.100:1883"
                        color: "#ffffff"
                        background: Rectangle {
                            color: "#1a1a1a"
                            border.color: "#444444"
                            border.width: 1
                            radius: 3
                        }
                    }

                    Text { text: "客户端 ID:"; color: "#ffffff" }
                    TextField {
                        id: mqttClientIdField
                        Layout.fillWidth: true
                        text: dialog.mqttClientId
                        placeholderText: "remote_driving_client"
                        color: "#ffffff"
                        background: Rectangle {
                            color: "#1a1a1a"
                            border.color: "#444444"
                            border.width: 1
                            radius: 3
                        }
                    }
                }
            }

            // 当前车辆信息
            Rectangle {
                Layout.fillWidth: true
                height: 40
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
                font.family: dialog.chineseFont || font.family
                    }

                    Text {
                        Layout.fillWidth: true
                        text: vehicleManager.currentVehicleName || vehicleManager.currentVin
                        color: "#00ff00"
                        font.pixelSize: 12
                font.family: dialog.chineseFont || font.family
                        font.bold: true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Button {
                    Layout.fillWidth: true
                    text: "连接"
                    enabled: vehicleManager.currentVin.length > 0
                    onClicked: {
                        // 设置当前 VIN
                        mqttController.setCurrentVin(vehicleManager.currentVin)
                        
                        // 设置 WebRTC（使用车辆 VIN 作为流名称）
                        var streamName = vehicleManager.currentVin || streamField.text
                        webrtcClient.connectToStream(
                            serverUrlField.text,
                            appField.text,
                            streamName
                        )

                        // 设置并连接 MQTT
                        mqttController.brokerUrl = mqttBrokerField.text
                        mqttController.clientId = mqttClientIdField.text
                        mqttController.connectToBroker()
                        
                        // 连接成功后，请求车端开始推流并上传底盘数据
                        // 注意：requestStreamStart 会在 MQTT 连接成功后自动调用（通过信号连接）
                        // 但为了确保立即触发，我们也可以在这里延迟调用
                        Qt.callLater(function() {
                            if (mqttController.isConnected) {
                                mqttController.requestStreamStart()
                            }
                        })

                        dialog.close()
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: "取消"
                    onClicked: dialog.close()
                }
            }
        }
    }
}
