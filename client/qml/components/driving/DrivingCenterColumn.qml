import "../.."
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
// 仅用于 C++ 注册类型（WebRtcClient）静态类型；禁止 RemoteDriving 模块内单例（AppContext）直连（§3.6 / verify-client-ui-module-contract.sh）
import RemoteDriving 1.0
import ".." as Components

/**
 * 中列：主视频 + 控制区 + 仪表盘。
 * 状态与指令经 facade.teleop；布局/主题经 facade.*；数据服务经 facade.appServices（与 internal 同源窄面，见 CLIENT_UI_MODULE_CONTRACT §3.6）。
 */
ColumnLayout {
    id: centerColLayout
    required property Item facade
    readonly property alias centerCameraRect: centerCameraRect
    readonly property alias centerControlsRect: centerControlsRect
    readonly property alias centerDashboardRect: centerDashboardRect
    readonly property alias mainCameraView: mainCameraView
    Layout.preferredWidth: facade.centerColAllocW
    Layout.minimumWidth: 380
    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.maximumHeight: facade.mainRowAvailH
    spacing: 4
    
    // 组件1（同级）：主视图
    Rectangle {
        id: centerCameraRect
        Layout.fillWidth: true
        // 使用 facade.mainRowAvailH 而非直接乘以 facade.height，保证总和受垂直预算约束
        Layout.preferredHeight: facade.mainRowAvailH * facade.centerCameraRatio
        radius: 6
        color: facade.colorPanel
        border.color: facade.colorBorderActive
        border.width: 1
        
        Column {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4
            
            Text {
                id: mainCameraSectionTitle
                text: "主视图"
                color: facade.colorTextPrimary
                font.pixelSize: 12
                font.bold: true
                font.family: facade.chineseFont || font.family
            }
            
            Rectangle {
                id: mainCameraView
                // 与内联 VideoPanel 一致：禁止 ≤0 宽度；负宽（布局瞬态）会导致 frontVideoOut 负几何 → RHI 洞穿透出宿主桌面
                width: Math.max(1, parent.width)
                height: Math.max(48, parent.height - mainCameraSectionTitle.height - parent.spacing)
                radius: 4
                color: facade.colorBackground
                clip: true

                // ★★★ 新增：mainCameraView 初始化时诊断信号连接 ★★★
                // 对比 leftFrontPanel/leftRearPanel/rightViewVideo 的 Component.onCompleted
                Component.onCompleted: {
                    console.log("[Client][UI][Video] ★★★ mainCameraView onCompleted ★★★")
                    console.log("[Client][UI][Video] mainCameraView streamClient=" + streamClient
                                + " frontClient=" + (facade.appServices.webrtcStreamManager && facade.appServices.webrtcStreamManager.frontClient ? "ok" : "null"))
                    var _wsm = facade.appServices.webrtcStreamManager
                    if (_wsm && _wsm.getQmlSignalReceiverCount) {
                        var rc = _wsm.getQmlSignalReceiverCount()
                        console.log("[Client][UI][Video] ★★★ mainCameraView onCompleted: getQmlSignalReceiverCount()=" + rc + " ★★★")
                        if (rc === 0) {
                            console.error("[Client][UI][Video] ★★★ FATAL: mainCameraView rc=0！信号无法到达！检查 Connections.target 绑定 ★★★")
                        }
                    }
                    if (_wsm && _wsm.getStreamDebugInfo) {
                        console.log("[Client][UI][Video] mainCameraView C++ StreamManager: " + _wsm.getStreamDebugInfo())
                    }
                    // ★★★ 诊断：打印完整信号元数据（用于确认 QML 连接的是哪个 signal 重载）★★★
                    if (_wsm && _wsm.getStreamSignalMetaInfo) {
                        console.log("[Client][UI][Video] mainCameraView onCompleted 信号元数据:\n" + _wsm.getStreamSignalMetaInfo())
                    }
                }

                // 与 VideoPanel 组件保持一致：通过 property 间接绑定 streamClient，
                // 使 Connections.target 在 webrtcStreamManager.frontClient 就绪时可响应式更新。
                // 之前直接写 Connections.target: webrtcStreamManager.frontClient 存在绑定时机问题，
                // 导致 onVideoFrameReady 信号从未被触发（虽然 onConnectionStatusChanged 正常），
                // 根因：QML Connections.target 表达式求值时序与信号注册冲突。
                property WebRtcClient streamClient: facade.appServices.webrtcStreamManager ? facade.appServices.webrtcStreamManager.frontClient : null

                // 视频帧：everHadVideoFrame 锁存首帧后，断网/抖动不再把 Canvas/文案盖回视频上（防闪烁）
                property bool hasVideoFrame: false
                property bool everHadVideoFrame: false
                property WebRtcClient _prevVideoBindClient: null
                property WebRtcClient _mainCamStreamIdentity: null
                property int _frontFrameReadyLogSeq: 0
                property bool _frontVideoOutHadBadGeom: false
                property bool _prevEverHadMainCamera: false

                onEverHadVideoFrameChanged: {
                    if (_prevEverHadMainCamera && !everHadVideoFrame) {
                        console.warn("[Client][UI][Video][LayerFlickerRisk] 主视图(mainCameraView) everHadVideoFrame true→false"
                                     + " ★ Canvas/文案层 z18–20 将盖住 frontVideoOut z10；常与 streamClient 身份变化有关"
                                     + " streamClient=" + (streamClient ? String(streamClient) : "null")
                                     + " tMs=" + Date.now())
                        facade.appServices.reportVideoFlickerQmlLayerEvent("DrivingInterface:mainCameraView",
                            "everHad_true_to_false streamClient=" + (streamClient ? String(streamClient) : "null"))
                    }
                    _prevEverHadMainCamera = everHadVideoFrame
                }

                function diagFrontVideoOutputGeom(axis) {
                    if (typeof frontVideoOut === "undefined" || !frontVideoOut)
                        return
                    var w = frontVideoOut.width
                    var h = frontVideoOut.height
                    var bad = (w <= 0 || h <= 0)
                    if (bad) {
                        if (!_frontVideoOutHadBadGeom) {
                            console.warn("[Client][UI][Video][VideoOutputGeom] 主视图 frontVideoOut invalid axis=" + axis
                                         + " w=" + w + " h=" + h + " visibleOut=" + frontVideoOut.visible
                                         + " tMs=" + Date.now())
                        }
                        _frontVideoOutHadBadGeom = true
                    } else if (_frontVideoOutHadBadGeom) {
                        _frontVideoOutHadBadGeom = false
                        console.warn("[Client][UI][Video][VideoOutputGeom] 主视图 ★ frontVideoOut recovered axis=" + axis
                                     + " w=" + w + " h=" + h + " tMs=" + Date.now())
                    }
                }

                function checkMainCameraLayoutConstraints() {
                    if (width <= 0 || height <= 0) {
                        console.warn("[Client][UI][Video] 主视图(mainCameraView) 尺寸为0或负数"
                            + " width=" + width + " height=" + height + " visible=" + visible)
                    }
                }

                function rebindMainCameraVideoSink() {
                    if (_prevVideoBindClient && _prevVideoBindClient !== streamClient) {
                        _prevVideoBindClient.bindVideoSurface(null)
                        _prevVideoBindClient = null
                    }
                    if (streamClient && frontVideoOut) {
                        streamClient.bindVideoSurface(frontVideoOut)
                        _prevVideoBindClient = streamClient
                    }
                }

                onStreamClientChanged: {
                    var prevId = _mainCamStreamIdentity
                    _mainCamStreamIdentity = streamClient
                    if (!streamClient || prevId !== streamClient) {
                        everHadVideoFrame = false
                        hasVideoFrame = false
                        console.warn("[Client][UI][Video][StreamIdentity] 主视图 streamClient ptr "
                                     + (streamClient ? String(streamClient) : "null")
                                     + " prevPtr=" + (prevId ? String(prevId) : "null")
                                     + " resetEverHad=true（Canvas 引导层 z=18 可能重新盖住 VideoOutput z=10）")
                    }
                    rebindMainCameraVideoSink()
                    checkMainCameraLayoutConstraints()
                    // 增强诊断：记录 target 变化时的详细信息
                    // 注意：QML Connections.target 通过 property 间接绑定，
                    // 若 target=null 而 console.log 显示 webrtcStreamManager.frontClient 正常
                    // → 说明是 Connections 初始化时序问题（target=null 时已创建，信号连接未建立）

                    // 仅在调试模式且限制频率下执行诊断
                    if (!facade.teleop.isDebugMode) return;
                    var now = Date.now();
                    if (now - facade.teleop.lastDiagTime < 1000) return;
                    facade.teleop.lastDiagTime = now;

                    var scInfo = (streamClient !== null && streamClient !== undefined)
                        ? ("ptr=" + streamClient)
                        : "null/undefined"
                    // Connections.target 隐式属性仅在 Connections 子体内可访问；
                    // 在 onStreamClientChanged 中用 streamClient 替代。
                    var targetInfo = (streamClient !== null && streamClient !== undefined)
                        ? ("ptr=" + streamClient)
                        : "null/undefined"
                    console.warn("[Client][UI][Video] 主视图 streamClient changed (QML property): "
                                + scInfo
                                + " target(=streamClient)=" + targetInfo
                                + " ★ 若 target=null 但 streamClient 非null → Connections.target 绑定失效！")
                    // ★★★ 诊断：打印完整信号元数据 ★★★
                    var _wsm_sc = facade.appServices.webrtcStreamManager
                    if (_wsm_sc && _wsm_sc.getStreamSignalMetaInfo) {
                        console.warn("[Client][UI][Video] 主视图 streamClientChanged 时的信号元数据:\n" + _wsm_sc.getStreamSignalMetaInfo())
                    }
                    if (_wsm_sc) {
                        var rc = _wsm_sc.getFrontSignalReceiverCount ? _wsm_sc.getFrontSignalReceiverCount() : -1
                        console.warn("[Client][UI][Video] 主视图 frontSignalReceiverCount=" + rc
                                    + " ★ rc=0 → QML Connections 未能连接到 videoFrameReady 信号！")
                    }
                }

                onWidthChanged: checkMainCameraLayoutConstraints()
                onHeightChanged: checkMainCameraLayoutConstraints()
                
                Timer {
                    id: frontFrameClearTimer
                    interval: 2500
                    repeat: false
                    onTriggered: { }
                }

                // ★★★ 诊断 Timer：每 10s 检查一次 mainCameraView 的 Connections.target 和信号接收者状态 ★★★
                Timer {
                    id: frontConnectionsDiagTimer
                    interval: 10000
                    repeat: true
                    running: true
                    onTriggered: {
                        var _wsm = facade.appServices.webrtcStreamManager ? facade.appServices.webrtcStreamManager : null
                        if (!_wsm) return
                        // 在 Timer.onTriggered 中使用 parent.xxx 显式引用父对象属性，
                        // 避免 JavaScript 引擎对 bare identifier 的作用域解析问题。
                        var sc = mainCameraView.streamClient
                        var scInfo = (sc !== null && sc !== undefined)
                            ? ("ptr=" + sc)
                            : "null/undefined"
                        var targetInfo = scInfo  // target 与 streamClient 同值
                        var rc = _wsm.getFrontSignalReceiverCount ? _wsm.getFrontSignalReceiverCount() : -1
                        var pend = (sc && sc.pendingVideoHandlerDepth) ? sc.pendingVideoHandlerDepth() : -1
                        var binds = (sc && sc.bindVideoSurfaceInvocationCount) ? sc.bindVideoSurfaceInvocationCount() : -1
                        var bindsLife = (sc && sc.bindVideoSurfaceLifetimeInvocationCount) ? sc.bindVideoSurfaceLifetimeInvocationCount() : -1
                        var totalP = _wsm.getTotalPendingFrames ? _wsm.getTotalPendingFrames() : -1
                        var hintF = ""
                        if (!sc)
                            hintF = " ★ streamClient=null"
                        else if (rc < 0)
                            hintF = " ★ frontRc=-1：QML 未识别 getFrontSignalReceiverCount（需 public Q_INVOKABLE）"
                        else if (rc === 0)
                            hintF = " ★ frontRc=0 → videoFrameReady 无接收者"
                        else
                            hintF = " ★ frontRc>0 → 有接收者；无画面查 ZLM/onTrack/解码"
                        console.log("[Client][UI][Video] ★ PeriodicDiag ★ mainCameraView"
                                    + " streamClient=" + scInfo
                                    + " target(=streamClient)=" + targetInfo
                                    + " frontRc=" + rc + hintF
                                    + " pendingDepth=" + pend
                                    + " bindSurfThisConn=" + binds + " bindSurfLifetime=" + bindsLife
                                    + " totalPendingAllStreams=" + totalP)
                    }
                }

                // 与侧向 VideoPanel 一致：无帧时 frontVideoOut 无纹理节点，显式垫片防止 RHI 合成洞穿宿主桌面
                Rectangle {
                    anchors.fill: parent
                    z: 0
                    color: facade.colorBackground
                }
                
                RemoteVideoSurface {
                    id: frontVideoOut
                    anchors.fill: parent
                    z: 10
                    fillMode: RemoteVideoSurface.PreserveAspectCrop
                    panelLabel: "主视图"
                    Component.onCompleted: mainCameraView.rebindMainCameraVideoSink()
                    onWidthChanged: {
                        mainCameraView.diagFrontVideoOutputGeom("w")
                        if (width <= 0 || height <= 0)
                            console.warn("[Client][UI][Video][RemoteSurfaceSize] 主视图 frontVideoOut"
                                         + " w=" + width + " h=" + height)
                    }
                    onHeightChanged: {
                        mainCameraView.diagFrontVideoOutputGeom("h")
                        if (width <= 0 || height <= 0)
                            console.warn("[Client][UI][Video][RemoteSurfaceSize] 主视图 frontVideoOut"
                                         + " w=" + width + " h=" + height)
                    }
                    onVisibleChanged: {
                        console.warn("[Client][UI][Video][RemoteSurfaceVisible] 主视图 frontVideoOut visible="
                                     + visible + " w=" + width + " h=" + height + " tMs=" + Date.now())
                    }
                }

                Connections {
                    id: mainFrontStreamConnections
                    target: mainCameraView.streamClient
                    ignoreUnknownSignals: true

                    function onTargetChanged() {
                        var targetObj = mainFrontStreamConnections.target !== undefined ? mainFrontStreamConnections.target : null
                        var targetInfo = (targetObj !== null && targetObj !== undefined)
                            ? ("ptr=" + targetObj)
                            : "null/undefined"
                        console.warn("[Client][UI][Video] mainCameraView Connections.target changed: " + " target=" + targetInfo)
                    }

                    function onVideoFrameReady(frameWidth, frameHeight, frameId) {
                        frontFrameClearTimer.stop()
                        var hasValidSize = (frameWidth > 0 && frameHeight > 0)
                        mainCameraView._frontFrameReadyLogSeq = mainCameraView._frontFrameReadyLogSeq + 1
                        var n = mainCameraView._frontFrameReadyLogSeq
                        if (!hasValidSize) {
                            if (n <= 15 || (n % 120 === 0))
                                console.warn("[Client][UI][Video][FrameReady] 主视图 INVALID"
                                             + " frameId=" + frameId + " fw=" + frameWidth + " fh=" + frameHeight)
                        } else if (n <= 5 || (n % 120 === 0)) {
                            console.log("[Client][UI][Video][FrameReady] 主视图 ok"
                                        + " n=" + n + " " + frameWidth + "x" + frameHeight + " frameId=" + frameId)
                        }
                        if (hasValidSize) {
                            mainCameraView.hasVideoFrame = true
                            mainCameraView.everHadVideoFrame = true
                        } else if (!mainCameraView.everHadVideoFrame) {
                            mainCameraView.hasVideoFrame = false
                        }
                        if (_mainCameraHandlerLogCount < 10) {
                            var capturedFrameWidth = frameWidth
                            var capturedFrameHeight = frameHeight
                            var capturedFrameId = frameId
                            var capturedHasVideo = mainCameraView.hasVideoFrame
                            Qt.callLater(function() {
                                console.log("[Client][UI][Video] 主视图 handlerDone frame="
                                            + (capturedFrameWidth > 0 && capturedFrameHeight > 0 ? (capturedFrameWidth + "x" + capturedFrameHeight) : "invalid")
                                            + " hasVideo=" + capturedHasVideo
                                            + " frameId=" + capturedFrameId)
                            })
                        }
                        _mainCameraHandlerLogCount++
                    }
                    function onConnectionStatusChanged(connected) {
                        console.log("[Client][UI][Video] 主视图 onConnectionStatusChanged connected=" + connected)
                        if (connected)
                            frontFrameClearTimer.stop()
                    }
                }

                // 前进/倒车视图绘制（无视频时显示）
                Canvas {
                    anchors.fill: parent
                    z: 18
                    visible: facade.teleop.forwardMode && !mainCameraView.everHadVideoFrame
                    onVisibleChanged: {
                        console.warn("[Client][UI][Video][Overlay] 主视图前进Canvas visible=" + visible
                                     + " facade.teleop.forwardMode=" + facade.teleop.forwardMode
                                     + " everHadVideoFrame=" + mainCameraView.everHadVideoFrame
                                     + " ★ true=引导层盖住视频")
                    }
                    onPaint: {
                        var ctx = getContext("2d")
                        // 勿用 clearRect；fillStyle 用主题色字符串（与 parent.color 的 QML Color 类型在部分版本下不兼容）
                        ctx.fillStyle = facade.colorBackground
                        ctx.fillRect(0, 0, width, height)
                        
                        // 引导线
                        ctx.strokeStyle = facade.colorAccent
                        ctx.lineWidth = 3
                        ctx.beginPath()
                        ctx.moveTo(width * 0.25, height * 0.65)
                        ctx.lineTo(width * 0.5, height * 0.5)
                        ctx.lineTo(width * 0.75, height * 0.65)
                        ctx.stroke()
                        
                        // 检测框示例
                        ctx.strokeStyle = "#FF6B6B"
                        ctx.lineWidth = 2
                        ctx.strokeRect(width * 0.12, height * 0.38, 50, 65)
                        ctx.fillStyle = "#FF6B6B"
                        ctx.font = "11px sans-serif"
                        ctx.fillText("行人", width * 0.12, height * 0.36)
                    }
                }
                
                Canvas {
                    anchors.fill: parent
                    z: 18
                    visible: !facade.teleop.forwardMode && !mainCameraView.everHadVideoFrame
                    onVisibleChanged: {
                        console.warn("[Client][UI][Video][Overlay] 主视图倒车Canvas visible=" + visible
                                     + " facade.teleop.forwardMode=" + facade.teleop.forwardMode
                                     + " everHadVideoFrame=" + mainCameraView.everHadVideoFrame)
                    }
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.fillStyle = facade.colorBackground
                        ctx.fillRect(0, 0, width, height)
                        ctx.strokeStyle = "#FF0000"
                        ctx.lineWidth = 2
                        var gridSize = 40
                        for (var x = 0; x < width; x += gridSize) {
                            ctx.beginPath()
                            ctx.moveTo(x, 0)
                            ctx.lineTo(x, height)
                            ctx.stroke()
                        }
                        for (var y = 0; y < height; y += gridSize) {
                            ctx.beginPath()
                            ctx.moveTo(0, y)
                            ctx.lineTo(width, y)
                            ctx.stroke()
                        }
                    }
                }
                
                // 占位文字（无视频时显示）
                Text {
                    anchors.centerIn: parent
                    z: 19
                    visible: !mainCameraView.everHadVideoFrame
                    text: facade.teleop.forwardMode ? "主视图" : "后视辅助"
                    font.pixelSize: 22
                    color: facade.colorBorderActive
                }
                // 状态文字（显示在视频上方，半透明背景）
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    width: statusText.implicitWidth + 16
                    height: statusText.implicitHeight + 8
                    radius: 4
                    color: "#80000000"  // 半透明黑色背景
                    z: 20  // 确保在视频上方
                    
                    Text {
                        id: statusText
                        anchors.centerIn: parent
                        property var _frontClient: facade.appServices.webrtcStreamManager ? facade.appServices.webrtcStreamManager.frontClient : null
                        property bool _isConnected: _frontClient ? _frontClient.isConnected : false
                        text: _frontClient ?
                              (_isConnected ?
                               (mainCameraView.everHadVideoFrame ? "视频已连接" : "等待视频流...") :
                               _frontClient.statusText) :
                              "等待连接"
                        color: (_frontClient && _isConnected && mainCameraView.everHadVideoFrame) ?
                               facade.colorAccent : facade.colorTextSecondary
                        font.pixelSize: 12
                        font.family: facade.chineseFont || font.family
                    }
                }

                // ── 端到端延时显示 ──
                Text {
                    id: latencyDisplayText
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 36 // 在状态文字上方一点
                    z: 21
                    text: (facade.appServices.networkQuality ? Math.round(facade.appServices.networkQuality.latencyMs) : 0) + " ms"
                    color: facade.colorDanger
                    font.pixelSize: 14
                    font.bold: true
                    font.family: facade.chineseFont || font.family
                    visible: mainCameraView.everHadVideoFrame
                }
                
                // 悬浮转向灯
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 20
                    spacing: 20
                    
                    // 左转灯按钮
                    Rectangle {
                        width: 120; height: 44; radius: 8
                        opacity: facade.teleop.leftTurnActive ? 1 : 0.45
                        color: facade.teleop.leftTurnActive ? "#1A3A1A" : "#0A0A12"
                        border.width: 2
                        border.color: facade.teleop.leftTurnActive ? facade.colorAccent : facade.colorBorder
                        
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            Text {
                                text: "◀"
                                color: facade.teleop.leftTurnActive ? facade.colorAccent : "#606080"
                                font.pixelSize: 18
                            }
                            Text {
                                text: "左转灯"
                                color: facade.teleop.leftTurnActive ? facade.colorTextPrimary : "#808090"
                                font.pixelSize: 13
                                font.family: facade.chineseFont || font.family
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                facade.teleop.leftTurnActive = !facade.teleop.leftTurnActive
                                if (facade.teleop.rightTurnActive) facade.teleop.rightTurnActive = false
                                facade.teleop.lightCommandSent("leftTurn", facade.teleop.leftTurnActive)
                            }
                        }
                        
                        // 闪烁动画
                        SequentialAnimation on opacity {
                            running: facade.teleop.leftTurnActive
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.5; duration: 500 }
                            NumberAnimation { to: 1.0; duration: 500 }
                        }
                    }
                    
                    // 右转灯按钮
                    Rectangle {
                        width: 120; height: 44; radius: 8
                        opacity: facade.teleop.rightTurnActive ? 1 : 0.45
                        color: facade.teleop.rightTurnActive ? "#1A3A1A" : "#0A0A12"
                        border.width: 2
                        border.color: facade.teleop.rightTurnActive ? facade.colorAccent : facade.colorBorder
                        
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            Text {
                                text: "右转灯"
                                color: facade.teleop.rightTurnActive ? facade.colorTextPrimary : "#808090"
                                font.pixelSize: 13
                                font.family: facade.chineseFont || font.family
                            }
                            Text {
                                text: "▶"
                                color: facade.teleop.rightTurnActive ? facade.colorAccent : "#606080"
                                font.pixelSize: 18
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                facade.teleop.rightTurnActive = !facade.teleop.rightTurnActive
                                if (facade.teleop.leftTurnActive) facade.teleop.leftTurnActive = false
                                facade.teleop.lightCommandSent("rightTurn", facade.teleop.rightTurnActive)
                            }
                        }
                        
                        SequentialAnimation on opacity {
                            running: facade.teleop.rightTurnActive
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.5; duration: 500 }
                            NumberAnimation { to: 1.0; duration: 500 }
                        }
                    }
                }
            }
        }
    }
    
    // ==================== 组件2（同级）：灯光/清扫/档位控制区 ====================
    Rectangle {
        id: centerControlsRect
        Layout.fillWidth: true
        Layout.preferredHeight: facade.mainRowAvailH * facade.centerControlsRatio
        Layout.minimumHeight: facade.minControlHeight
        radius: 6
        color: facade.colorPanel
        border.color: facade.colorBorder
        border.width: 1
        
        RowLayout {
            anchors.fill: parent
            anchors.margins: facade.controlAreaMargin
            spacing: facade.controlAreaSpacing
            
            // 左侧：车辆灯光控制（用中列分配宽度比例，避免首帧 width 未就绪）
            ColumnLayout {
                Layout.preferredWidth: facade.centerColAllocW * facade.controlSideColumnRatio
                Layout.fillHeight: true
                spacing: 0
                Item { Layout.fillHeight: true }
                
                GridLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(centerControlsRect.height * 0.85, 190)
                    Layout.alignment: Qt.AlignVCenter
                    columns: 3
                    rowSpacing: 16
                    columnSpacing: 16
                    
                    Components.ControlButton {
                        label: "刹车灯"
                        active: facade.teleop.brakeLightActive
                        onClicked: {
                            facade.teleop.brakeLightActive = !facade.teleop.brakeLightActive
                            facade.teleop.sendControlCommand("light", { name: "brakeLight", active: facade.teleop.brakeLightActive })
                        }
                    }
                    Components.ControlButton {
                        label: "工作灯"
                        active: facade.teleop.workLightActive
                        onClicked: {
                            facade.teleop.workLightActive = !facade.teleop.workLightActive
                            facade.teleop.sendControlCommand("light", { name: "workLight", active: facade.teleop.workLightActive })
                        }
                    }
                    Components.ControlButton {
                        label: "左转灯"
                        active: facade.teleop.leftTurnActive
                        onClicked: {
                            facade.teleop.leftTurnActive = !facade.teleop.leftTurnActive
                            if (facade.teleop.rightTurnActive) facade.teleop.rightTurnActive = false
                            facade.teleop.sendControlCommand("light", { name: "leftTurn", active: facade.teleop.leftTurnActive })
                        }
                    }
                    Components.ControlButton {
                        label: "右转灯"
                        active: facade.teleop.rightTurnActive
                        onClicked: {
                            facade.teleop.rightTurnActive = !facade.teleop.rightTurnActive
                            if (facade.teleop.leftTurnActive) facade.teleop.leftTurnActive = false
                            facade.teleop.sendControlCommand("light", { name: "rightTurn", active: facade.teleop.rightTurnActive })
                        }
                    }
                    Components.ControlButton {
                        label: "近光"
                        active: facade.teleop.headlightActive
                        onClicked: {
                            facade.teleop.headlightActive = !facade.teleop.headlightActive
                            facade.teleop.sendControlCommand("light", { name: "headlight", active: facade.teleop.headlightActive })
                        }
                    }
                    Components.ControlButton {
                        label: "警示"
                        active: facade.teleop.warningLightActive
                        onClicked: {
                            facade.teleop.warningLightActive = !facade.teleop.warningLightActive
                            facade.teleop.sendControlCommand("light", { name: "warning", active: facade.teleop.warningLightActive })
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
            
            // 中间：速度圆圈（用 facade.mainRowAvailH 比例估算高度，避免首帧依赖）
            Item {
                id: speedometerContainer
                Layout.preferredWidth: Math.min(Math.max(0, facade.mainRowAvailH * facade.centerControlsRatio - facade.controlAreaMargin * 2), facade.controlSpeedometerSize)
                Layout.preferredHeight: Layout.preferredWidth
                Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
                
                // 速度表背景环
                Rectangle {
                    id: speedometerBg
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height) * 0.95
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: 3
                    border.color: facade.colorBorder
                    
                    // 速度弧线
                    Canvas {
                        id: speedArc
                        anchors.fill: parent
                        
                        property real speedValue: facade.teleop.displaySpeed
                        property real steerValue: facade.teleop.displaySteering
                        
                        onSpeedValueChanged: requestPaint()
                        onSteerValueChanged: requestPaint()
                        
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            
                            var cx = width / 2
                            var cy = height / 2
                            var outerRadius = width / 2 - 4
                            var innerRadius = outerRadius - 8
                            
                            // 计算速度比例 (0-100 km/h)
                            var speedRatio = Math.min(1, Math.max(0, speedValue / 100))
                            
                            // 起始角度 -135° 到 135° (270°范围)
                            var startAngle = -225 * Math.PI / 180
                            var endAngle = 45 * Math.PI / 180
                            var totalAngle = endAngle - startAngle
                            var currentAngle = startAngle + totalAngle * speedRatio
                            
                            // 背景弧
                            ctx.strokeStyle = "#2A2A3E"
                            ctx.lineWidth = 8
                            ctx.lineCap = "round"
                            ctx.beginPath()
                            ctx.arc(cx, cy, outerRadius - 4, startAngle, endAngle)
                            ctx.stroke()
                            
                            // 速度弧（渐变色）
                            var arcColor = speedRatio <= 0.6 ? facade.colorAccent : 
                                           (speedRatio <= 0.85 ? facade.colorWarning : facade.colorDanger)
                            ctx.strokeStyle = arcColor
                            ctx.lineWidth = 8
                            ctx.lineCap = "round"
                            ctx.beginPath()
                            ctx.arc(cx, cy, outerRadius - 4, startAngle, currentAngle)
                            ctx.stroke()
                            
                            // 速度指针
                            ctx.save()
                            ctx.translate(cx, cy)
                            ctx.rotate(currentAngle + Math.PI / 2)
                            
                            ctx.fillStyle = facade.colorTextPrimary
                            ctx.beginPath()
                            ctx.moveTo(0, -innerRadius + 15)
                            ctx.lineTo(-4, 8)
                            ctx.lineTo(4, 8)
                            ctx.closePath()
                            ctx.fill()
                            
                            // 中心圆点
                            ctx.beginPath()
                            ctx.arc(0, 0, 6, 0, Math.PI * 2)
                            ctx.fillStyle = facade.colorBorderActive
                            ctx.fill()
                            
                            ctx.restore()
                            
                            // 转向指示（小三角）
                            if (Math.abs(steerValue) > 1) {
                                var steerAngle = steerValue * Math.PI / 180 * 0.5  // 缩放转向角度显示
                                ctx.save()
                                ctx.translate(cx, cy + 25)
                                ctx.rotate(steerAngle)
                                
                                ctx.fillStyle = "#70B0FF"
                                ctx.beginPath()
                                ctx.moveTo(0, -12)
                                ctx.lineTo(-6, 4)
                                ctx.lineTo(6, 4)
                                ctx.closePath()
                                ctx.fill()
                                ctx.restore()
                            }
                        }
                        
                        Connections {
                            target: facade.teleop
                            function onVehicleSpeedChanged() { speedArc.requestPaint() }
                            function onSteeringAngleChanged() { speedArc.requestPaint() }
                            function onTargetSpeedChanged() { speedArc.requestPaint() }
                        }
                        Connections {
                            target: facade.appServices.vehicleStatus
                            enabled: facade.appServices.vehicleStatus !== null
                            function onSpeedChanged() { speedArc.requestPaint() }
                            function onSteeringChanged() { speedArc.requestPaint() }
                            function onGearChanged() { speedArc.requestPaint() }
                        }
                    }
                    
                    // 速度数值显示
                    Column {
                        anchors.centerIn: parent
                        spacing: 0
                        
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Math.round(facade.teleop.displaySpeed).toString()
                            color: facade.colorTextPrimary
                            font.pixelSize: speedometerBg.width * 0.28
                            font.bold: true
                        }
                        
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "km/h"
                            color: facade.colorTextSecondary
                            font.pixelSize: speedometerBg.width * 0.1
                        }
                        
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "↻ " + facade.teleop.displaySteering.toFixed(0) + "°"
                            color: "#70B0FF"
                            font.pixelSize: speedometerBg.width * 0.09
                            font.family: facade.chineseFont || font.family
                            visible: Math.abs(facade.teleop.displaySteering) > 1
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "档位 " + facade.teleop.displayGear
                            color: "#9CB2DF"
                            font.pixelSize: speedometerBg.width * 0.09
                            font.bold: true
                            font.family: facade.chineseFont || font.family
                        }
                    }
                }
            }
            
            // 右侧：清扫功能控制
            ColumnLayout {
                Layout.preferredWidth: facade.centerColAllocW * facade.controlSideColumnRatio
                Layout.fillHeight: true
                spacing: 0
                Item { Layout.fillHeight: true }
                
                GridLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(centerControlsRect.height * 0.85, 190)
                    Layout.alignment: Qt.AlignVCenter
                    columns: 3
                    rowSpacing: 4
                    columnSpacing: 4
                    
                    Components.ControlButton {
                        label: "清扫"
                        active: facade.teleop.sweepActive
                        onClicked: {
                            // ★ 检查远驾接管状态
                            var remoteControlEnabled = false;
                            if (facade.appServices.vehicleStatus) {
                                remoteControlEnabled = (facade.appServices.vehicleStatus.drivingMode === "远驾");
                            }
                            if (!remoteControlEnabled) {
                                console.log("[Client][UI][Sweep] ⚠ 远驾接管未启用，无法发送清扫命令")
                                return;
                            }

                            facade.teleop.sweepActive = !facade.teleop.sweepActive
                            console.log("[Client][UI][Sweep] 清扫状态: " + (facade.teleop.sweepActive ? "启用" : "禁用") + "，准备发送");
                            
                            // ★ 使用统一发送接口 (DataChannel 优先)
                            facade.teleop.sendControlCommand("sweep", { name: "sweep", active: facade.teleop.sweepActive });
                            facade.teleop.sweepCommandSent("sweep", facade.teleop.sweepActive)
                        }
                    }
                    Components.ControlButton {
                        label: "洒水"
                        active: facade.teleop.waterSprayActive
                        onClicked: {
                            facade.teleop.waterSprayActive = !facade.teleop.waterSprayActive
                            facade.teleop.sendControlCommand("sweep", { name: "waterSpray", active: facade.teleop.waterSprayActive })
                        }
                    }
                    Components.ControlButton {
                        label: "吸污"
                        active: facade.teleop.suctionActive
                        onClicked: {
                            facade.teleop.suctionActive = !facade.teleop.suctionActive
                            facade.teleop.sendControlCommand("sweep", { name: "suction", active: facade.teleop.suctionActive })
                        }
                    }
                    Components.ControlButton {
                        label: "卸料"
                        active: facade.teleop.dumpActive
                        onClicked: {
                            facade.teleop.dumpActive = !facade.teleop.dumpActive
                            facade.teleop.sendControlCommand("sweep", { name: "dump", active: facade.teleop.dumpActive })
                        }
                    }
                    Components.ControlButton {
                        label: "喇叭"
                        active: facade.teleop.hornActive
                        onClicked: {
                            facade.teleop.hornActive = !facade.teleop.hornActive
                            facade.teleop.sendControlCommand("sweep", { name: "horn", active: facade.teleop.hornActive })
                        }
                    }
                    Components.ControlButton {
                        label: "灯光"
                        active: facade.teleop.workLampActive
                        onClicked: {
                            facade.teleop.workLampActive = !facade.teleop.workLampActive
                            facade.teleop.sendControlCommand("sweep", { name: "workLamp", active: facade.teleop.workLampActive })
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
    
    // ==================== 组件3（同级）：箱体/急停车速/设备/清扫状态/档位选择 ====================
    Rectangle {
        id: centerDashboardRect
        Layout.fillWidth: true
        Layout.preferredHeight: facade.mainRowAvailH * facade.centerDashboardRatio
        Layout.minimumHeight: facade.minDashboardHeight
        readonly property int cardHeight: Math.max(104, height - facade.dashboardMargin * 2)
        radius: 14
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1D2538" }
            GradientStop { position: 1.0; color: "#131B2D" }
        }
        border.color: "#324466"
        border.width: 1
        
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 1
            height: 2
            radius: 12
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.2; color: "#3388FF" }
                GradientStop { position: 0.5; color: "#55AAFF" }
                GradientStop { position: 0.8; color: "#3388FF" }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
        
        RowLayout {
            anchors.fill: parent
            anchors.margins: facade.dashboardMargin
            spacing: facade.dashboardSpacing
            
            // ======================== 档位显示 ========================
            Item {
                Layout.preferredWidth: facade.dashboardGearWidth
                Layout.fillHeight: true
                
                Components.DashboardCard {
                    anchors.centerIn: parent
                    width: parent.width - 4
                    height: centerDashboardRect.cardHeight
                    Column {
                        anchors.centerIn: parent
                        spacing: 7
                        
                        // 档位圆环（双环设计）
                        Item {
                            width: 56; height: 56
                            anchors.horizontalCenter: parent.horizontalCenter
                            
                            // 外圈光晕
                            Rectangle {
                                anchors.centerIn: parent
                                width: 58; height: 58; radius: 29
                                color: "transparent"
                                border.width: 1
                                border.color: Qt.rgba(0.33, 0.6, 1.0, 0.15)
                            }
                            
                            // 主圆环
                            Rectangle {
                                anchors.centerIn: parent
                                width: 52; height: 52; radius: 26
                                color: "transparent"
                                border.width: 3
                                border.color: "#3A6FC4"
                                
                                // 档位指示弧
                                Canvas {
                                    anchors.fill: parent
                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.clearRect(0, 0, width, height)
                                        
                                        // 背景弧
                                        ctx.strokeStyle = Qt.rgba(0.2, 0.35, 0.6, 0.3)
                                        ctx.lineWidth = 4
                                        ctx.lineCap = "round"
                                        ctx.beginPath()
                                        ctx.arc(width/2, height/2, 21, -Math.PI * 0.75, Math.PI * 0.75)
                                        ctx.stroke()
                                        
                                        // 高亮弧（渐变感）
                                        ctx.strokeStyle = "#55AAFF"
                                        ctx.lineWidth = 4
                                        ctx.lineCap = "round"
                                        ctx.shadowColor = "#55AAFF"
                                        ctx.shadowBlur = 6
                                        ctx.beginPath()
                                        ctx.arc(width/2, height/2, 21, -Math.PI * 0.75, Math.PI * 0.25)
                                        ctx.stroke()
                                    }
                                }
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: facade.teleop.displayGear
                                    color: "#E8F0FF"
                                    font.pixelSize: 24
                                    font.bold: true
                                    font.family: "Consolas"
                                }
                            }
                        }
                        
                        Text {
                            text: "档位反馈"
                            color: "#9CB2DF"
                            font.pixelSize: 11
                            font.bold: true
                            font.family: facade.chineseFont || font.family
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }
            }
            
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                Layout.topMargin: facade.dashboardSplitterMargin
                Layout.bottomMargin: facade.dashboardSplitterMargin
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.3; color: "#2A3456" }
                    GradientStop { position: 0.7; color: "#2A3456" }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
            
            // ======================== 水箱 + 垃圾箱 ========================
            ColumnLayout {
                Layout.preferredWidth: facade.dashboardTankWidth
                Layout.fillHeight: true
                spacing: 6
                
                Item { Layout.fillHeight: true }

                Text {
                    text: "箱体状态"
                    color: "#9CB2DF"
                    font.pixelSize: 11
                    font.bold: true
                    font.family: facade.chineseFont || font.family
                    Layout.alignment: Qt.AlignHCenter
                }
                
                // 水箱显示
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    radius: 8
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#18263C" }
                        GradientStop { position: 1.0; color: "#122236" }
                    }
                    border.color: facade.teleop.waterTankLevel < 20 ? "#C05A5A" : "#2F4D75"
                    border.width: 1
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 4
                        
                        // // 水滴图标容器（尺寸调小以适应更窄的布局）
                        // Rectangle {
                        //     width: 18; height: 18; radius: 5
                        //     color: Qt.rgba(0.2, 0.5, 1.0, 0.15)
                            
                        //     Text {
                        //         anchors.centerIn: parent
                        //         text: "💧"
                        //         font.pixelSize: 11
                        //     }
                        // }
                        
                        Text {
                            text: "水箱"
                            color: "#8899BB"
                            font.pixelSize: 11
                            font.family: facade.chineseFont || font.family
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        Text {
                            id: waterTankPercentText
                            text: Math.round(facade.teleop.waterTankLevel) + "%"
                            color: facade.teleop.waterTankLevel < 20 ? "#FF6B6B" : "#55BBFF"
                            font.pixelSize: 11
                            font.bold: true
                            font.family: "Consolas"
                        }
                    }
                    
                    // 底部水位条
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottomMargin: 1
                        anchors.leftMargin: 1
                        anchors.rightMargin: 1
                        width: parent.width * Math.min(facade.teleop.waterTankLevel / 100.0, 1.0)
                        height: 3
                        radius: 2
                        color: facade.teleop.waterTankLevel < 20 ? "#FF6B6B" : "#3388FF"
                        opacity: 0.8
                        
                        Behavior on width {
                            NumberAnimation { duration: 500; easing.type: Easing.OutCubic }
                        }
                    }
                    
                    Component.onCompleted: {
                        console.log("[Client][UI][WaterTank] 水箱组件初始化: " + facade.teleop.waterTankLevel + "%")
                    }
                }
                
                // 监听水箱水位变化
                Connections {
                    target: facade.teleop
                    ignoreUnknownSignals: true
                    function onWaterTankLevelChanged() {
                        var percent = Math.round(facade.teleop.waterTankLevel)
                        console.log("[Client][UI][WaterTank] 水箱水位变化: " + facade.teleop.waterTankLevel + " -> " + percent + "%")
                        if (typeof waterTankPercentText !== "undefined") {
                            waterTankPercentText.text = percent + "%"
                            waterTankPercentText.color = percent < 20 ? "#FF6B6B" : "#55BBFF"
                        }
                    }
                }
                
                // 垃圾箱显示
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    radius: 8
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1D2436" }
                        GradientStop { position: 1.0; color: "#171D2D" }
                    }
                    border.color: facade.teleop.trashBinLevel > 80 ? Qt.rgba(1, 0.4, 0.4, 0.6) : "#2F4D75"
                    border.width: 1
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 4
                        
                        // // 垃圾箱图标容器（尺寸调小以适应更窄的布局）
                        // Rectangle {
                        //     width: 18; height: 18; radius: 5
                        //     color: facade.teleop.trashBinLevel > 80 ? Qt.rgba(1, 0.3, 0.3, 0.15) : Qt.rgba(0.3, 0.8, 0.5, 0.15)
                            
                        //     Text {
                        //         anchors.centerIn: parent
                        //         text: "🗑️"
                        //         font.pixelSize: 11
                        //     }
                        // }
                        
                        Text {
                            text: "垃圾箱"
                            color: "#8899BB"
                            font.pixelSize: 11
                            font.family: facade.chineseFont || font.family
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        Text {
                            id: trashBinPercentText
                            text: Math.round(facade.teleop.trashBinLevel) + "%"
                            color: facade.teleop.trashBinLevel > 80 ? "#FF6B6B" : "#66DDAA"
                            font.pixelSize: 11
                            font.bold: true
                            font.family: "Consolas"
                        }
                    }
                    
                    // 底部填充条
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottomMargin: 1
                        anchors.leftMargin: 1
                        anchors.rightMargin: 1
                        width: parent.width * Math.min(facade.teleop.trashBinLevel / 100.0, 1.0)
                        height: 3
                        radius: 2
                        color: facade.teleop.trashBinLevel > 80 ? "#FF6B6B" : "#44BB88"
                        opacity: 0.8
                        
                        Behavior on width {
                            NumberAnimation { duration: 500; easing.type: Easing.OutCubic }
                        }
                    }
                    
                    // 高水位脉冲动画
                    SequentialAnimation on border.color {
                        running: facade.teleop.trashBinLevel > 80
                        loops: Animation.Infinite
                        ColorAnimation { to: "#FF4444"; duration: 800 }
                        ColorAnimation { to: "#553333"; duration: 800 }
                    }
                    
                    Component.onCompleted: {
                        console.log("[Client][UI][TrashBin] 垃圾箱组件初始化: " + facade.teleop.trashBinLevel + "%")
                    }
                }
                
                // 监听垃圾箱水位变化
                Connections {
                    target: facade.teleop
                    ignoreUnknownSignals: true
                    function onTrashBinLevelChanged() {
                        var percent = Math.round(facade.teleop.trashBinLevel)
                        var isHigh = percent > 80
                        console.log("[Client][UI][TrashBin] 垃圾箱水位变化: " + facade.teleop.trashBinLevel + " -> " + percent + "% (高水位: " + (isHigh ? "是" : "否") + ")")
                        if (typeof trashBinPercentText !== "undefined") {
                            trashBinPercentText.text = percent + "%"
                            trashBinPercentText.color = isHigh ? "#FF6B6B" : "#66DDAA"
                        }
                    }
                }
                
                Item { Layout.fillHeight: true }
            }
            
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                Layout.topMargin: facade.dashboardSplitterMargin
                Layout.bottomMargin: facade.dashboardSplitterMargin
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.3; color: "#2A3456" }
                    GradientStop { position: 0.7; color: "#2A3456" }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
            
            // ======================== 速度控制 ========================
            Item {
                Layout.preferredWidth: facade.dashboardSpeedWidth
                Layout.fillHeight: true
                
                Components.DashboardCard {
                    anchors.centerIn: parent
                    width: Math.max(0, parent.width - 2)
                    height: centerDashboardRect.cardHeight
                    Column {
                        anchors.centerIn: parent
                        spacing: 8
                    
                        Text {
                            text: "目标车速 km/h"
                            color: "#9CB2DF"
                            font.pixelSize: 11
                            font.bold: true
                            font.family: facade.chineseFont || font.family
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 8
                        
                            // ★ 急停按钮（带动画效果，尺寸调大）
                            Rectangle {
                                id: emergencyStopButton
                                width: 52; height: 46; radius: 12
                            
                                // 背景渐变
                                gradient: Gradient {
                                    GradientStop {
                                        position: 0.0
                                        color: facade.teleop.emergencyStopPressed ? "#FF3A3A" : "#2C344C"
                                    }
                                    GradientStop {
                                        position: 1.0
                                        color: facade.teleop.emergencyStopPressed ? "#C91010" : "#212A40"
                                    }
                                }
                            
                                border.width: 2
                                border.color: facade.teleop.emergencyStopPressed ? "#FF7B7B" : "#4B5F87"
                            
                                // 急停图标+文字
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 1
                                
                                Text {
                                    text: facade.teleop.emergencyStopPressed ? "✅" : "⛔"
                                    font.pixelSize: facade.teleop.emergencyStopPressed ? 10 : 12
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                                Text {
                                    text: facade.teleop.emergencyStopPressed ? "恢复" : "急停"
                                    color: facade.teleop.emergencyStopPressed ? "#FFFFFF" : "#CCDDEE"
                                    font.pixelSize: 10
                                    font.bold: true
                                    font.family: facade.chineseFont || font.family
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                            }
                            
                                // 按下光晕
                                Rectangle {
                                    anchors.fill: parent
                                    radius: parent.radius
                                    color: "transparent"
                                    border.width: facade.teleop.emergencyStopPressed ? 3 : 0
                                    border.color: Qt.rgba(1, 0.3, 0.3, 0.5)
                                    visible: facade.teleop.emergencyStopPressed
                                
                                // 脉冲动画
                                SequentialAnimation on opacity {
                                    running: facade.teleop.emergencyStopPressed
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 1.0; duration: 600 }
                                    NumberAnimation { to: 0.3; duration: 600 }
                                }
                            }
                            
                                // 颜色过渡动画
                                Behavior on border.color {
                                    ColorAnimation { duration: 200 }
                                }
                            
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        console.log("[Client][UI][EmergencyStop] ========== 急停/恢复按钮点击 ==========")
                                        
                                        if (facade.teleop.emergencyStopPressed) {
                                            console.log("[Client][UI][EmergencyStop] 正在执行恢复...")
                                            facade.teleop.emergencyStopPressed = false
                                            emergencyStopButton.enabled = true
                                            
                                            var smRec = facade.appServices.safetyMonitor
                                            if (smRec && typeof smRec.clearEmergency === "function")
                                                smRec.clearEmergency()
                                            
                                            console.log("[Client][UI][EmergencyStop] ✓ 已发送恢复请求")
                                        } else {
                                            console.log("[Client][UI][EmergencyStop] 正在执行急停...")
                                            console.log("[Client][UI][EmergencyStop] 当前目标速度: " + facade.teleop.targetSpeed)
                                            
                                            facade.teleop.emergencyStopPressed = true
                                            facade.teleop.targetSpeed = 0.0
                                            if (typeof targetSpeedInput !== "undefined")
                                                targetSpeedInput.text = "0.0"
                                            
                                            var ssmEs = facade.appServices.systemStateMachine
                                            if (ssmEs && typeof ssmEs.fireByName === "function")
                                                ssmEs.fireByName("EMERGENCY_STOP")
                                            var vcEs = facade.appServices.vehicleControl
                                            if (vcEs && typeof vcEs.requestEmergencyStop === "function")
                                                vcEs.requestEmergencyStop()
                                            else {
                                                facade.teleop.sendControlCommand("brake", { value: 1.0 })
                                                facade.teleop.sendControlCommand("speed", { value: 0.0 })
                                            }
                                            console.log("[Client][UI][EmergencyStop] ✓ 已发送急停命令")
                                        }
                                        console.log("[Client][UI][EmergencyStop] ========================================")
                                    }
                                }
                            }
                        
                            // 车端反馈车速（与 vehicle/status、CARLA 桥一致）
                            Column {
                                spacing: 2
                                // width: parent.width  // ★ 严重缺陷：Row 内部子项 width: parent.width 会导致 polish 递归死循环 (QQuickItem::polish loop)
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    visible: facade.appServices.mqttController
                                             && facade.appServices.mqttController.mqttBrokerConnected
                                    text: "车端 " + Math.round(facade.teleop.reportedSpeedKmh).toString() + " km/h"
                                    color: "#7AE2A8"
                                    font.pixelSize: 11
                                    font.family: facade.chineseFont || font.family
                                }
                            }

                            // ★ 目标速度输入框（美化版，尺寸调大）
                            Rectangle {
                                width: 96; height: 42; radius: 10
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#182236" }
                                    GradientStop { position: 1.0; color: "#111A2C" }
                                }
                                border.width: 2
                                border.color: targetSpeedInput.activeFocus ? "#5E93FF" : "#36507D"
                            
                                // 聚焦光效
                                Rectangle {
                                    anchors.fill: parent
                                    radius: parent.radius
                                    color: "transparent"
                                    border.width: targetSpeedInput.activeFocus ? 1 : 0
                                    border.color: Qt.rgba(0.27, 0.53, 1.0, 0.3)
                                    anchors.margins: -2
                                    visible: targetSpeedInput.activeFocus
                                }
                            
                                Behavior on border.color {
                                    ColorAnimation { duration: 200 }
                                }
                            
                                TextField {
                                id: targetSpeedInput
                                anchors.fill: parent
                                anchors.margins: 2
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                text: facade.teleop.targetSpeed.toFixed(1)
                                color: "#E8F0FF"
                                font.pixelSize: 17
                                font.bold: true
                                font.family: "Consolas"
                                
                                background: Rectangle {
                                    color: "transparent"
                                }
                                
                                validator: DoubleValidator {
                                    bottom: 0.0
                                    top: 100.0
                                    decimals: 1
                                }
                                
                                onEditingFinished: {
                                    console.log("[Client][UI][Speed] ========== 目标速度输入完成 ==========")
                                    console.log("[Client][UI][Speed] 输入文本: " + text)
                                    
                                    var newSpeed = parseFloat(text)
                                    if (isNaN(newSpeed)) {
                                        console.log("[Client][UI][Speed] ⚠ 输入无效，恢复为当前值: " + facade.teleop.targetSpeed.toFixed(1))
                                        text = facade.teleop.targetSpeed.toFixed(1)
                                        return
                                    }
                                    newSpeed = Math.max(0.0, Math.min(100.0, newSpeed))
                                    
                                    // [SystemicFix] 仅在变化超过 0.05 时才更新，且只有在焦点消失后同步，消除 Binding Loop 抖动
                                    if (Math.abs(facade.teleop.targetSpeed - newSpeed) > 0.05) {
                                        facade.teleop.targetSpeed = newSpeed
                                        console.log("[Client][UI][Speed] 新目标速度: " + facade.teleop.targetSpeed.toFixed(1) + " km/h")
                                    }
                                    
                                    text = facade.teleop.targetSpeed.toFixed(1)
                                    
                                    if (facade.teleop.targetSpeed > 0.0) {
                                        facade.teleop.emergencyStopPressed = false
                                        console.log("[Client][UI][Speed] 目标速度非0，急停按钮状态已重置为: 未按下（正常颜色）")
                                    }
                                    
                                    var remoteControlEnabled = false;
                                    if (facade.appServices.vehicleStatus) {
                                        remoteControlEnabled = (facade.appServices.vehicleStatus.drivingMode === "远驾");
                                    }
                                    if (!remoteControlEnabled) {
                                        console.log("[Client][UI][Speed] ⚠ 远驾接管未启用，无法发送速度命令")
                                        console.log("[Client][UI][Speed] 当前驾驶模式: " + (facade.appServices.vehicleStatus ? facade.appServices.vehicleStatus.drivingMode : "未知"))
                                        return;
                                    }
                                    
                                    // ★ 使用统一发送接口 (DataChannel 优先)
                                    facade.teleop.sendControlCommand("speed", { value: facade.teleop.targetSpeed })
                                    console.log("[Client][UI][Speed] ✓ 已通过统一接口发送速度命令")
                                    console.log("[Client][UI][Speed] ========================================")
                                }
                                
                                // [Fix] 彻底移除 onTextChanged 以解决与键盘调速定时器的 Binding Loop 冲突
                                // 目标速度的实时显示通过 text 属性的绑定实现
                                }
                            }
                        }
                    }
                }
            }
            
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                Layout.topMargin: facade.dashboardSplitterMargin
                Layout.bottomMargin: facade.dashboardSplitterMargin
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.3; color: "#2A3456" }
                    GradientStop { position: 0.7; color: "#2A3456" }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
            
            // ======================== 设备状态 ========================
            Item {
                Layout.preferredWidth: facade.dashboardStatusWidth
                Layout.fillHeight: true
                
                Column {
                    anchors.centerIn: parent
                    spacing: 6
                    
                    Text {
                        text: "设备状态"
                        color: "#7B8DB8"
                        font.pixelSize: 11
                        font.bold: true
                        font.family: facade.chineseFont || font.family
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    
                    // 状态网格（卡片化）
                    Grid {
                        columns: 2
                        rowSpacing: 4
                        columnSpacing: 6
                        anchors.horizontalCenter: parent.horizontalCenter
                        
                        // 定义支持绑定的设备列表 (通过 Connections 更新)
                        property var deviceList: [
                            { name: "左盘刷", key: "leftBrushStatus", status: true },
                            { name: "右盘刷", key: "rightBrushStatus", status: true },
                            { name: "主刷", key: "mainBrushStatus", status: true },
                            { name: "吸嘴", key: "nozzleStatus", status: true }
                        ]
                        
                        Connections {
                            target: facade.appServices.vehicleStatus
                            ignoreUnknownSignals: true
                            function onDeviceStatusChanged() {
                                // 遍历列表并更新状态 (假设 facade.appServices.vehicleStatus 有对应的布尔属性)
                                for (var i = 0; i < parent.deviceList.length; i++) {
                                    var item = parent.deviceList[i];
                                    if (typeof facade.appServices.vehicleStatus[item.key] === "boolean") {
                                        item.status = facade.appServices.vehicleStatus[item.key];
                                    }
                                }
                                // 触发 QML 刷新
                                var temp = parent.deviceList;
                                parent.deviceList = [];
                                parent.deviceList = temp;
                            }
                        }
                        
                        Repeater {
                            model: parent.deviceList
                            
                            Rectangle {
                                width: 52; height: 24; radius: 6
                                color: modelData.status ? Qt.rgba(0.2, 0.8, 0.5, 0.1) : Qt.rgba(1, 0.3, 0.3, 0.1)
                                border.width: 1
                                border.color: modelData.status ? Qt.rgba(0.2, 0.8, 0.5, 0.25) : Qt.rgba(1, 0.3, 0.3, 0.25)
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 3
                                    
                                    Rectangle {
                                        width: 6; height: 6; radius: 3
                                        color: modelData.status ? "#44CC88" : "#FF5555"
                                        anchors.verticalCenter: parent.verticalCenter
                                        
                                        // 在线呼吸灯效果
                                        SequentialAnimation on opacity {
                                            running: modelData.status
                                            loops: Animation.Infinite
                                            NumberAnimation { to: 0.5; duration: 1500 }
                                            NumberAnimation { to: 1.0; duration: 1500 }
                                        }
                                    }
                                    
                                    Text {
                                        text: modelData.name
                                        color: modelData.status ? "#88CCAA" : "#CC8888"
                                        font.pixelSize: 9
                                        font.family: facade.chineseFont || font.family
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                Layout.topMargin: facade.dashboardSplitterMargin
                Layout.bottomMargin: facade.dashboardSplitterMargin
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.3; color: "#2A3456" }
                    GradientStop { position: 0.7; color: "#2A3456" }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
            
            // ======================== 清扫状态 ========================
            Item {
                Layout.preferredWidth: facade.dashboardProgressWidth
                Layout.fillHeight: true
                
                Components.DashboardCard {
                    anchors.centerIn: parent
                    width: parent.width - 2
                    height: centerDashboardRect.cardHeight
                    Column {
                        anchors.centerIn: parent
                        spacing: 8
                    
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 6
                        
                        Text {
                            text: "🧹"
                            font.pixelSize: 16
                        }
                            Text {
                                text: "清扫状态"
                                color: "#9CB2DF"
                                font.pixelSize: 13
                                font.bold: true
                                font.family: facade.chineseFont || font.family
                            }
                        }
                    
                        // 进度环形显示
                        Item {
                        width: 52; height: 52
                        anchors.horizontalCenter: parent.horizontalCenter
                        
                        property real progressValue: facade.teleop.cleaningTotal > 0 ? (facade.teleop.cleaningCurrent / facade.teleop.cleaningTotal) : 0
                        
                        // 背景圆弧
                        Canvas {
                            id: progressCanvas
                            anchors.fill: parent
                            
                            property real progress: parent.progressValue
                            
                            onProgressChanged: requestPaint()
                            
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                var cx = width / 2
                                var cy = height / 2
                                var r = 21
                                var startAngle = -Math.PI / 2
                                
                                // 背景弧
                                ctx.strokeStyle = "#1E2538"
                                ctx.lineWidth = 5
                                ctx.lineCap = "round"
                                ctx.beginPath()
                                ctx.arc(cx, cy, r, 0, Math.PI * 2)
                                ctx.stroke()
                                
                                // 进度弧（完整环形显示）
                                if (progress > 0) {
                                    ctx.strokeStyle = "#44BBFF"
                                    ctx.lineWidth = 5
                                    ctx.lineCap = "round"
                                    ctx.shadowColor = "#44BBFF"
                                    ctx.shadowBlur = 4
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, r, startAngle, startAngle + Math.PI * 2 * Math.min(progress, 1.0))
                                    ctx.stroke()
                                }
                            }
                        }
                        
                        // 中心百分比
                        Text {
                            id: cleaningPercentText
                            anchors.centerIn: parent
                            text: {
                                var percent = facade.teleop.cleaningTotal > 0 ? Math.round(facade.teleop.cleaningCurrent * 100 / facade.teleop.cleaningTotal) : 0
                                return percent + "%"
                            }
                            color: "#55CCFF"
                            font.pixelSize: 14
                            font.bold: true
                            font.family: "Consolas"
                        }
                        }
                    
                        Component.onCompleted: {
                            var cleaningPercent = facade.teleop.cleaningTotal > 0 ? Math.round(facade.teleop.cleaningCurrent * 100 / facade.teleop.cleaningTotal) : 0
                            console.log("[Client][UI][CleaningProgress] 清扫进度组件初始化: " + facade.teleop.cleaningCurrent + " / " + facade.teleop.cleaningTotal + " m (" + cleaningPercent + "%)")
                        }
                    }
                }
                
                // 监听清扫进度变化
                Connections {
                    target: facade.teleop
                    ignoreUnknownSignals: true
                    function onCleaningCurrentChanged() {
                        var percent = facade.teleop.cleaningTotal > 0 ? Math.round(facade.teleop.cleaningCurrent * 100 / facade.teleop.cleaningTotal) : 0
                        console.log("[Client][UI][CleaningProgress] 清扫进度变化: " + facade.teleop.cleaningCurrent + " / " + facade.teleop.cleaningTotal + " m -> " + percent + "%")
                        if (typeof cleaningPercentText !== "undefined") {
                            cleaningPercentText.text = percent + "%"
                        }
                        progressCanvas.requestPaint()
                    }
                    function onCleaningTotalChanged() {
                        var percent = facade.teleop.cleaningTotal > 0 ? Math.round(facade.teleop.cleaningCurrent * 100 / facade.teleop.cleaningTotal) : 0
                        console.log("[Client][UI][CleaningProgress] 清扫总量变化: " + facade.teleop.cleaningTotal + " m (当前: " + facade.teleop.cleaningCurrent + " m, 进度: " + percent + "%)")
                        if (typeof cleaningPercentText !== "undefined") {
                            cleaningPercentText.text = percent + "%"
                        }
                        progressCanvas.requestPaint()
                    }
                }
            }
            
            Item { Layout.fillWidth: true }
            
            // 分割线
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                Layout.topMargin: 8
                Layout.bottomMargin: 8
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.3; color: "#2A3456" }
                    GradientStop { position: 0.7; color: "#2A3456" }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
            
            // ======================== 档位选择 ========================
            Item {
                Layout.preferredWidth: facade.dashboardGearSelectWidth
                Layout.fillHeight: true
                
                Components.DashboardCard {
                    anchors.centerIn: parent
                    width: parent.width - 2
                    height: centerDashboardRect.cardHeight
                    Column {
                        anchors.centerIn: parent
                        spacing: 8
                    
                        Text {
                            text: "档位选择"
                            color: "#9CB2DF"
                            font.pixelSize: 11
                            font.bold: true
                            font.family: facade.chineseFont || font.family
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    
                        Row {
                            spacing: 10
                            anchors.horizontalCenter: parent.horizontalCenter
                        
                        Repeater {
                            model: ["P", "N", "R", "D"]
                            
                            Rectangle {
                                id: gearButton
                                width: 40; height: 40; radius: 20
                                
                                property bool isSelected: modelData === facade.teleop.displayGear
                                readonly property color gearBaseColor: {
                                    if (modelData === "R") return "#E85D5D"
                                    if (modelData === "D") return "#4F8DFF"
                                    if (modelData === "P") return "#7D6BFF"
                                    return "#8A96B8"
                                }
                                
                                gradient: Gradient {
                                    GradientStop {
                                        position: 0.0
                                        color: {
                                            if (gearButton.isSelected) return Qt.lighter(gearButton.gearBaseColor, 1.12)
                                            if (gearMouseArea.pressed) return "#1D2640"
                                            if (gearMouseArea.containsMouse) return "#2A3553"
                                            return "#232B42"
                                        }
                                    }
                                    GradientStop {
                                        position: 1.0
                                        color: {
                                            if (gearButton.isSelected) return Qt.darker(gearButton.gearBaseColor, 1.18)
                                            if (gearMouseArea.pressed) return "#141B2E"
                                            if (gearMouseArea.containsMouse) return "#1C2440"
                                            return "#171E33"
                                        }
                                    }
                                }
                                
                                border.width: 2
                                border.color: gearButton.isSelected ? Qt.lighter(gearButton.gearBaseColor, 1.35) : (gearMouseArea.containsMouse ? "#4E6188" : "#3A4868")
                                
                                // 选中外光环
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 48; height: 48; radius: 24
                                    color: "transparent"
                                    border.width: gearButton.isSelected ? 1 : 0
                                    border.color: gearButton.isSelected ? Qt.rgba(0.9, 0.95, 1.0, 0.45) : "transparent"
                                    visible: gearButton.isSelected
                                    
                                    Behavior on visible {
                                        PropertyAnimation { duration: 200 }
                                    }
                                }
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: gearButton.isSelected ? "#FFFFFF" : "#9CA9C8"
                                    font.pixelSize: 16
                                    font.bold: true
                                    font.family: "Consolas"
                                }
                                
                                // 悬浮/按压效果
                                scale: gearMouseArea.pressed ? 0.95 : (gearMouseArea.containsMouse ? 1.08 : 1.0)
                                
                                Behavior on scale {
                                    NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                                }
                                Behavior on border.color {
                                    ColorAnimation { duration: 200 }
                                }
                                
                                MouseArea {
                                    id: gearMouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: facade.teleop.currentGear = modelData
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
}
