/**
 * 完整驾驶 HUD
 * 布局：主视频 + 速度表 + 方向盘指示 + 挡位 + 网络条 + 安全叠层
 * 绑定 MVVM 模型：TelemetryModel / NetworkStatusModel / SafetyStatusModel
 * 统一使用 Theme
 */
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtMultimedia
import RemoteDriving 1.0
import "components"
import "styles" as ThemeModule

Item {
    id: root

    // 外部绑定
    required property var telemetryModel
    required property var networkStatusModel
    required property var safetyStatusModel
    /** WebRtcClient*：主路视频，经 bindVideoOutput 输出到 VideoOutput 内置 sink */
    required property var videoStreamClient
    property var _prevHudVideoBindClient: null

    function rebindHudVideoOutput() {
        if (_prevHudVideoBindClient && _prevHudVideoBindClient !== videoStreamClient) {
            _prevHudVideoBindClient.bindVideoOutput(null)
            _prevHudVideoBindClient = null
        }
        if (videoStreamClient && hudVideoOut) {
            videoStreamClient.bindVideoOutput(hudVideoOut)
            _prevHudVideoBindClient = videoStreamClient
        }
    }

    onVideoStreamClientChanged: rebindHudVideoOutput()

    // 控制信号
    signal emergencyStopRequested()
    signal keyPressed(int key)
    signal keyReleased(int key)

    focus: true
    
    // ── 便捷属性 ─────────────────────────────────────────────────────
    readonly property string chineseFont: AppContext ? AppContext.chineseFont : ""
    readonly property var theme: ThemeModule.Theme

    // 背景
    Rectangle {
        anchors.fill: parent
        color: "transparent"
    }

    // 主视频区（Qt Multimedia）
    VideoOutput {
        id: hudVideoOut
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop
        Component.onCompleted: root.rebindHudVideoOutput()
    }

    // 顶部状态栏
    Rectangle {
        id: topBar
        anchors.top:    parent.top
        anchors.left:   parent.left
        anchors.right:  parent.right
        height:  theme.topBarHeight
        color:   Qt.rgba(0, 0, 0, 0.6)
        radius:  4

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin:  theme.marginLarge
            anchors.rightMargin: theme.marginLarge

            Text {
                text:  "状态: " + safetyStatusModel.systemState
                color: safetyStatusModel.allSafe ? theme.colorGood : theme.colorWarn
                font.pixelSize: theme.fontNormal
                font.bold: true
                font.family: root.chineseFont || font.family
            }

            Item { Layout.fillWidth: true }

            NetworkStatusBar {
                networkModel: networkStatusModel
                height: 24
                width: 150
            }

            Item { Layout.fillWidth: true }

            Text {
                text:  "RTT: " + networkStatusModel.rttMs.toFixed(0) + " ms"
                color: networkStatusModel.rttMs > 150 ? theme.colorWarn :
                       networkStatusModel.rttMs > 80  ? theme.colorCaution : theme.colorGood
                font.pixelSize: theme.fontNormal
                font.family: root.chineseFont || font.family
            }

            Text {
                id: clockText
                color: theme.colorText
                font.pixelSize: theme.fontSmall
                font.family: root.chineseFont || font.family

                Timer {
                    interval: 1000; running: true; repeat: true
                    onTriggered: clockText.text = Qt.formatDateTime(new Date(), "HH:mm:ss")
                }
            }
        }
    }

    // 底部 HUD 面板
    Rectangle {
        id: bottomHUD
        anchors.bottom: parent.bottom
        anchors.left:   parent.left
        anchors.right:  parent.right
        height:  theme.bottomHudHeight
        color:   Qt.rgba(0, 0, 0, 0.65)

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin:  theme.marginLarge
            anchors.rightMargin: theme.marginLarge
            spacing: theme.marginLarge

            SpeedGauge {
                speed: telemetryModel.speed
                maxSpeed: 120
                height: theme.gaugeSize
                width:  theme.gaugeSize
                Layout.alignment: Qt.AlignVCenter
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 4

                Text { 
                    text: "油门"; 
                    color: theme.colorText; 
                    font.pixelSize: theme.fontSmall
                    font.family: root.chineseFont || font.family
                }
                ProgressBar {
                    value: telemetryModel.throttle
                    width: 120; height: 10
                    contentItem: Rectangle { color: theme.colorGood; radius: 4; width: parent.width * parent.value }
                }
                Text { 
                    text: "刹车"; 
                    color: theme.colorText; 
                    font.pixelSize: theme.fontSmall
                    font.family: root.chineseFont || font.family
                }
                ProgressBar {
                    value: telemetryModel.brake
                    width: 120; height: 10
                    contentItem: Rectangle { color: theme.colorDanger; radius: 4; width: parent.width * parent.value }
                }
            }

            SteeringIndicator {
                steeringAngle: telemetryModel.steering
                height: theme.gaugeSize
                width:  theme.gaugeSize
                Layout.alignment: Qt.AlignVCenter
            }

            GearIndicator {
                gear: telemetryModel.gear
                height: theme.gaugeSize
                width:  theme.gaugeSize * 0.6
                Layout.alignment: Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            Column {
                Layout.alignment: Qt.AlignVCenter
                Text {
                    text:  "🔋 " + telemetryModel.battery.toFixed(0) + "%"
                    color: telemetryModel.battery < 20 ? theme.colorWarn : theme.colorText
                    font.pixelSize: theme.fontNormal
                    font.family: root.chineseFont || font.family
                }
            }
        }
    }

    // 安全警告叠层
    SafetyWarningOverlay {
        anchors.fill: parent
        safetyModel: safetyStatusModel
        visible: !safetyStatusModel.allSafe || safetyStatusModel.emergencyStop
    }

    // 急停按钮
    Rectangle {
        anchors.right:  parent.right
        anchors.bottom: bottomHUD.top
        anchors.rightMargin:  theme.marginLarge
        anchors.bottomMargin: theme.marginSmall
        width:  80; height: 80
        radius: 40
        color:  emergencyArea.containsMouse ? "#CC0000" : "#FF0000"
        border.color: "white"; border.width: 3

        Text { 
            anchors.centerIn: parent; 
            text: "急停"; 
            color: "white"; 
            font.bold: true; 
            font.pixelSize: 16
            font.family: root.chineseFont || font.family
        }

        MouseArea {
            id: emergencyArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.emergencyStopRequested()
        }
    }

    // 键盘事件
    Keys.onPressed: (event) => {
        root.keyPressed(event.key)
        if (event.key === Qt.Key_Escape) root.emergencyStopRequested()
    }
    Keys.onReleased: (event) => root.keyReleased(event.key)
}
