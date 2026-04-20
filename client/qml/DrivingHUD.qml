/**
 * 完整驾驶 HUD
 * 布局：主视频 + 速度表 + 方向盘指示 + 挡位 + 网络条 + 安全叠层
 * 绑定 MVVM 模型：TelemetryModel / NetworkStatusModel / SafetyStatusModel
 * 统一使用 Theme
 */
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "components"
import "styles" as ThemeModule

Item {
    id: root

    // 外部绑定
    required property var telemetryModel
    required property var networkStatusModel
    required property var safetyStatusModel
    /** WebRtcClient*：主路视频，经 bindVideoSurface 输出到 RemoteVideoSurface */
    required property var videoStreamClient
    property var _prevHudVideoBindClient: null

    function rebindHudVideoOutput() {
        if (_prevHudVideoBindClient && _prevHudVideoBindClient !== videoStreamClient) {
            _prevHudVideoBindClient.videoSurface = null
            _prevHudVideoBindClient = null
        }
        if (videoStreamClient && hudVideoOut) {
            videoStreamClient.videoSurface = hudVideoOut
            _prevHudVideoBindClient = videoStreamClient
        }
    }

    onVideoStreamClientChanged: rebindHudVideoOutput()

    // 控制信号
    signal emergencyStopRequested()
    signal emergencyRecoverRequested()
    signal keyPressed(int key)
    signal keyReleased(int key)

    focus: true
    
    // ── 便捷属性 ─────────────────────────────────────────────────────
    readonly property string chineseFont: AppContext ? AppContext.chineseFont : ""
    readonly property var theme: ThemeModule.Theme

    // ── 数据有效性监控 ───────────────────────────────────────────────
    property double currentTime: Date.now()
    Timer {
        interval: 100
        running: true
        repeat: true
        onTriggered: root.currentTime = Date.now()
    }

    readonly property bool telemetryStale: (currentTime - telemetryModel.lastUpdateTimestamp) > 500
    readonly property bool videoFrozen: videoStreamClient ? videoStreamClient.videoFrozen : false

    // 背景
    Rectangle {
        anchors.fill: parent
        color: "transparent"
    }

    // 主视频区（Scene Graph 纹理）
    RemoteVideoSurface {
        id: hudVideoOut
        anchors.fill: parent
        fillMode: RemoteVideoSurface.PreserveAspectCrop
        panelLabel: "DrivingHUD"
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
                    id: throttleBar
                    value: telemetryModel.throttle
                    width: 120; height: 10
                    contentItem: Rectangle { color: theme.colorGood; radius: 4; width: throttleBar.visualPosition * parent.width }
                }
                Text { 
                    text: "刹车"; 
                    color: theme.colorText; 
                    font.pixelSize: theme.fontSmall
                    font.family: root.chineseFont || font.family
                }
                ProgressBar {
                    id: brakeBar
                    value: telemetryModel.brake
                    width: 120; height: 10
                    contentItem: Rectangle { color: theme.colorDanger; radius: 4; width: brakeBar.visualPosition * parent.width }
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

    // ── ★ 架构级安全增强：视频冻结与数据失效监测 ──
    Rectangle {
        id: videoFrozenOverlay
        anchors.fill: parent
        color: Qt.rgba(1, 0, 0, 0.3)
        visible: root.videoFrozen
        z: 9999
        
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 20
            
            Rectangle {
                width: 120; height: 120; radius: 60
                color: "red"
                Layout.alignment: Qt.AlignHCenter
                Text {
                    anchors.centerIn: parent
                    text: "!"
                    color: "white"
                    font.pixelSize: 80
                    font.bold: true
                }
            }
            
            Text {
                text: "视频画面冻结！"
                color: "white"
                font.pixelSize: 32
                font.bold: true
                font.family: root.chineseFont
                Layout.alignment: Qt.AlignHCenter
            }
            
            Text {
                text: "检测到渲染层超过 200ms 无新帧，请立即停止驾驶"
                color: "white"
                font.pixelSize: 18
                font.family: root.chineseFont
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }

    Rectangle {
        id: telemetryStaleOverlay
        anchors.fill: bottomHUD
        color: Qt.rgba(0.2, 0.2, 0.2, 0.8)
        visible: root.telemetryStale
        z: 100
        
        Text {
            anchors.centerIn: parent
            text: "⚠️ 遥测数据失效 (延时 > 500ms)"
            color: "#FFCC00"
            font.pixelSize: 20
            font.bold: true
            font.family: root.chineseFont
        }
    }

    // ★ 架构级安全增强：输入链路静默检测（Watchdog）
    // 解决：UI 键盘有操作（由 SafetyMonitor 捕获）但控制环数据持续为 0（采样器链路断开或焦点丢失）
    Rectangle {
        id: inputSilentOverlay
        anchors.fill: parent
        color: Qt.rgba(0.8, 0.3, 0.3, 0.9) // 醒目但透明的警告色
        z: 10000 // 位于视频层之上
        visible: (appServices.vehicleStatus.drivingMode === "远驾") && 
                 (appServices.vehicleControl ? appServices.vehicleControl.inputLinkSilent : false)

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 30
            
            Rectangle {
                width: 100; height: 100; radius: 50
                color: "white"
                Layout.alignment: Qt.AlignHCenter
                Text {
                    anchors.centerIn: parent
                    text: "⌨!"
                    font.pixelSize: 50
                    color: "#D32F2F"
                }
            }

            Column {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "输入链路无响应"
                    color: "white"
                    font.pixelSize: 36
                    font.bold: true
                    font.family: root.chineseFont
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "检测到键盘操作未生效。请点击下方按钮恢复焦点，或检查网络与外设。"
                    color: "#EEEEEE"
                    font.pixelSize: 20
                    font.family: root.chineseFont
                }
            }

            Rectangle {
                id: resumeFocusBtn
                Layout.preferredWidth: 260
                Layout.preferredHeight: 60
                radius: 30
                color: "white"
                Layout.alignment: Qt.AlignHCenter
                
                Text {
                    anchors.centerIn: parent
                    text: "点击恢复控制焦点"
                    color: "#D32F2F"
                    font.pixelSize: 22
                    font.bold: true
                    font.family: root.chineseFont
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        root.forceActiveFocus()
                        if (appServices && appServices.safetyMonitor && typeof appServices.safetyMonitor.notifyOperatorActivity === "function")
                            appServices.safetyMonitor.notifyOperatorActivity()
                        console.log("[Client][UI][Watchdog] 用户点击恢复焦点")
                    }
                }

                SequentialAnimation on scale {
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 1.05; duration: 600; easing.type: Easing.InOutQuad }
                    NumberAnimation { from: 1.05; to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
                }
            }
        }
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
            onClicked: {
                if (safetyStatusModel.emergencyStop) {
                    root.emergencyRecoverRequested()
                } else {
                    root.emergencyStopRequested()
                }
            }
        }
    }

    // ── ★ 恢复驾驶按钮 ──────────────────────────────────────────────
    Rectangle {
        id: recoverBtn
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: bottomHUD.top
        anchors.bottomMargin: theme.marginLarge * 2
        width:  220; height: 64; radius: 32
        color:  "#2E7D32" // Material Design Green 800
        border.color: "white"; border.width: 2
        visible: safetyStatusModel.emergencyStop
        z: 10001

        RowLayout {
            anchors.centerIn: parent
            spacing: 12
            Text {
                text: "↩"
                color: "white"
                font.pixelSize: 32
            }
            Text {
                text: "恢复远程驾驶"
                color: "white"
                font.pixelSize: 22
                font.bold: true
                font.family: root.chineseFont
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.emergencyRecoverRequested()
        }

        SequentialAnimation on scale {
            running: recoverBtn.visible
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 1.08; duration: 800; easing.type: Easing.InOutQuad }
            NumberAnimation { from: 1.08; to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
        }
    }

    // 键盘事件
    Keys.onPressed: (event) => {
        root.keyPressed(event.key)
        if (event.key === Qt.Key_Escape) root.emergencyStopRequested()
    }
    Keys.onReleased: (event) => root.keyReleased(event.key)
}
