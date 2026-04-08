import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import RemoteDriving 1.0
import "styles" as ThemeModule

ApplicationWindow {
    id: window
    // 自适应窗口大小
    width: Math.min(Math.max(1280, Screen.width * 0.9), 1920)
    height: Math.min(Math.max(720, Screen.height * 0.9), 1080)
    minimumWidth: 1280
    minimumHeight: 720
    visible: true
    flags: Qt.FramelessWindowHint
    title: windowTitleText
    color: ThemeModule.Theme.colorBackground
    x: (Screen.width - width) / 2
    y: (Screen.height - height) / 2
    
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

    // ── 主布局 ────────────────────────────────────────────────────────
    property bool componentsReady: false
    
    // 登录页面
    LoginPage {
        id: loginPage
        anchors.fill: parent
        visible: {
            if (componentsReady) return false
            var am = AppContext.authManager
            if (!am || !am.isLoggedIn) {
                return true
            }
            return false
        }
        z: 1000
        
        Component.onCompleted: {
            console.log("[Client][UI] LoginPage loaded, visible:", loginPage.visible)
            console.log("[Client][UI] componentsReady:", componentsReady)
        }
    }
    
    // 主驾驶界面
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 60
        opacity: componentsReady ? 1.0 : 0.0
        visible: componentsReady
        Behavior on opacity { NumberAnimation { duration: 300 } }
        spacing: 0
        
        DrivingInterface {
            id: drivingInterface
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    // 顶部状态栏
    StatusBar {
        id: statusBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 60
        z: 1000
        opacity: componentsReady ? 1.0 : 0.0
        visible: componentsReady
        Behavior on opacity { NumberAnimation { duration: 300 } }
    }

    // ── 渲染刷新管理器 ─────────────────────────────────────────────────
    QtObject {
        id: renderRefreshManager

        property int refreshCount: 0
        property bool isRefreshing: false

        function refresh() {
            if (isRefreshing) return
            isRefreshing = true
            refreshCount++
            if (AppContext.forceRefreshAllRenderers()) {
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
            AppContext.forceRefreshAllRenderers()
        }
    }

    Timer {
        id: dialogOpenPollingTimer
        interval: 500
        repeat: true
        running: dialogState && dialogState.isOpen
        onTriggered: {
            if (!dialogState || !dialogState.isOpen) return
            AppContext.forceRefreshAllRenderers()
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
            AppContext.forceRefreshAllRenderers()
            dialogOpenRefreshTimer.restart()
        }
        
        onClosed: {
            dialogState.isOpen = false
            console.log("[Client][UI] VehicleSelectionDialog closed")
            Qt.callLater(function() {
                if (AppContext.forceRefreshAllRenderers()) {
                    console.log("[Client][UI] VehicleSelectionDialog closed → forceRefreshAllRenderers")
                }
            })
        }
    }

    // 连接对话框
    ConnectionsDialog {
        id: connectionsDialog
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
            console.log("[Client][UI][TEST] autoConnectVideo: entering driving interface")
            vm.addTestVehicle("123456789", "测试车辆")
            vm.currentVin = "123456789"
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
            if (mqtt && mqtt.isConnected) {
                mqtt.requestStreamStart()
            }
            var whep = vm ? vm.lastWhepUrl : ""
            wsm.connectFourStreams(whep || "")
        }
    }

    // ── 生命周期 ────────────────────────────────────────────────────────
    Component.onCompleted: {
        updateTitle()
        console.log("[Client][UI] Main window completed")
        console.log("[Client][UI] componentsReady:", componentsReady)
        console.log("[Client][UI] LoginPage visible:", loginPage.visible)
        
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
    }
    
    // ── 登录状态监听 ────────────────────────────────────────────────────
    Connections {
        target: AppContext.authManager
        ignoreUnknownSignals: true
        function onLoginSucceeded(token, userInfo) {
            console.log("[Client][UI] onLoginSucceeded username=" + (userInfo && userInfo.username ? userInfo.username : ""))
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
                    AppContext.forceRefreshAllRenderers()
                    console.log("[Client][UI] Vehicle selected → componentsReady=true")
                })
            }
        }
    }

    Connections {
        target: drivingInterface
        ignoreUnknownSignals: true
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
