import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import RemoteDriving 1.0

ApplicationWindow {
    id: window
    // 自适应窗口大小：使用屏幕尺寸的 90%，最小 1280x720，最大 1920x1080
    width: Math.min(Math.max(1280, Screen.width * 0.9), 1920)
    height: Math.min(Math.max(720, Screen.height * 0.9), 1080)
    minimumWidth: 1280
    minimumHeight: 720
    visible: true
    flags: Qt.FramelessWindowHint  // 移除窗口标题栏（最小化/最大化/关闭按钮所在的白条）
    title: windowTitleText
    color: "#1E1E2E"  //  fallback 背景，避免 Docker/无 GPU 下黑屏
    x: (Screen.width - width) / 2  // 居中显示
    y: (Screen.height - height) / 2
    
    // 现代化配色方案
    readonly property color primaryColor: "#4A90E2"      // 主色调：蓝色
    readonly property color secondaryColor: "#50C878"    // 辅助色：绿色
    readonly property color accentColor: "#FF6B6B"       // 强调色：红色
    readonly property color bgDark: "#1E1E2E"           // 深色背景
    readonly property color bgMedium: "#2A2A3E"          // 中等背景
    readonly property color bgLight: "#3A3A4E"          // 浅色背景
    readonly property color textPrimary: "#FFFFFF"      // 主文本色
    readonly property color textSecondary: "#B0B0B0"   // 次要文本色
    
    // 背景渐变
    Rectangle {
        anchors.fill: parent
        z: -1
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1E1E2E" }
            GradientStop { position: 1.0; color: "#0F0F1A" }
        }
    }
    
    // 延迟计算标题，避免在 ApplicationWindow 创建时立即访问 vehicleManager
    property string windowTitleText: "远程驾驶客户端"
    
    function updateTitle() {
        if (typeof vehicleManager !== "undefined" && vehicleManager) {
            windowTitleText = "远程驾驶客户端 - " + (vehicleManager.currentVehicleName || "未选择车辆")
        } else {
            windowTitleText = "远程驾驶客户端"
        }
    }
    
    // 监听车辆变化，更新标题
    Connections {
        target: vehicleManager
        ignoreUnknownSignals: true  // 如果 vehicleManager 未定义，忽略信号
        function onCurrentVehicleChanged() {
            if (typeof vehicleManager !== "undefined" && vehicleManager) {
                window.updateTitle()
            }
        }
    }
    
    // 设置默认字体（支持中文）
    property string chineseFont: {
        var fonts = ["WenQuanYi Zen Hei", "WenQuanYi Micro Hei", "Noto Sans CJK SC", 
                     "Noto Sans CJK TC", "Source Han Sans SC", "Droid Sans Fallback",
                     "SimHei", "Microsoft YaHei"]
        var availableFonts = Qt.fontFamilies()
        for (var i = 0; i < fonts.length; i++) {
            if (availableFonts.indexOf(fonts[i]) !== -1) {
                console.log("Using Chinese font:", fonts[i])
                return fonts[i]
            }
        }
        console.warn("No Chinese font found, Chinese text may not display correctly")
        console.log("Available fonts:", availableFonts)
        return ""  // 使用系统默认字体
    }
    
    // 全局字体设置（ApplicationWindow 的 font 属性会影响所有子组件）
    font.family: chineseFont || "DejaVu Sans"
    font.pixelSize: 12


    // 主布局：仅在选择车辆并确认后才显示（流程：登录 → 选择车辆 → 远程驾驶主页面）
    property bool componentsReady: false
    
    // 登录页面（嵌入主窗口）
    LoginPage {
        id: loginPage
        anchors.fill: parent
        visible: {
            // 如果主界面已就绪，不显示登录页面
            if (componentsReady) return false
            // 如果未登录，显示登录页面
            if (typeof authManager === "undefined" || !authManager || !authManager.isLoggedIn) {
                return true
            }
            // 如果已登录但未选择车辆，也不显示登录页面（会显示车辆选择对话框）
            return false
        }
        z: 1000  // 提高 z-index，确保在最上层
        
        Component.onCompleted: {
            console.log("LoginPage loaded, visible:", loginPage.visible)
            console.log("componentsReady:", componentsReady)
            if (typeof authManager !== "undefined" && authManager) {
                console.log("authManager.isLoggedIn:", authManager.isLoggedIn)
            }
        }
    }
    
    // 主驾驶界面（登录并选择车辆后显示）
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 60  // 为状态栏留出空间
        opacity: componentsReady ? 1.0 : 0.0
        visible: componentsReady
        Behavior on opacity { NumberAnimation { duration: 300 } }
        spacing: 0
        
        // 新的驾驶界面（包含前进模式、倒车模式、多视频视图等）
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

    // 车辆选择对话框（保持弹窗形式）
    VehicleSelectionDialog {
        id: vehicleSelectionDialog
        
        Component.onCompleted: {
            console.log("VehicleSelectionDialog loaded")
        }
        
        onOpened: {
            console.log("VehicleSelectionDialog opened")
        }
        
        onClosed: {
            console.log("VehicleSelectionDialog closed")
        }
    }

    // 连接对话框
    ConnectionsDialog {
        id: connectionsDialog
    }


    // 自动化测试模式：仅当 CLIENT_AUTO_CONNECT_VIDEO=1（如 scripts/verify-connect-feature.sh）时跳过登录，直接进入主界面并触发拉流；正常运行请勿设置该变量
    Timer {
        id: testModeEnterDrivingTimer
        interval: 2000
        repeat: false
        onTriggered: {
            if (typeof autoConnectVideo === "undefined" || !autoConnectVideo) return
            if (typeof vehicleManager === "undefined" || !vehicleManager) return
            console.log(" [TEST] autoConnectVideo: entering driving interface and triggering connect")
            vehicleManager.addTestVehicle("123456789", "测试车辆")
            vehicleManager.currentVin = "123456789"
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
            if (typeof webrtcStreamManager === "undefined" || !webrtcStreamManager) return
            console.log(" [TEST] autoConnectVideo: triggering connectFourStreams" + (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected ? " and requestStreamStart" : " (MQTT not connected, skip requestStreamStart)"))
            if (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected)
                mqttController.requestStreamStart()
            var whep = (typeof vehicleManager !== "undefined" && vehicleManager) ? vehicleManager.lastWhepUrl : ""
            webrtcStreamManager.connectFourStreams(whep || "")
        }
    }

    // 窗口就绪
    Component.onCompleted: {
        updateTitle()
        console.log("Main window completed")
        console.log("componentsReady:", componentsReady)
        console.log("LoginPage visible:", loginPage.visible)
        if (typeof autoConnectVideo !== "undefined" && autoConnectVideo) {
            console.log("[Client][UI][TEST] CLIENT_AUTO_CONNECT_VIDEO=1：跳过登录页，立即进入驾驶壳层；约 2s 后注入测试车辆并触发 connectFourStreams")
            componentsReady = true
            updateTitle()
            testModeEnterDrivingTimer.start()
        }
        if (typeof authManager !== "undefined" && authManager) {
            console.log("authManager exists, isLoggedIn:", authManager.isLoggedIn)
            console.log("authManager.username:", authManager.username)
            
            // 如果启动时已登录，检查是否已选择车辆
            if (authManager.isLoggedIn) {
                if (typeof vehicleManager !== "undefined" && vehicleManager) {
                    console.log("vehicleManager.currentVin:", vehicleManager.currentVin)
                    console.log("vehicleManager.vehicleList:", vehicleManager.vehicleList)
                    
                    if (vehicleManager.currentVin.length > 0) {
                        console.log("Already logged in and vehicle selected, showing main interface")
                        componentsReady = true
                    } else {
                        console.log("Already logged in but no vehicle selected, opening vehicle selection dialog")
                        // 延迟打开车辆选择对话框，确保 UI 已完全加载
                        Qt.callLater(function() {
                            console.log("Calling vehicleSelectionDialog.open()")
                            vehicleSelectionDialog.open()
                        })
                    }
                } else {
                    console.log("vehicleManager not available")
                }
            } else {
                console.log("Not logged in, LoginPage should be visible")
            }
        } else {
            console.log("authManager not available")
        }
    }
    
    // 登录成功后，打开车辆选择对话框
    Connections {
        target: authManager
        ignoreUnknownSignals: true
        function onLoginSucceeded(token, userInfo) {
            console.log("[Client][UI] onLoginSucceeded tokenLen=" + (token ? token.length : 0) + " username=" + (userInfo && userInfo.username ? userInfo.username : ""))
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
            console.log("[Client][UI] openVehicleSelectionTimer: isLoggedIn=" + (typeof authManager !== "undefined" && authManager ? authManager.isLoggedIn : false))
            if (typeof authManager !== "undefined" && authManager && authManager.isLoggedIn) {
                console.log("[Client][UI] 打开车辆选择对话框 vehicleSelectionDialog.open()")
                vehicleSelectionDialog.open()
            } else {
                console.log("[Client][UI] 未打开对话框: authManager 不可用或未登录")
            }
        }
    }
    
    // 车辆选择对话框关闭且已选车辆时，进入远程驾驶主页面（连接车端由主界面「连接车端」按钮触发，使用 VIN 会话配置）
    Connections {
        target: vehicleSelectionDialog
        ignoreUnknownSignals: true
        function onClosed() {
            if (typeof vehicleManager !== "undefined" && vehicleManager && vehicleManager.currentVin.length > 0) {
                componentsReady = true
                updateTitle()
            }
        }
    }
    // 仅当无 VIN 绑定 MQTT 配置时，主界面「连接车端」会请求打开连接设置
    Connections {
        target: drivingInterface
        ignoreUnknownSignals: true
        function onOpenMqttDialogRequested() {
            connectionsDialog.mqttBroker = (typeof mqttController !== "undefined" && mqttController) ? mqttController.brokerUrl : "mqtt://localhost:1883"
            connectionsDialog.open()
        }
    }

    Connections {
        target: authManager
        ignoreUnknownSignals: true
        function onLoginStatusChanged(loggedIn) {
            if (!loggedIn) {
                console.log("[Client][UI] 登录状态变为未登录，返回登录页并关闭车辆选择弹窗")
                componentsReady = false
                vehicleSelectionDialog.close()
            }
        }
    }
}
