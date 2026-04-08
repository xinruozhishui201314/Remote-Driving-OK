import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtMultimedia
import RemoteDriving 1.0
import "../styles" as ThemeModule

/**
 * 驾驶界面 - 视频面板部分
 * 从 DrivingInterface.qml 提取，包含四路视频面板和主视图
 * 共享状态通过 AppContext 传递
 */
Rectangle {
    id: videoPanels
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 主题颜色（从 DrivingInterface.qml 继承）
    // ═══════════════════════════════════════════════════════════════════════════
    readonly property color colorPanel: ThemeModule.Theme.drivingColorPanel
    readonly property color colorBorder: ThemeModule.Theme.drivingColorBorder
    readonly property color colorBorderActive: ThemeModule.Theme.drivingColorBorderActive
    readonly property color colorTextPrimary: ThemeModule.Theme.drivingColorTextPrimary
    readonly property color colorTextSecondary: ThemeModule.Theme.drivingColorTextSecondary
    readonly property color colorAccent: ThemeModule.Theme.drivingColorAccent
    readonly property color colorWarning: ThemeModule.Theme.drivingColorWarning
    readonly property color colorDanger: ThemeModule.Theme.drivingColorDanger
    readonly property string chineseFont: AppContext.chineseFont || ""
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 布局比例常量
    // ═══════════════════════════════════════════════════════════════════════════
    readonly property real mainRowAvailH: {
        var base = (drivingInterface ? drivingInterface.height : 720)
        if (rootColumnLayout && topBarRect && rootColumnLayout.height > 0)
            return Math.max(200, rootColumnLayout.height - topBarRect.height - rootColumnLayout.spacing)
        return Math.max(200, base * (17/18))
    }
    readonly property int sideColMinWidth: 180
    readonly property int sideColMaxWidth: 390
    readonly property int sideColTopMinHeight: 100
    readonly property real leftVideoRatio: 0.58
    readonly property real leftMapRatio: 0.40
    
    // 引用父组件属性
    property var drivingInterface: null
    property var rootColumnLayout: null
    property var topBarRect: null
    property var sideColAllocW: 0
    property var mainRowSpacing: 8
    property var forwardMode: true
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 视频面板组件（从 VideoPanel.qml 复制并适配）
    // ═══════════════════════════════════════════════════════════════════════════
    component DrivingVideoPanel: Rectangle {
        id: drivingVideoPanelRoot
        property string title: ""
        property bool showPlaceholder: true
        property QtObject streamClient: null
        property bool hasVideoFrame: false
        property int placeholderClearDelay: 2500
        property bool isLayoutDebugEnabled: (typeof layoutDebugEnabled !== "undefined" && layoutDebugEnabled) || false
        property QtObject _prevVideoBindClient: null

        function rebindSidePanelVideoOutput() {
            if (_prevVideoBindClient && _prevVideoBindClient !== streamClient) {
                _prevVideoBindClient.bindVideoOutput(null)
                _prevVideoBindClient = null
            }
            if (streamClient && videoOutSide) {
                streamClient.bindVideoOutput(videoOutSide)
                _prevVideoBindClient = streamClient
            }
        }

        // streamClient 变化诊断
        onStreamClientChanged: {
            rebindSidePanelVideoOutput()
            var info = (streamClient !== null && streamClient !== undefined)
                ? ("ptr=" + streamClient + " type=" + (streamClient.metaObject ? streamClient.metaObject().className() : "N/A"))
                : "null/undefined"
            console.warn("[Client][UI][Video] VideoPanel[" + title + "] onStreamClientChanged streamClient=" + info)
        }
        
        radius: 6
        color: colorPanel
        border.color: colorBorderActive
        border.width: 1
        
        // 延迟清除 hasVideoFrame
        Timer {
            id: placeholderClearTimer
            interval: placeholderClearDelay
            repeat: false
            onTriggered: {
                hasVideoFrame = false
                console.log("[Client][UI][Video] VideoPanel placeholder 显示（连接断开≥" + placeholderClearDelay/1000 + "s）")
            }
        }
        
        Column {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4
            
            Text {
                text: title
                color: colorTextPrimary
                font.pixelSize: 12
                font.bold: true
                font.family: chineseFont || font.family
            }
            
            Item {
                width: parent.width
                height: parent.height - 24
                clip: true
                
                VideoOutput {
                    id: videoOutSide
                    anchors.fill: parent
                    z: 5
                    fillMode: VideoOutput.PreserveAspectCrop
                    Component.onCompleted: drivingVideoPanelRoot.rebindSidePanelVideoOutput()
                }

                Connections {
                    target: streamClient
                    ignoreUnknownSignals: true

                    function onVideoFrameReady(image, frameWidth, frameHeight, frameId) {
                        var hasValidSize = (frameWidth > 0 && frameHeight > 0)
                        if (hasValidSize) {
                            placeholderClearTimer.stop()
                            hasVideoFrame = true
                        }
                    }

                    function onConnectionStatusChanged(connected) {
                        if (connected) {
                            placeholderClearTimer.stop()
                        } else {
                            placeholderClearTimer.restart()
                        }
                    }
                }
                
                // 占位图标
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    visible: showPlaceholder && !hasVideoFrame
                    z: 15
                    Text {
                        anchors.centerIn: parent
                        text: "📹"
                        font.pixelSize: 28
                        color: colorBorderActive
                    }
                }
                
                // 连接状态文字
                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 4
                    z: 16
                    text: streamClient ? (streamClient.isConnected ? "视频已连接" : (streamClient.statusText || "连接中...")) : "等待连接"
                    color: streamClient && streamClient.isConnected ? colorAccent : colorTextSecondary
                    font.pixelSize: 11
                    font.family: chineseFont || font.family
                }
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 左列：左视图 + 后视图
    // ═══════════════════════════════════════════════════════════════════════════
    ColumnLayout {
        id: leftColLayout
        Layout.preferredWidth: Math.min(sideColMaxWidth, Math.max(sideColMinWidth, sideColAllocW))
        Layout.minimumWidth: sideColMinWidth
        Layout.maximumWidth: sideColMaxWidth
        Layout.fillWidth: false
        Layout.fillHeight: true
        Layout.minimumHeight: Math.max(400, mainRowAvailH * 0.9)
        Layout.maximumHeight: mainRowAvailH
        spacing: 4
        
        DrivingVideoPanel {
            id: leftFrontPanel
            Layout.fillWidth: true
            Layout.preferredHeight: mainRowAvailH * leftVideoRatio
            Layout.minimumHeight: sideColTopMinHeight
            title: "左视图"
            streamClient: AppContext.webrtcStreamManager ? AppContext.webrtcStreamManager.leftClient : null
        }
        
        DrivingVideoPanel {
            id: leftRearPanel
            Layout.fillWidth: true
            Layout.preferredHeight: mainRowAvailH * leftMapRatio
            Layout.minimumHeight: 120
            title: "后视图"
            streamClient: AppContext.webrtcStreamManager ? AppContext.webrtcStreamManager.rearClient : null
        }
    }
    
    // ═══════════════════════════���═══════════════════════════════════════════════
    // 右列：右视图 + 高精地图
    // ═══════════════════════════════════════════════════════════════════════════
    ColumnLayout {
        id: rightColLayout
        Layout.preferredWidth: Math.min(sideColMaxWidth, Math.max(sideColMinWidth, sideColAllocW))
        Layout.minimumWidth: sideColMinWidth
        Layout.maximumWidth: sideColMaxWidth
        Layout.fillWidth: false
        Layout.fillHeight: true
        Layout.minimumHeight: Math.max(400, mainRowAvailH * 0.9)
        Layout.maximumHeight: mainRowAvailH
        spacing: 4
        
        DrivingVideoPanel {
            id: rightViewVideo
            Layout.fillWidth: true
            Layout.preferredHeight: mainRowAvailH * leftVideoRatio
            Layout.minimumHeight: sideColTopMinHeight
            title: "右视图"
            streamClient: AppContext.webrtcStreamManager ? AppContext.webrtcStreamManager.rightClient : null
        }
        
        Rectangle {
            id: hdMapRect
            Layout.fillWidth: true
            Layout.preferredHeight: mainRowAvailH * leftMapRatio
            Layout.minimumHeight: 120
            radius: 6
            color: colorPanel
            border.color: colorBorderActive
            border.width: 1
            
            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4
                
                Text {
                    text: "高精地图"
                    color: colorTextPrimary
                    font.pixelSize: 12
                    font.bold: true
                    font.family: chineseFont || font.family
                }
                
                Rectangle {
                    width: parent.width
                    height: parent.height - 28
                    radius: 4
                    color: ThemeModule.Theme.drivingColorBackground
                    
                    Canvas {
                        id: hdMapCanvas
                        anchors.fill: parent
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.strokeStyle = colorBorderActive
                            ctx.lineWidth = 1
                            
                            // 网格
                            ctx.globalAlpha = 0.3
                            var gridSize = 30
                            for (var x = gridSize; x < width; x += gridSize) {
                                ctx.beginPath()
                                ctx.moveTo(x, 0)
                                ctx.lineTo(x, height)
                                ctx.stroke()
                            }
                            for (var y = gridSize; y < height; y += gridSize) {
                                ctx.beginPath()
                                ctx.moveTo(0, y)
                                ctx.lineTo(width, y)
                                ctx.stroke()
                            }
                            ctx.globalAlpha = 1
                            
                            // 道路
                            ctx.strokeStyle = "#4A90E2"
                            ctx.lineWidth = 3
                            ctx.beginPath()
                            ctx.moveTo(width * 0.1, height * 0.5)
                            ctx.lineTo(width * 0.9, height * 0.5)
                            ctx.stroke()
                            
                            ctx.beginPath()
                            ctx.moveTo(width * 0.5, height * 0.1)
                            ctx.lineTo(width * 0.5, height * 0.9)
                            ctx.stroke()
                            
                            // 车辆位置
                            var vx = width * 0.5
                            var vy = height * 0.5
                            if (AppContext.vehicleStatus && 
                                typeof vehicleStatus.mapX === "number" && typeof vehicleStatus.mapY === "number") {
                                vx = width * Math.max(0, Math.min(1, vehicleStatus.mapX))
                                vy = height * Math.max(0, Math.min(1, vehicleStatus.mapY))
                            }
                            
                            ctx.fillStyle = colorAccent
                            ctx.beginPath()
                            ctx.moveTo(vx, vy - 10)
                            ctx.lineTo(vx - 7, vy + 5)
                            ctx.lineTo(vx + 7, vy + 5)
                            ctx.closePath()
                            ctx.fill()
                            
                            // 比例尺
                            ctx.fillStyle = colorTextSecondary
                            ctx.font = "10px sans-serif"
                            ctx.fillText("100m", width - 35, height - 10)
                        }
                    }
                    
                    Connections {
                        target: vehicleStatus
                        ignoreUnknownSignals: true
                        function onMapXChanged() { hdMapCanvas.requestPaint() }
                        function onMapYChanged() { hdMapCanvas.requestPaint() }
                    }
                }
            }
        }
    }
}
