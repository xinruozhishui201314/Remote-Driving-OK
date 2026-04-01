/**
 * 完整驾驶 HUD（《客户端架构设计》§3.4.2）。
 * 布局：主视频 + 速度表 + 方向盘指示 + 挡位 + 网络条 + 安全叠层。
 * 绑定 MVVM 模型：TelemetryModel / NetworkStatusModel / SafetyStatusModel。
 */
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"
import "styles"

Item {
    id: root

    // 外部绑定（由 main.qml 注入）
    required property var telemetryModel
    required property var networkStatusModel
    required property var safetyStatusModel
    required property var videoRenderer

    // 控制信号（发往 VehicleControlService）
    signal emergencyStopRequested()
    signal keyPressed(int key)
    signal keyReleased(int key)

    focus: true

    // ─── 背景（透明叠加）───────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: "transparent"
    }

    // ─── 主视频区 ───────────────────────────────────────────────────────────────
    Loader {
        id: videoArea
        anchors.fill: parent
        sourceComponent: videoRenderer
    }

    // ─── 顶部状态栏 ─────────────────────────────────────────────────────────────
    Rectangle {
        id: topBar
        anchors.top:    parent.top
        anchors.left:   parent.left
        anchors.right:  parent.right
        height:  Theme.topBarHeight
        color:   Qt.rgba(0, 0, 0, 0.6)
        radius:  4

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin:  Theme.marginLarge
            anchors.rightMargin: Theme.marginLarge

            // 系统状态
            Text {
                text:  "状态: " + safetyStatusModel.systemState
                color: safetyStatusModel.allSafe ? Theme.colorGood : Theme.colorWarn
                font.pixelSize: Theme.fontNormal
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            // 网络质量条
            NetworkStatusBar {
                networkModel: networkStatusModel
                height: 24
                width: 150
            }

            Item { Layout.fillWidth: true }

            // 延迟数值
            Text {
                text:  "RTT: " + networkStatusModel.rttMs.toFixed(0) + " ms"
                color: networkStatusModel.rttMs > 150 ? Theme.colorWarn :
                       networkStatusModel.rttMs > 80  ? Theme.colorCaution : Theme.colorGood
                font.pixelSize: Theme.fontNormal
            }

            // 时间
            Text {
                id: clockText
                color: Theme.colorText
                font.pixelSize: Theme.fontSmall

                Timer {
                    interval: 1000; running: true; repeat: true
                    onTriggered: clockText.text = Qt.formatDateTime(new Date(), "HH:mm:ss")
                }
            }
        }
    }

    // ─── 底部 HUD 面板 ──────────────────────────────────────────────────────────
    Rectangle {
        id: bottomHUD
        anchors.bottom: parent.bottom
        anchors.left:   parent.left
        anchors.right:  parent.right
        height:  Theme.bottomHudHeight
        color:   Qt.rgba(0, 0, 0, 0.65)

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin:  Theme.marginLarge
            anchors.rightMargin: Theme.marginLarge
            spacing: Theme.marginLarge

            // 速度表
            SpeedGauge {
                speed: telemetryModel.speed
                maxSpeed: 120
                height: Theme.gaugeSize
                width:  Theme.gaugeSize
                Layout.alignment: Qt.AlignVCenter
            }

            // 油门/刹车进度条
            ColumnLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 4

                Text { text: "油门"; color: Theme.colorText; font.pixelSize: Theme.fontSmall }
                ProgressBar {
                    value: telemetryModel.throttle
                    width: 120; height: 10
                    contentItem: Rectangle { color: "#4CAF50"; radius: 4; width: parent.width * parent.value }
                }
                Text { text: "刹车"; color: Theme.colorText; font.pixelSize: Theme.fontSmall }
                ProgressBar {
                    value: telemetryModel.brake
                    width: 120; height: 10
                    contentItem: Rectangle { color: "#F44336"; radius: 4; width: parent.width * parent.value }
                }
            }

            // 方向盘指示
            SteeringIndicator {
                steeringAngle: telemetryModel.steering
                height: Theme.gaugeSize
                width:  Theme.gaugeSize
                Layout.alignment: Qt.AlignVCenter
            }

            // 挡位
            GearIndicator {
                gear: telemetryModel.gear
                height: Theme.gaugeSize
                width:  Theme.gaugeSize * 0.6
                Layout.alignment: Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            // 电量
            Column {
                Layout.alignment: Qt.AlignVCenter
                Text {
                    text:  "🔋 " + telemetryModel.battery.toFixed(0) + "%"
                    color: telemetryModel.battery < 20 ? Theme.colorWarn : Theme.colorText
                    font.pixelSize: Theme.fontNormal
                }
            }
        }
    }

    // ─── 安全警告叠层 ─────────────────────────────────────────────────────────
    SafetyWarningOverlay {
        anchors.fill: parent
        safetyModel: safetyStatusModel
        visible: !safetyStatusModel.allSafe || safetyStatusModel.emergencyStop
    }

    // ─── 急停按钮 ────────────────────────────────────────────────────────────
    Rectangle {
        anchors.right:  parent.right
        anchors.bottom: bottomHUD.top
        anchors.rightMargin:  Theme.marginLarge
        anchors.bottomMargin: Theme.marginSmall
        width:  80; height: 80
        radius: 40
        color:  emergencyArea.containsMouse ? "#CC0000" : "#FF0000"
        border.color: "white"; border.width: 3

        Text { anchors.centerIn: parent; text: "急停"; color: "white"; font.bold: true; font.pixelSize: 16 }

        MouseArea {
            id: emergencyArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.emergencyStopRequested()
        }
    }

    // ─── 键盘事件 ─────────────────────────────────────────────────────────────
    Keys.onPressed: (event) => {
        root.keyPressed(event.key)
        if (event.key === Qt.Key_Escape) root.emergencyStopRequested()
    }
    Keys.onReleased: (event) => root.keyReleased(event.key)
}
