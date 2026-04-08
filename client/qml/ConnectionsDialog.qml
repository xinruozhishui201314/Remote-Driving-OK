import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "styles" as ThemeModule

/**
 * 连接配置对话框
 * 统一使用 AppContext 和 Theme
 */
Popup {
    id: dialog
    
    // ── 统一属性 ─────────────────────────────────────────────────────
    readonly property string chineseFont: AppContext ? AppContext.chineseFont : ""
    readonly property var vehicleManager: AppContext ? AppContext.vehicleManager : null
    readonly property var mqttController: AppContext ? AppContext.mqttController : null
    readonly property var webrtcClient: AppContext ? AppContext.webrtcClient : null
    
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
        color: ThemeModule.Theme.colorSurface
        border.color: ThemeModule.Theme.colorBorder
        border.width: 1
        radius: 8

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15

            Text {
                text: "连接设置"
                color: ThemeModule.Theme.colorText
                font.pixelSize: 18
                font.family: dialog.chineseFont || font.family
                font.bold: true
            }

            GroupBox {
                id: webrtcGroupBox
                Layout.fillWidth: true
                title: "WebRTC 视频流设置"
                // GroupBox.label 类型为 Item（非 Text），不能写 label.font.*；见 Qt 文档「Customizing GroupBox」
                label: Label {
                    text: webrtcGroupBox.title
                    color: ThemeModule.Theme.colorText
                    font.family: dialog.chineseFont || font.family
                }

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 10

                    Text { 
                        text: "服务器地址:"; 
                        color: ThemeModule.Theme.colorText
                        font.family: dialog.chineseFont || font.family
                    }
                    TextField {
                        id: serverUrlField
                        Layout.fillWidth: true
                        text: dialog.serverUrl
                        placeholderText: "http://192.168.1.100:8080"
                        color: ThemeModule.Theme.colorText
                        placeholderTextColor: ThemeModule.Theme.colorTextDim
                        background: Rectangle {
                            color: ThemeModule.Theme.colorButtonBg
                            border.color: ThemeModule.Theme.colorBorder
                            border.width: 1
                            radius: 3
                        }
                    }

                    Text { 
                        text: "应用名称:"; 
                        color: ThemeModule.Theme.colorText
                        font.family: dialog.chineseFont || font.family
                    }
                    TextField {
                        id: appField
                        Layout.fillWidth: true
                        text: dialog.app
                        placeholderText: "live"
                        color: ThemeModule.Theme.colorText
                        placeholderTextColor: ThemeModule.Theme.colorTextDim
                        background: Rectangle {
                            color: ThemeModule.Theme.colorButtonBg
                            border.color: ThemeModule.Theme.colorBorder
                            border.width: 1
                            radius: 3
                        }
                    }

                    Text { 
                        text: "流名称:"; 
                        color: ThemeModule.Theme.colorText
                        font.family: dialog.chineseFont || font.family
                    }
                    TextField {
                        id: streamField
                        Layout.fillWidth: true
                        text: dialog.vehicleManager ? (dialog.vehicleManager.currentVin || dialog.stream) : dialog.stream
                        placeholderText: dialog.vehicleManager ? (dialog.vehicleManager.currentVin || "test") : "test"
                        enabled: false
                        color: ThemeModule.Theme.colorTextDim
                        background: Rectangle {
                            color: ThemeModule.Theme.colorButtonBg
                            border.color: ThemeModule.Theme.colorBorder
                            border.width: 1
                            radius: 3
                        }
                    }
                }
            }

            GroupBox {
                id: mqttGroupBox
                Layout.fillWidth: true
                title: "MQTT 控制设置"
                label: Label {
                    text: mqttGroupBox.title
                    color: ThemeModule.Theme.colorText
                    font.family: dialog.chineseFont || font.family
                }

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 10

                    Text { 
                        text: "MQTT Broker:"; 
                        color: ThemeModule.Theme.colorText
                        font.family: dialog.chineseFont || font.family
                    }
                    TextField {
                        id: mqttBrokerField
                        Layout.fillWidth: true
                        text: dialog.mqttBroker
                        placeholderText: "mqtt://192.168.1.100:1883"
                        color: ThemeModule.Theme.colorText
                        placeholderTextColor: ThemeModule.Theme.colorTextDim
                        background: Rectangle {
                            color: ThemeModule.Theme.colorButtonBg
                            border.color: ThemeModule.Theme.colorBorder
                            border.width: 1
                            radius: 3
                        }
                    }

                    Text { 
                        text: "客户端 ID:"; 
                        color: ThemeModule.Theme.colorText
                        font.family: dialog.chineseFont || font.family
                    }
                    TextField {
                        id: mqttClientIdField
                        Layout.fillWidth: true
                        text: dialog.mqttClientId
                        placeholderText: "remote_driving_client"
                        color: ThemeModule.Theme.colorText
                        placeholderTextColor: ThemeModule.Theme.colorTextDim
                        background: Rectangle {
                            color: ThemeModule.Theme.colorButtonBg
                            border.color: ThemeModule.Theme.colorBorder
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
                color: ThemeModule.Theme.colorButtonBg
                border.color: ThemeModule.Theme.colorBorder
                border.width: 1
                radius: 3
                visible: dialog.vehicleManager && dialog.vehicleManager.currentVin && dialog.vehicleManager.currentVin.length > 0

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10

                    Text {
                        text: "当前车辆:"
                        color: ThemeModule.Theme.colorText
                        font.pixelSize: 12
                        font.family: dialog.chineseFont || font.family
                    }

                    Text {
                        Layout.fillWidth: true
                        text: dialog.vehicleManager ? (dialog.vehicleManager.currentVehicleName || dialog.vehicleManager.currentVin) : ""
                        color: ThemeModule.Theme.colorGood
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
                    enabled: dialog.vehicleManager && dialog.vehicleManager.currentVin && dialog.vehicleManager.currentVin.length > 0
                    background: Rectangle {
                        radius: 6
                        color: parent.enabled ? (parent.pressed ? ThemeModule.Theme.colorPrimary : (parent.hovered ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorBorderActive)) : ThemeModule.Theme.colorBorder
                    }
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 14
                        font.family: dialog.chineseFont || font.family
                        color: parent.enabled ? ThemeModule.Theme.colorText : ThemeModule.Theme.colorTextDim
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        var mc = dialog.mqttController
                        var wc = dialog.webrtcClient
                        var vm = dialog.vehicleManager
                        
                        if (mc) {
                            mc.setCurrentVin(vm ? vm.currentVin : "")
                        }
                        
                        if (wc) {
                            var streamName = (vm && vm.currentVin) ? vm.currentVin : streamField.text
                            wc.connectToStream(serverUrlField.text, appField.text, streamName)
                        }
                        
                        if (mc) {
                            mc.brokerUrl = mqttBrokerField.text
                            mc.clientId = mqttClientIdField.text
                            mc.connectToBroker()
                        }
                        
                        Qt.callLater(function() {
                            if (mc && mc.isConnected) {
                                mc.requestStreamStart()
                            }
                        })

                        dialog.close()
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: "取消"
                    background: Rectangle {
                        radius: 6
                        color: parent.pressed ? ThemeModule.Theme.colorBorder : (parent.hovered ? ThemeModule.Theme.colorButtonBgHover : ThemeModule.Theme.colorButtonBg)
                        border.color: ThemeModule.Theme.colorBorder
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 14
                        font.family: dialog.chineseFont || font.family
                        color: ThemeModule.Theme.colorText
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: dialog.close()
                }
            }
        }
    }
}
