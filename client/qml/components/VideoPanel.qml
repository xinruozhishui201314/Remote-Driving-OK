import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtMultimedia
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
    property QtObject streamClient: null  // WebRtcClient*
    property bool hasVideoFrame: false
    property int placeholderClearDelay: 2500  // ms
    property string chineseFont: ThemeModule.Theme.chineseFont
    property QtObject _prevVideoBindClient: null

    function rebindPanelVideoOutput() {
        if (_prevVideoBindClient && _prevVideoBindClient !== streamClient) {
            _prevVideoBindClient.bindVideoOutput(null)
            _prevVideoBindClient = null
        }
        if (streamClient && videoOutPanel) {
            streamClient.bindVideoOutput(videoOutPanel)
            _prevVideoBindClient = streamClient
        }
    }

    // ── 诊断：streamClient property 变化追踪 ─────────────────────────
    onStreamClientChanged: {
        rebindPanelVideoOutput()
        var info = (streamClient !== null && streamClient !== undefined)
            ? ("ptr=" + streamClient + " type=" + (streamClient.metaObject ? streamClient.metaObject().className() : "N/A"))
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
            var itemInfo = checkItem.metaObject ? checkItem.metaObject().className : "unknown"
            var itemId = checkItem.id || (checkItem === drivingInterface ? "drivingInterface" : "unknown")
            chain += itemInfo + "(" + itemId + ":visible=" + checkItem.visible + ",size=" + Math.round(checkItem.width || 0) + "x" + Math.round(checkItem.height || 0) + ") → "
            if (!checkItem.visible) {
                console.error("[Client][UI][Video] 发现不可见父项！item=" + itemInfo + "(" + itemId + ")")
            }
            checkItem = checkItem.parent
        }
        console.warn("[Client][UI][Video] VideoPanel[" + title + "] 可见性链: " + chain + "VideoOutput")
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
            console.warn("[Client][UI][Video] VideoOutput[" + title + "]: exists=true")
        } else {
            console.error("[Client][UI][Video] VideoOutput[" + title + "] 不存在！")
        }
    }
    
    // ── 延迟清除定时器 ───────────────────────────────────────────────────
    Timer {
        id: placeholderClearTimer
        interval: placeholderClearDelay
        repeat: false
        onTriggered: {
            hasVideoFrame = false
            console.log("[Client][UI][Video] VideoPanel placeholder 显示（连接断开≥2.5s）")
        }
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
            
            var sc = parent.streamClient
            var scInfo = (sc !== null && sc !== undefined)
                ? ("ptr=" + sc + " type=" + (sc.metaObject ? sc.metaObject().className() : "N/A"))
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
            console.log("[Client][UI][Video] PeriodicDiag VideoPanel[" + title + "]"
                        + " streamClient=" + scInfo
                        + " myRc=" + myRc)
        }
    }
    
    // ── 主内容区域 ───────────────────────────────────────────────────────
    Column {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4
        
        // 标题
        Text {
            text: title
            color: ThemeModule.Theme.colorTextPrimary
            font.pixelSize: 12
            font.bold: true
            font.family: chineseFont || font.family
        }
        
        // 视频区域
        Item {
            width: parent.width
            height: parent.height - 24
            clip: true
            
            VideoOutput {
                id: videoOutPanel
                anchors.fill: parent
                z: 5
                fillMode: VideoOutput.PreserveAspectCrop
                Component.onCompleted: videoPanel.rebindPanelVideoOutput()
            }

            Connections {
                target: streamClient
                ignoreUnknownSignals: true

                function onTargetChanged() {
                    var targetObj = this && this.target !== undefined ? this.target : null
                    var targetInfo = (targetObj !== null && targetObj !== undefined)
                        ? ("ptr=" + targetObj)
                        : "null/undefined"
                    console.warn("[Client][UI][Video] VideoPanel[" + title + "] Connections.target changed: "
                                + " target=" + targetInfo)
                }

                function onVideoFrameReady(frame, frameWidth, frameHeight, frameId) {
                    placeholderClearTimer.stop()
                    var hasValidSize = (frameWidth > 0 && frameHeight > 0)
                    if (hasValidSize)
                        hasVideoFrame = true
                }

                function onConnectionStatusChanged(connected) {
                    console.log("[Client][UI][Video] VideoPanel[" + title + "] onConnectionStatusChanged connected=" + connected)
                    if (connected) {
                        placeholderClearTimer.stop()
                    } else {
                        placeholderClearTimer.restart()
                    }
                }
            }
            
            // ── 占位图标 ────────────────────────────────────────────────
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                visible: showPlaceholder && !hasVideoFrame
                z: 15
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
