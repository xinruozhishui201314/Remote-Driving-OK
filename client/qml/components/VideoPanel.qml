import ".."
import QtQuick 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "../styles" as ThemeModule

/**
 * 视频窗口组件
 * 可绑定 WebRtcStreamManager 的 front/rear/left/right Client
 * 从 DrivingInterface.qml 提取，支持独立维护
 */
Rectangle {
    id: videoPanel
    
    // ── 属性 ────────────────────────────────────────────────────────────
    property string title: ""
    property bool showPlaceholder: true
    property WebRtcClient streamClient: null
    property bool hasVideoFrame: false
    property bool everHadVideoFrame: false
    property int placeholderClearDelay: 2500  // ms
    property string chineseFont: ThemeModule.Theme.chineseFont
    property WebRtcClient _prevVideoBindClient: null
    property WebRtcClient _streamClientIdentity: null
    property int _frameReadyLogSeq: 0
    /** 用于检测 VideoOutput 从无效尺寸恢复到正数（与 Scene Graph 是否绘制对齐） */
    property bool _videoOutPanelHadBadGeom: false
    /** 检测 everHadVideoFrame 被清空 → 占位层 z15 重新盖住 VideoOutput z5（硬件 GL 仍闪时搜此关键字） */
    property bool _prevEverHadVideoFrame: false

    onEverHadVideoFrameChanged: {
        if (_prevEverHadVideoFrame && !everHadVideoFrame) {
            console.warn("[Client][UI][Video][LayerFlickerRisk] VideoPanel[" + title + "] everHadVideoFrame true→false"
                         + " ★ 占位 Rectangle(z15) 将盖住视频；常见根因: streamClient 指针变化/重绑、或逻辑清空"
                         + " streamClient=" + (streamClient ? String(streamClient) : "null")
                         + " tMs=" + Date.now())
            AppContext.reportVideoFlickerQmlLayerEvent("VideoPanel:" + title,
                "everHad_true_to_false streamClient=" + (streamClient ? String(streamClient) : "null"))
        }
        _prevEverHadVideoFrame = everHadVideoFrame
    }

    function diagVideoOutputGeom(axis) {
        if (typeof videoOutPanel === "undefined" || !videoOutPanel)
            return
        var w = videoOutPanel.width
        var h = videoOutPanel.height
        var bad = (w <= 0 || h <= 0)
        if (bad) {
            if (!_videoOutPanelHadBadGeom) {
                console.warn("[Client][UI][Video][VideoOutputGeom] VideoPanel[" + title + "] invalid size axis=" + axis
                             + " w=" + w + " h=" + h + " visibleOut=" + videoOutPanel.visible
                             + " tMs=" + Date.now() + " ★ 与 C++ [VideoSink] post-setVideoFrame 对照")
            }
            _videoOutPanelHadBadGeom = true
        } else if (_videoOutPanelHadBadGeom) {
            _videoOutPanelHadBadGeom = false
            console.warn("[Client][UI][Video][VideoOutputGeom] VideoPanel[" + title + "] ★ recovered valid size axis=" + axis
                         + " w=" + w + " h=" + h + " visibleOut=" + videoOutPanel.visible
                         + " tMs=" + Date.now() + " ★ 0→正 可与解码出画时间对齐")
        }
    }

    function rebindPanelVideoOutput() {
        if (_prevVideoBindClient && _prevVideoBindClient !== streamClient) {
            _prevVideoBindClient.bindVideoSurface(null)
            _prevVideoBindClient = null
        }
        if (streamClient && videoOutPanel) {
            streamClient.bindVideoSurface(videoOutPanel)
            _prevVideoBindClient = streamClient
        }
    }

    // ── 诊断：streamClient property 变化追踪 ─────────────────────────
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
        rebindPanelVideoOutput()
        var info = (streamClient !== null && streamClient !== undefined)
            ? ("ptr=" + streamClient)
            : "null/undefined"
        console.warn("[Client][UI][Video] VideoPanel[" + title + "] onStreamClientChanged"
                    + " streamClient=" + info
                    + " streamClient 就绪")
        
        var _wsm_diag = AppContext.webrtcStreamManager
        if (_wsm_diag && _wsm_diag.getStreamSignalMetaInfo) {
            var meta = _wsm_diag.getStreamSignalMetaInfo()
            console.warn("[Client][UI][Video] VideoPanel[" + title + "] streamClientChanged 时的信号元数据:\n" + meta)
        }
        
        if (_wsm_diag) {
            var rcF = _wsm_diag.getFrontSignalReceiverCount ? _wsm_diag.getFrontSignalReceiverCount() : -1
            var rcR = _wsm_diag.getRearSignalReceiverCount ? _wsm_diag.getRearSignalReceiverCount() : -1
            var rcL = _wsm_diag.getLeftSignalReceiverCount ? _wsm_diag.getLeftSignalReceiverCount() : -1
            var rcRi = _wsm_diag.getRightSignalReceiverCount ? _wsm_diag.getRightSignalReceiverCount() : -1
            console.warn("[Client][UI][Video] VideoPanel[" + title + "] 各路接收者: front=" + rcF + " rear=" + rcR + " left=" + rcL + " right=" + rcRi)
        }
    }
    
    // ── 样式 ────────────────────────────────────────────────────────────
    radius: 6
    color: ThemeModule.Theme.colorPanel
    border.color: ThemeModule.Theme.colorBorderActive
    border.width: 1
    
    // ── 可见性诊断 ─────────────────────────────────────────────────────
    onVisibleChanged: {
        console.warn("[Client][UI][Video] VideoPanel[" + title + "] onVisibleChanged"
            + " visible=" + visible
            + " width=" + width + " height=" + height
            + " parent.visible=" + (parent ? parent.visible : "N/A")
            + " drivingInterface.visible=" + (typeof drivingInterface !== "undefined" && drivingInterface ? drivingInterface.visible : "N/A"))
        
        var checkItem = parent
        var chain = title + " → "
        while (checkItem) {
            var itemId = checkItem.id || (checkItem === drivingInterface ? "drivingInterface" : "unknown")
            chain += "(" + itemId + ":visible=" + checkItem.visible + ",size=" + Math.round(checkItem.width || 0) + "x" + Math.round(checkItem.height || 0) + ") → "
            if (!checkItem.visible) {
                console.error("[Client][UI][Video] 发现不可见父项！item=" + itemId)
            }
            checkItem = checkItem.parent
        }
        console.warn("[Client][UI][Video] VideoPanel[" + title + "] 可见性链: " + chain + "RemoteVideoSurface")
    }
    
    // ── 布局约束诊断 ─────────────────────────────────────────────────────
    onWidthChanged: checkLayoutConstraints()
    onHeightChanged: checkLayoutConstraints()
    function checkLayoutConstraints() {
        if (width <= 0 || height <= 0) {
            console.warn("[Client][UI][Video] VideoPanel[" + title + "] 尺寸为0或负数！"
                + " width=" + width + " height=" + height
                + " Layout.minimumWidth=" + (Layout ? Layout.minimumWidth : "N/A"))
        }
    }
    
    onYChanged: if (y >= 0) {
        console.log("[Client][UI][Layout] VideoPanel[" + title + "] position changed"
            + " x=" + x + " y=" + y + " width=" + width + " height=" + height)
    }
    
    // ── 组件创建诊断 ─────────────────────────────────────────────────────
    Component.onCompleted: {
        console.warn("[Client][UI][Video] VideoPanel[" + title + "] onCompleted")
        console.warn("[Client][UI][Video] 初始状态: visible=" + visible + " width=" + width + " height=" + height)
        console.warn("[Client][UI][Video] streamClient=" + (streamClient ? ("ptr=" + streamClient) : "null"))
        
        if (typeof videoOutPanel !== "undefined" && videoOutPanel) {
            console.warn("[Client][UI][Video] RemoteVideoSurface[" + title + "]: exists=true")
        } else {
            console.error("[Client][UI][Video] RemoteVideoSurface[" + title + "] 不存在！")
        }
    }
    
    // ── 延迟清除定时器 ───────────────────────────────────────────────────
    Timer {
        id: placeholderClearTimer
        interval: placeholderClearDelay
        repeat: false
        onTriggered: { }
    }
    
    // ── 诊断定时器 ───────────────────────────────────────────────────────
    Timer {
        id: connectionsDiagTimer
        interval: 10000
        repeat: true
        running: true
        onTriggered: {
            var _wsm = AppContext.webrtcStreamManager ? AppContext.webrtcStreamManager : null
            if (!_wsm) return
            
            var sc = videoPanel.streamClient
            var scInfo = (sc !== null && sc !== undefined)
                ? ("ptr=" + sc)
                : "null/undefined"
            
            var myRc = -1
            if (sc) {
                if (title === "左视图") {
                    myRc = _wsm.getLeftSignalReceiverCount ? _wsm.getLeftSignalReceiverCount() : -1
                } else if (title === "右视图") {
                    myRc = _wsm.getRightSignalReceiverCount ? _wsm.getRightSignalReceiverCount() : -1
                } else if (title === "后视图") {
                    myRc = _wsm.getRearSignalReceiverCount ? _wsm.getRearSignalReceiverCount() : -1
                }
            }
            var pend = (sc && sc.pendingVideoHandlerDepth) ? sc.pendingVideoHandlerDepth() : -1
            var binds = (sc && sc.bindVideoSurfaceInvocationCount) ? sc.bindVideoSurfaceInvocationCount() : -1
            var bindsLife = (sc && sc.bindVideoSurfaceLifetimeInvocationCount) ? sc.bindVideoSurfaceLifetimeInvocationCount() : -1
            var totalP = _wsm.getTotalPendingFrames ? _wsm.getTotalPendingFrames() : -1
            console.log("[Client][UI][Video] PeriodicDiag VideoPanel[" + title + "]"
                        + " streamClient=" + scInfo
                        + " myRc=" + myRc
                        + " pendingDepth=" + pend
                        + " bindSurfThisConn=" + binds + " bindSurfLifetime=" + bindsLife
                        + " totalPendingAllStreams=" + totalP)
        }
    }
    
    // ── 主内容区域 ───────────────────────────────────────────────────────
    Column {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4
        
        // 标题
        Text {
            id: videoPanelTitleText
            text: title
            color: ThemeModule.Theme.colorTextPrimary
            font.pixelSize: 12
            font.bold: true
            font.family: chineseFont || font.family
        }
        
        // 视频区域
        Item {
            width: Math.max(1, parent.width)
            height: Math.max(32, parent.height - videoPanelTitleText.height - parent.spacing)
            clip: true

            // 最底层永不透明衬底：RemoteVideoSurface 在 updatePaintNode 无纹理时可 return nullptr，
            // 与 Docker+X11+软光栅组合时若仅靠上层占位易误判「透出宿主页」；双衬底保证本格始终遮挡背后像素。
            Rectangle {
                anchors.fill: parent
                z: -200
                color: ThemeModule.Theme.colorPanel
            }

            Rectangle {
                anchors.fill: parent
                z: 0
                color: ThemeModule.Theme.colorPanel
            }
            
            RemoteVideoSurface {
                id: videoOutPanel
                anchors.fill: parent
                z: 5
                fillMode: 1
                panelLabel: videoPanel.title
                Component.onCompleted: videoPanel.rebindPanelVideoOutput()
                onWidthChanged: {
                    videoPanel.diagVideoOutputGeom("w")
                    if (width <= 0 || height <= 0)
                        console.warn("[Client][UI][Video][RemoteSurfaceSize] VideoPanel[" + videoPanel.title + "]"
                                     + " w=" + width + " h=" + height
                                     + " ★ 尺寸为0时 Scene Graph 可能跳过绘制")
                }
                onHeightChanged: {
                    videoPanel.diagVideoOutputGeom("h")
                    if (width <= 0 || height <= 0)
                        console.warn("[Client][UI][Video][RemoteSurfaceSize] VideoPanel[" + videoPanel.title + "]"
                                     + " w=" + width + " h=" + height)
                }
                onVisibleChanged: {
                    console.warn("[Client][UI][Video][RemoteSurfaceVisible] VideoPanel[" + videoPanel.title + "]"
                                 + " visible=" + visible + " w=" + width + " h=" + height
                                 + " tMs=" + Date.now())
                }
            }

            Connections {
                id: streamClientConnections
                target: streamClient
                ignoreUnknownSignals: true

                function onTargetChanged() {
                    var targetObj = streamClientConnections.target !== undefined ? streamClientConnections.target : null
                    var targetInfo = (targetObj !== null && targetObj !== undefined)
                        ? ("ptr=" + targetObj)
                        : "null/undefined"
                    console.warn("[Client][UI][Video] VideoPanel[" + title + "] Connections.target changed: "
                                + " target=" + targetInfo)
                }

                function onVideoFrameReady(frameWidth, frameHeight, frameId) {
                    placeholderClearTimer.stop()
                    var hasValidSize = (frameWidth > 0 && frameHeight > 0)
                    videoPanel._frameReadyLogSeq = videoPanel._frameReadyLogSeq + 1
                    var n = videoPanel._frameReadyLogSeq
                    if (!hasValidSize) {
                        if (n <= 15 || (n % 120 === 0))
                            console.warn("[Client][UI][Video][FrameReady] VideoPanel[" + videoPanel.title + "] INVALID"
                                         + " frameId=" + frameId + " fw=" + frameWidth + " fh=" + frameHeight
                                         + " everHad=" + videoPanel.everHadVideoFrame
                                         + " ★ 占位层可能永不消失")
                    } else if (n <= 5 || (n % 120 === 0)) {
                        console.log("[Client][UI][Video][FrameReady] VideoPanel[" + videoPanel.title + "] ok"
                                    + " n=" + n + " " + frameWidth + "x" + frameHeight + " frameId=" + frameId)
                    }
                    if (hasValidSize) {
                        hasVideoFrame = true
                        everHadVideoFrame = true
                    } else if (!everHadVideoFrame) {
                        hasVideoFrame = false
                    }
                }

                function onConnectionStatusChanged(connected) {
                    console.log("[Client][UI][Video] VideoPanel[" + title + "] onConnectionStatusChanged connected=" + connected)
                    if (connected)
                        placeholderClearTimer.stop()
                }
            }
            
            // ── 占位图标 ────────────────────────────────────────────────
            Rectangle {
                anchors.fill: parent
                // 不透明底色：无解码帧时 RemoteVideoSurface 不挂纹理节点，若此处透明会透出窗口背后（误认「宿主网页」）
                color: ThemeModule.Theme.colorPanel
                visible: showPlaceholder && !everHadVideoFrame
                z: 15
                onVisibleChanged: {
                    console.warn("[Client][UI][Video][Placeholder] VideoPanel[" + title + "] 📹层 visible=" + visible
                                 + " everHadVideoFrame=" + everHadVideoFrame
                                 + " showPlaceholder=" + showPlaceholder
                                 + " z=15>RemoteVideoSurface.z=5 ★ true=遮挡视频")
                }
                Text {
                    anchors.centerIn: parent
                    text: "📹"
                    font.pixelSize: 28
                    color: ThemeModule.Theme.colorBorderActive
                }
            }
            
            // ── 连接状态文字 ────────────────────────────────────────────
            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 4
                z: 16
                text: streamClient ? (streamClient.isConnected ? "视频已连接" : (streamClient.statusText || "连接中...")) : "等待连接"
                color: streamClient && streamClient.isConnected ? ThemeModule.Theme.colorAccent : ThemeModule.Theme.colorTextSecondary
                font.pixelSize: 11
                font.family: chineseFont || font.family
            }
        }
    }
}
