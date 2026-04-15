import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import RemoteDriving 1.0
import "shell" as Shell
import "styles" as ThemeModule

ApplicationWindow {
    id: window
    // 自适应窗口大小
    width: Math.min(Math.max(1280, Screen.width * 0.9), 1920)
    height: Math.min(Math.max(720, Screen.height * 0.9), 1080)
    minimumWidth: 1280
    minimumHeight: 720
    visible: true
    flags: AppContext.useWindowFrame ? Qt.Window : (Qt.Window | Qt.FramelessWindowHint)
    title: windowTitleText
    color: ThemeModule.Theme.colorBackground
    x: (Screen.width - width) / 2
    y: (Screen.height - height) / 2
    
    // ── 非理想 GL 环境告警（软件光栅等；Linux+xcb 默认应硬件呈现，否则进程通常已拒绝启动）──
    Rectangle {
        id: softGlBanner
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: !AppContext.hardwarePresentationOk
        height: visible ? (softGlBannerText.implicitHeight + 14) : 0
        z: 100000
        color: "#8B1538"
        Text {
            id: softGlBannerText
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 8
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: "#FFFFFF"
            text: "当前 OpenGL 呈现环境非理想（软件光栅或探测未通过），四路视频可能卡顿、撕裂或跳帧。"
                  + " 生产远控请使用 GPU（含 docker /dev/dri 或 NVIDIA 挂载）、unset LIBGL_ALWAYS_SOFTWARE；"
                  + " 无 GPU 调试可设 CLIENT_GPU_PRESENTATION_OPTIONAL=1 或 CLIENT_ALLOW_SOFTWARE_PRESENTATION=1。"
        }
    }

    // ── 解码/呈现完整性（EventBus → VideoIntegrityBannerBridge）────────────────
    property bool integrityBannerVisible: false
    property string integrityBannerFullText: ""
    property color integrityBannerBg: "#4A148C"

    Timer {
        id: integrityBannerHideTimer
        interval: 16000
        repeat: false
        onTriggered: window.integrityBannerVisible = false
    }

    Connections {
        target: AppContext.videoIntegrityBannerBridge
        enabled: AppContext.videoIntegrityBannerBridge !== null
        ignoreUnknownSignals: true
        function onDecodeIntegrityBanner(stream, code, title, body, mitigationApplied) {
            window.integrityBannerFullText = title + "\n\n" + body + "\n\n路名: " + stream + "  code: "
                    + code
            window.integrityBannerBg = mitigationApplied ? "#1565C0" : "#E65100"
            window.integrityBannerVisible = true
            integrityBannerHideTimer.restart()
        }
        function onPresentIntegrityBanner(stream, code, title, body, suspectGpuCompositor) {
            window.integrityBannerFullText = title + "\n\n" + body + "\n\n路名: " + stream + "  code: "
                    + code
            window.integrityBannerBg = suspectGpuCompositor ? "#4A148C" : "#BF360C"
            window.integrityBannerVisible = true
            integrityBannerHideTimer.restart()
        }
    }

    Rectangle {
        id: videoIntegrityBanner
        anchors.top: softGlBanner.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: window.integrityBannerVisible
        height: visible ? Math.min(240, videoIntegrityBannerText.implicitHeight + 16) : 0
        z: 99999
        color: window.integrityBannerBg
        Text {
            id: videoIntegrityBannerText
            anchors.fill: parent
            anchors.margins: 8
            text: window.integrityBannerFullText
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: "#FFFFFF"
        }
    }

    // ── 背景渐变 ────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        z: -1
        gradient: Gradient {
            GradientStop { position: 0.0; color: ThemeModule.Theme.colorBackground }
            GradientStop { position: 1.0; color: ThemeModule.Theme.colorSurface }
        }
    }
    
    // ── 窗口标题 ─────────────────────────────────────────────────────
    property string windowTitleText: "远程驾驶客户端"
    
    function updateTitle() {
        var vm = AppContext.vehicleManager
        if (vm) {
            windowTitleText = "远程驾驶客户端 - " + (vm.currentVehicleName || "未选择车辆")
        } else {
            windowTitleText = "远程驾驶客户端"
        }
    }

    Connections {
        target: AppContext.vehicleManager
        ignoreUnknownSignals: true
        function onCurrentVehicleChanged() {
            var vm = AppContext.vehicleManager
            if (vm) {
                window.updateTitle()
            }
        }
    }
    
    // ── 中文字体 ─────────────────────────────────────────────────────
    // 优先使用 AppContext.chineseFont（统一字体来源）
    readonly property string chineseFont: AppContext.chineseFont
    font.family: chineseFont || "DejaVu Sans"
    font.pixelSize: 12

    // ── 主布局：会话工作区（模块化壳层，接口见 shell/SessionWorkspace.qml）──
    property bool componentsReady: false

    Shell.SessionWorkspace {
        id: sessionWorkspace
        anchors.fill: parent
        anchors.topMargin: (softGlBanner.visible ? softGlBanner.height : 0)
            + (videoIntegrityBanner.visible ? videoIntegrityBanner.height : 0)
        z: 10
        drivingShellActive: window.componentsReady
        appFontFamily: window.chineseFont || ""
        onOpenVehicleSelectionRequested: vehicleSelectionDialog.open()
    }

    /** 与 SessionConstants / StackLayout 对齐，供日志与外部脚本断言 */
    readonly property alias sessionStage: sessionWorkspace.sessionStage
    /** 驾驶界面 Loader，供 Connections 绑定 DrivingInterface 信号 */
    property alias drivingLoader: sessionWorkspace.drivingInterfaceLoader

    // ── 渲染刷新管理器 ─────────────────────────────────────────────────
    QtObject {
        id: renderRefreshManager

        property int refreshCount: 0
        property bool isRefreshing: false

        function refresh() {
            if (isRefreshing) return
            isRefreshing = true
            refreshCount++
            if (AppContext.forceRefreshAllRenderers("main.renderRefreshManager.refresh")) {
                console.log("[Client][UI][Render] refresh() success")
            }
            Qt.callLater(function() { isRefreshing = false })
        }
    }

    QtObject {
        id: dialogState
        property bool isOpen: false
    }

    Timer {
        id: dialogOpenRefreshTimer
        interval: 50
        repeat: false
        onTriggered: {
            console.log("[Client][UI] VehicleSelectionDialog opened (50ms后) → forceRefreshAllRenderers")
            AppContext.forceRefreshAllRenderers("main.dialogOpenRefreshTimer.50ms")
        }
    }

    Timer {
        id: dialogOpenPollingTimer
        interval: 500
        repeat: true
        running: dialogState && dialogState.isOpen
        onTriggered: {
            if (!dialogState || !dialogState.isOpen) return
            AppContext.forceRefreshAllRenderers("main.dialogOpenPollingTimer.500ms")
        }
    }

    // 车辆选择对话框
    VehicleSelectionDialog {
        id: vehicleSelectionDialog
        
        Component.onCompleted: {
            console.log("[Client][UI] VehicleSelectionDialog loaded")
        }
        
        onOpened: {
            dialogState.isOpen = true
            console.log("[Client][UI] VehicleSelectionDialog opened")
            AppContext.forceRefreshAllRenderers("main.VehicleSelectionDialog.onOpened")
            dialogOpenRefreshTimer.restart()
        }
        
        onClosed: {
            dialogState.isOpen = false
            console.log("[Client][UI] VehicleSelectionDialog closed")
            Qt.callLater(function() {
                if (AppContext.forceRefreshAllRenderers("main.VehicleSelectionDialog.onClosed")) {
                    console.log("[Client][UI] VehicleSelectionDialog closed → forceRefreshAllRenderers")
                }
            })
        }
    }

    // 连接对话框
    ConnectionsDialog {
        id: connectionsDialog
    }

    // ConnectionsDialog 内 connectToBroker 为异步：在 MQTT 真正 up 后再 requestStreamStart（替代原 Qt.callLater 竞态）
    Connections {
        target: AppContext.mqttController
        ignoreUnknownSignals: true
        function onMqttConnectResolved(succeeded, detail) {
            if (succeeded || !AppContext.pendingRequestStreamAfterMqttConnect)
                return
            AppContext.pendingRequestStreamAfterMqttConnect = false
            console.warn("[Client][StreamE2E][MAIN] pendingRequestStream cleared (MQTT failed): " + detail)
        }
        function onMqttBrokerConnectionChanged(connected) {
            if (!connected || !AppContext.pendingRequestStreamAfterMqttConnect)
                return
            AppContext.pendingRequestStreamAfterMqttConnect = false
            var mc = AppContext.mqttController
            if (mc && typeof mc.requestStreamStart === "function") {
                mc.requestStreamStart()
                console.warn("[Client][StreamE2E][MAIN] start_stream after dialog MQTT connected (pending flag cleared)")
            }
        }
    }

    // ── 测试模式自动连接 ───────────────────────────────────────────────
    Timer {
        id: testModeEnterDrivingTimer
        interval: 2000
        repeat: false
        onTriggered: {
            if (typeof autoConnectVideo === "undefined" || !autoConnectVideo) return
            var vm = AppContext.vehicleManager
            if (!vm) return
            var testVin = "123456789"
            if (typeof autoConnectTestVin !== "undefined" && autoConnectTestVin
                    && String(autoConnectTestVin).length > 0)
                testVin = String(autoConnectTestVin)
            console.log("[Client][UI][TEST] autoConnectVideo: entering driving interface vin=" + testVin)
            vm.addTestVehicle(testVin, "测试车辆")
            vm.currentVin = testVin
            componentsReady = true
            updateTitle()
            autoConnectTriggerTimer.start()
        }
    }
    
    Timer {
        id: autoConnectTriggerTimer
        interval: 1500
        repeat: false
        onTriggered: {
            if (typeof autoConnectVideo === "undefined" || !autoConnectVideo) return
            var wsm = AppContext.webrtcStreamManager
            if (!wsm) return
            var mqtt = AppContext.mqttController
            var vm = AppContext.vehicleManager
            console.log("[Client][UI][TEST] autoConnectVideo: triggering connectFourStreams")
            if (mqtt && mqtt.mqttBrokerConnected) {
                mqtt.requestStreamStart()
            }
            var whep = vm ? vm.lastWhepUrl : ""
            var whepLen = whep ? String(whep).length : 0
            console.warn("[Client][StreamE2E][QML_TEST_AUTOCONNECT] path=main.autoConnectTriggerTimer whepLen=" + whepLen
                        + " currentVin=" + (vm && vm.currentVin ? vm.currentVin : "")
                        + " ★ 测试模式：无 whep 时 C++ 用 ZLM_VIDEO_URL")
            wsm.connectFourStreams(whep || "")
        }
    }

    // ── 生命周期 ────────────────────────────────────────────────────────
    Component.onCompleted: {
        updateTitle()
        console.log("[Client][UI] Main window completed")
        console.log("[Client][UI] componentsReady:", componentsReady, " sessionStage:", sessionStage,
                    " useWindowFrame:", AppContext.useWindowFrame)
        console.log("[Client][WindowPolicy][QML] useWindowFrame=" + AppContext.useWindowFrame
                    + " reason=" + AppContext.windowFramePolicyReason
                    + " color=" + color + " visibility=" + visibility)
        
        if (typeof autoConnectVideo !== "undefined" && autoConnectVideo) {
            console.log("[Client][UI][TEST] CLIENT_AUTO_CONNECT_VIDEO=1：跳过登录页")
            componentsReady = true
            updateTitle()
            testModeEnterDrivingTimer.start()
        }
        
        var am = AppContext.authManager
        var vm = AppContext.vehicleManager
        if (am) {
            console.log("[Client][UI] authManager exists, isLoggedIn:", am.isLoggedIn)
            if (am.isLoggedIn) {
                if (vm) {
                    console.log("[Client][UI] vehicleManager.currentVin:", vm.currentVin)
                    if (vm.currentVin && vm.currentVin.length > 0) {
                        console.log("[Client][UI] Already logged in and vehicle selected")
                        componentsReady = true
                    } else {
                        console.log("[Client][UI] Already logged in but no vehicle selected")
                        Qt.callLater(function() {
                            vehicleSelectionDialog.open()
                        })
                    }
                }
            } else {
                console.log("[Client][UI] Not logged in, LoginPage should be visible")
            }
        } else {
            console.warn("[Client][UI] authManager not available")
        }

        // 布局稳定后再打尺寸链：区分「QML 客户区为 0」与「尺寸正常但仍透出宿桌面(X11/GL)」
        Qt.callLater(function () {
            var ci = window.contentItem
            var ciW = ci ? ci.width : -1
            var ciH = ci ? ci.height : -1
            console.log("[Client][UI][PresentProbe] postLayout"
                        + " win=" + Math.round(window.width) + "x" + Math.round(window.height)
                        + " sessionWorkspace=" + Math.round(sessionWorkspace.width) + "x"
                        + Math.round(sessionWorkspace.height)
                        + " contentItem=" + Math.round(ciW) + "x" + Math.round(ciH)
                        + " sessionStage=" + sessionStage
                        + " useWindowFrame=" + AppContext.useWindowFrame
                        + " | 若 sessionWorkspace≈0 → 布局/Stack 未铺开"
                        + "；若尺寸正常仍见宿桌面 → 查 X11 合成/混成器、CLIENT_QPA_XCB_DEBUG、sceneGraphError")
        })
    }
    
    // ── 登录状态监听 ────────────────────────────────────────────────────
    Connections {
        target: AppContext.authManager
        ignoreUnknownSignals: true
        function onLoginSucceeded(token, userInfo) {
            console.log("[Client][UI] onLoginSucceeded username=" + (userInfo && userInfo.username ? userInfo.username : ""))
            console.warn("[Client][StreamE2E][QML_LOGIN_UI] onLoginSucceeded → 将打开选车；拉流在选车/会话/连接按钮后")
            openVehicleSelectionTimer.start()
        }
        function onLoginFailed(error) {
            console.log("[Client][UI] onLoginFailed error=" + (error || ""))
        }
    }
    
    Timer {
        id: openVehicleSelectionTimer
        interval: 500
        onTriggered: {
            var am = AppContext.authManager
            if (am && am.isLoggedIn) {
                console.log("[Client][UI] 打开车辆选择对话框")
                vehicleSelectionDialog.open()
            } else {
                console.log("[Client][UI] 未打开对话框: 未登录")
            }
        }
    }
    
    Connections {
        target: vehicleSelectionDialog
        ignoreUnknownSignals: true
        function onClosed() {
            var vm = AppContext.vehicleManager
            if (vm && vm.currentVin && vm.currentVin.length > 0) {
                componentsReady = true
                updateTitle()
                Qt.callLater(function() {
                    AppContext.forceRefreshAllRenderers("main.vehicleSelectionDialog.onClosed.selectedVin")
                    console.log("[Client][UI] Vehicle selected → componentsReady=true")
                })
            }
        }
    }

    Connections {
        target: drivingLoader.item
        ignoreUnknownSignals: true
        enabled: drivingLoader.item !== null
        function onOpenMqttDialogRequested() {
            var mqtt = AppContext.mqttController
            connectionsDialog.mqttBroker = mqtt ? mqtt.brokerUrl : "mqtt://localhost:1883"
            connectionsDialog.open()
        }
    }

    Connections {
        target: AppContext.authManager
        ignoreUnknownSignals: true
        function onLoginStatusChanged(loggedIn) {
            if (!loggedIn) {
                console.log("[Client][UI] 登录状态变为未登录，返回登录页")
                componentsReady = false
                vehicleSelectionDialog.close()
            }
        }
    }

    Connections {
        target: AppContext.vehicleManager
        ignoreUnknownSignals: true
        function onSessionCreated(sessionVin, sessionId, whipUrl, whepUrl, controlConfig) {
            // ★ 核心修复：会话创建后，确保 MQTT 连通后自动发送 start_stream
            // 解决 SessionManager 自动触发 MQTT 连接但未设置 QML 待处理标志导致的「有视频流拉取无推流指令」问题
            if (AppContext.isMqttConnected) {
                var mc = AppContext.mqttController
                if (mc && typeof mc.requestStreamStart === "function") {
                    mc.requestStreamStart()
                    console.warn("[Client][StreamE2E][MAIN] start_stream sent immediately (sessionCreated, MQTT already up)")
                }
            } else {
                AppContext.pendingRequestStreamAfterMqttConnect = true
                console.warn("[Client][StreamE2E][MAIN] pendingRequestStream set (sessionCreated) vin=" + sessionVin)
            }
        }
    }

    // ── 全局“防呆”覆盖层 (SafetyShield) ────────────────────────────────────
    Rectangle {
        id: safetyShield
        anchors.fill: parent
        z: 2000000 // 最高层级
        color: Qt.rgba(0.2, 0, 0, 0.85) // 深红半透明背景
        visible: AppContext.safetyMonitor ? (AppContext.safetyMonitor.emergencyActive || (AppContext.hasVehicle && !AppContext.safetyMonitor.allSystemsGo)) : false
        
        // 拦截所有鼠标点击和按键输入
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            onPressed: (mouse) => mouse.accepted = true
            onReleased: (mouse) => mouse.accepted = true
            onDoubleClicked: (mouse) => mouse.accepted = true
            onWheel: (wheel) => wheel.accepted = true
        }
        
        // 拦截键盘输入：将焦点强行锁定在此组件
        focus: visible
        Keys.onPressed: event.accepted = true
        Keys.onReleased: event.accepted = true

        Column {
            anchors.centerIn: parent
            spacing: 30
            
            // 警示图标
            Rectangle {
                width: 100; height: 100
                radius: 50
                color: "red"
                anchors.horizontalCenter: parent.horizontalCenter
                
                Text {
                    anchors.centerIn: parent
                    text: "!"
                    font.pixelSize: 64
                    font.bold: true
                    color: "white"
                }
                
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.3; duration: 500 }
                    NumberAnimation { from: 0.3; to: 1.0; duration: 500 }
                }
            }
            
            Text {
                text: AppContext.safetyMonitor && AppContext.safetyMonitor.emergencyActive ? "急停已触发" : "链路不完整 / 数据失效"
                font.pixelSize: 48
                font.bold: true
                color: "white"
                anchors.horizontalCenter: parent.horizontalCenter
                font.family: window.chineseFont || ""
            }
            
            Text {
                text: AppContext.safetyMonitor ? (AppContext.safetyMonitor.emergencyActive ? AppContext.safetyMonitor.emergencyReason : "MQTT/WebRTC 连接异常，已自动切断控制链路") : "未知原因"
                font.pixelSize: 24
                color: "#FFCDD2"
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                width: parent.parent.width * 0.8
                font.family: window.chineseFont || ""
            }
            
            Button {
                text: "重置会话"
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: {
                    console.log("[Client][UI][Safety] User manually resetting session from SafetyShield")
                    // 重置 UI 状态，驱动 sessionStage 切回选车页
                    window.componentsReady = false
                    window.updateTitle()

                    // 清理底层连接与会话状态
                    if (AppContext.teleopSession && typeof AppContext.teleopSession.stop === "function") {
                        AppContext.teleopSession.stop()
                    } else {
                        // 回退方案：直接操作各管理器
                        if (AppContext.webrtcStreamManager) AppContext.webrtcStreamManager.disconnectAll()
                        if (AppContext.mqttController) AppContext.mqttController.requestStreamStop()
                        if (AppContext.vehicleManager) AppContext.vehicleManager.currentVin = ""
                    }
                }
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 18
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: window.chineseFont || ""
                }
                background: Rectangle {
                    implicitWidth: 160
                    implicitHeight: 50
                    color: parent.pressed ? "#C62828" : "#E53935"
                    border.color: "white"
                    border.width: 1
                    radius: 4
                }
            }
        }
        
        // 闪烁的全屏红框
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: "red"
            border.width: 10
            
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.0; duration: 800 }
                NumberAnimation { from: 0.0; to: 1.0; duration: 800 }
            }
        }
    }

    // ── UI 线程性能压力指标 (Liveness Feedback) ───────────────────────────
    Rectangle {
        id: uiPerformanceOverlay
        width: 180
        height: 30
        color: Qt.rgba(0, 0, 0, 0.6)
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 20
        anchors.topMargin: 20
        z: 3000000
        visible: uiStallWarning.visible

        Row {
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                width: 12; height: 12
                radius: 6
                color: "red"
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.2; duration: 300 }
                    NumberAnimation { from: 0.2; to: 1.0; duration: 300 }
                }
            }
            Text {
                id: uiStallWarning
                text: "UI 线程性能告警"
                color: "white"
                font.pixelSize: 14
                font.bold: true
                font.family: window.chineseFont || ""
                
                property double lastHeartbeat: Date.now()

                Timer {
                    interval: 100
                    repeat: true
                    running: true
                    onTriggered: {
                        var now = Date.now();
                        var diff = now - uiStallWarning.lastHeartbeat;
                        if (diff > 500) { // 提高阈值至 500ms，减少非核心干扰；报警告，不影响继续操作
                            uiStallWarning.visible = true;
                            console.warn("[Client][UI] UI thread stall detected in QML: " + diff + "ms");
                        } else {
                            uiStallWarning.visible = false;
                        }
                    }
                }
                
                // 每 50ms 更新一下 lastHeartbeat
                Timer {
                    interval: 50
                    repeat: true
                    running: true
                    onTriggered: {
                        uiStallWarning.lastHeartbeat = Date.now();
                    }
                }
            }
        }
    }

}
