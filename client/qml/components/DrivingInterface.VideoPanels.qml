import ".."
import QtQuick 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "../styles" as ThemeModule

/**
 * 驾驶界面 - 视频面板部分（归档 / 未接入主界面 import 链）
 *
 * 从 DrivingInterface.qml 提取，包含四路视频面板和主视图。
 * 共享状态通过 AppContext 传递。
 *
 * Canonical：client/qml/components/driving/* + DrivingInterface（DrivingFacade v2）。
 * 禁止仅在本文交付远驾功能；应 port 至 DrivingLeftRail / DrivingRightRail / DrivingCenterColumn。
 * 见 docs/CLIENT_MODULARIZATION_ASSESSMENT.md §5、docs/CLIENT_UI_MODULE_CONTRACT.md §5。
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
        property WebRtcClient streamClient: null
        property bool hasVideoFrame: false
        property bool everHadVideoFrame: false
        property int placeholderClearDelay: 2500
        property bool isLayoutDebugEnabled: (typeof layoutDebugEnabled !== "undefined" && layoutDebugEnabled) || false
        property WebRtcClient _prevVideoBindClient: null
        property WebRtcClient _streamClientIdentity: null
        property int _sideFrameReadyLogSeq: 0
        property bool _videoOutSideHadBadGeom: false
        property bool _prevEverHadSidePanel: false

        onEverHadVideoFrameChanged: {
            if (_prevEverHadSidePanel && !everHadVideoFrame) {
                console.warn("[Client][UI][Video][LayerFlickerRisk] DrivingVideoPanel[" + title + "] everHadVideoFrame true→false"
                             + " ★ 占位将盖回视频；查 streamClient 是否抖动"
                             + " streamClient=" + (streamClient ? String(streamClient) : "null")
                             + " tMs=" + Date.now())
                AppContext.reportVideoFlickerQmlLayerEvent("DrivingVideoPanel:" + title,
                    "everHad_true_to_false streamClient=" + (streamClient ? String(streamClient) : "null"))
            }
            _prevEverHadSidePanel = everHadVideoFrame
        }

        function diagSideVideoOutputGeom(axis) {
            if (typeof videoOutSide === "undefined" || !videoOutSide)
                return
            var w = videoOutSide.width
            var h = videoOutSide.height
            var bad = (w <= 0 || h <= 0)
            if (bad) {
                if (!_videoOutSideHadBadGeom) {
                    console.warn("[Client][UI][Video][VideoOutputGeom] DrivingVideoPanel[" + title + "] invalid axis=" + axis
                                 + " w=" + w + " h=" + h + " visibleOut=" + videoOutSide.visible
                                 + " tMs=" + Date.now())
                }
                _videoOutSideHadBadGeom = true
            } else if (_videoOutSideHadBadGeom) {
                _videoOutSideHadBadGeom = false
                console.warn("[Client][UI][Video][VideoOutputGeom] DrivingVideoPanel[" + title + "] ★ recovered axis=" + axis
                             + " w=" + w + " h=" + h + " tMs=" + Date.now())
            }
        }

        function checkSideLayoutConstraints() {
            if (width <= 0 || height <= 0) {
                console.warn("[Client][UI][Video] VideoPanel[" + title + "] 尺寸为0或负数！(DrivingVideoPanel)"
                    + " width=" + width + " height=" + height
                    + " visible=" + visible)
            }
        }

        function rebindSidePanelVideoOutput() {
            if (_prevVideoBindClient && _prevVideoBindClient !== streamClient) {
                _prevVideoBindClient.bindVideoSurface(null)
                _prevVideoBindClient = null
            }
            if (streamClient && videoOutSide) {
                streamClient.bindVideoSurface(videoOutSide)
                _prevVideoBindClient = streamClient
            }
        }

        // streamClient 变化诊断
        onStreamClientChanged: {
            var prevId = _streamClientIdentity
            _streamClientIdentity = streamClient
            if (!streamClient || prevId !== streamClient) {
                everHadVideoFrame = false
                hasVideoFrame = false
                console.warn("[Client][UI][Video][StreamIdentity] VideoPanel[" + title + "] ptr "
                             + (streamClient ? String(streamClient) : "null")
                             + " prevPtr=" + (prevId ? String(prevId) : "null")
                             + " resetEverHad=true")
            }
            rebindSidePanelVideoOutput()
            var info = (streamClient !== null && streamClient !== undefined)
                ? ("ptr=" + streamClient)
                : "null/undefined"
            console.warn("[Client][UI][Video] VideoPanel[" + title + "] onStreamClientChanged streamClient=" + info)
        }

        onWidthChanged: checkSideLayoutConstraints()
        onHeightChanged: checkSideLayoutConstraints()
        
        radius: 6
        color: colorPanel
        border.color: colorBorderActive
        border.width: 1
        
        // 延迟清除 hasVideoFrame
        Timer {
            id: placeholderClearTimer
            interval: placeholderClearDelay
            repeat: false
            onTriggered: { }
        }

        Timer {
            id: sideConnectionsDiagTimer
            interval: 10000
            repeat: true
            running: true
            onTriggered: {
                var _wsm = AppContext.webrtcStreamManager ? AppContext.webrtcStreamManager : null
                if (!_wsm) return
                var sc = drivingVideoPanelRoot.streamClient
                var scInfo = (sc !== null && sc !== undefined)
                    ? ("ptr=" + sc) : "null"
                var myRc = -1
                if (sc) {
                    if (title === "左视图")
                        myRc = _wsm.getLeftSignalReceiverCount ? _wsm.getLeftSignalReceiverCount() : -1
                    else if (title === "右视图")
                        myRc = _wsm.getRightSignalReceiverCount ? _wsm.getRightSignalReceiverCount() : -1
                    else if (title === "后视图")
                        myRc = _wsm.getRearSignalReceiverCount ? _wsm.getRearSignalReceiverCount() : -1
                }
                var pend = (sc && sc.pendingVideoHandlerDepth) ? sc.pendingVideoHandlerDepth() : -1
                var binds = (sc && sc.bindVideoSurfaceInvocationCount) ? sc.bindVideoSurfaceInvocationCount() : -1
                var bindsLife = (sc && sc.bindVideoSurfaceLifetimeInvocationCount) ? sc.bindVideoSurfaceLifetimeInvocationCount() : -1
                var totalP = _wsm.getTotalPendingFrames ? _wsm.getTotalPendingFrames() : -1
                console.log("[Client][UI][Video] PeriodicDiag DrivingVideoPanel[" + title + "]"
                            + " streamClient=" + scInfo + " myRc=" + myRc
                            + " pendingDepth=" + pend
                            + " bindSurfThisConn=" + binds + " bindSurfLifetime=" + bindsLife
                            + " totalPendingAllStreams=" + totalP)
            }
        }
        
        Column {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4
            
            Text {
                id: sidePanelTitleText
                text: title
                color: colorTextPrimary
                font.pixelSize: 12
                font.bold: true
                font.family: chineseFont || font.family
            }
            
            Item {
                width: Math.max(1, parent.width)
                height: Math.max(32, parent.height - sidePanelTitleText.height - parent.spacing)
                clip: true

                Rectangle {
                    anchors.fill: parent
                    z: 0
                    color: colorPanel
                }
                
                RemoteVideoSurface {
                    id: videoOutSide
                    anchors.fill: parent
                    z: 5
                    fillMode: 1
                    panelLabel: drivingVideoPanelRoot.title
                    Component.onCompleted: drivingVideoPanelRoot.rebindSidePanelVideoOutput()
                    onWidthChanged: {
                        drivingVideoPanelRoot.diagSideVideoOutputGeom("w")
                        if (width <= 0 || height <= 0)
                            console.warn("[Client][UI][Video][RemoteSurfaceSize] DrivingVideoPanel[" + drivingVideoPanelRoot.title + "]"
                                         + " w=" + width + " h=" + height)
                    }
                    onHeightChanged: {
                        drivingVideoPanelRoot.diagSideVideoOutputGeom("h")
                        if (width <= 0 || height <= 0)
                            console.warn("[Client][UI][Video][RemoteSurfaceSize] DrivingVideoPanel[" + drivingVideoPanelRoot.title + "]"
                                         + " w=" + width + " h=" + height)
                    }
                    onVisibleChanged: {
                        console.warn("[Client][UI][Video][RemoteSurfaceVisible] DrivingVideoPanel[" + drivingVideoPanelRoot.title + "]"
                                     + " visible=" + visible + " w=" + width + " h=" + height
                                     + " tMs=" + Date.now())
                    }
                }

                Connections {
                    id: sideStreamClientConnections
                    target: streamClient
                    ignoreUnknownSignals: true

                    function onTargetChanged() {
                        var targetObj = sideStreamClientConnections.target !== undefined ? sideStreamClientConnections.target : null
                        var targetInfo = (targetObj !== null && targetObj !== undefined)
                            ? ("ptr=" + targetObj) : "null/undefined"
                        console.warn("[Client][UI][Video] DrivingVideoPanel[" + drivingVideoPanelRoot.title
                                     + "] Connections.target changed target=" + targetInfo)
                    }

                    function onVideoFrameReady(frameWidth, frameHeight, frameId) {
                        var hasValidSize = (frameWidth > 0 && frameHeight > 0)
                        drivingVideoPanelRoot._sideFrameReadyLogSeq = drivingVideoPanelRoot._sideFrameReadyLogSeq + 1
                        var n = drivingVideoPanelRoot._sideFrameReadyLogSeq
                        if (!hasValidSize) {
                            if (n <= 15 || (n % 120 === 0))
                                console.warn("[Client][UI][Video][FrameReady] DrivingVideoPanel[" + drivingVideoPanelRoot.title + "] INVALID"
                                             + " frameId=" + frameId + " fw=" + frameWidth + " fh=" + frameHeight)
                        } else if (n <= 5 || (n % 120 === 0)) {
                            console.log("[Client][UI][Video][FrameReady] DrivingVideoPanel[" + drivingVideoPanelRoot.title + "] ok"
                                        + " n=" + n + " " + frameWidth + "x" + frameHeight + " frameId=" + frameId)
                        }
                        if (hasValidSize) {
                            placeholderClearTimer.stop()
                            hasVideoFrame = true
                            everHadVideoFrame = true
                        } else if (!everHadVideoFrame) {
                            hasVideoFrame = false
                        }
                    }

                    function onConnectionStatusChanged(connected) {
                        if (connected)
                            placeholderClearTimer.stop()
                    }
                }
                
                // 占位图标（不透明底：无帧时与 VideoPanel.qml / DrivingInterface 内联 VideoPanel 一致，避免透出宿主）
                Rectangle {
                    anchors.fill: parent
                    color: colorPanel
                    visible: showPlaceholder && !everHadVideoFrame
                    z: 15
                    onVisibleChanged: {
                        console.warn("[Client][UI][Video][Placeholder] VideoPanel[" + title + "] 📹层 visible=" + visible
                                     + " everHadVideoFrame=" + everHadVideoFrame
                                     + " showPlaceholder=" + showPlaceholder
                                     + " z=15>RemoteVideoSurface.z=5")
                    }
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
    
    // ═══════════════════════════════════════════════════════════════════════════
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
