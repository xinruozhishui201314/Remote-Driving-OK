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
}
