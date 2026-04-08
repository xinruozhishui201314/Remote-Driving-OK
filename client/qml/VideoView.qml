import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import RemoteDriving 1.0
import "styles" as ThemeModule

/**
 * 视频显示组件
 * 
 * 功能：
 * - 视频状态管理（空闲、连接中、已连接、断开、重连中、错误）
 * - 错误处理与报告
 * - 自动重连机制
 * - 使用 AppContext 和 Theme 统一上下文
 */
Rectangle {
    id: videoContainer

    // ── 视频状态枚举 ──────────────────────────────────────────────────
    readonly property int StateIdle: 0           // 空闲
    readonly property int StateConnecting: 1     // 连接中
    readonly property int StateConnected: 2     // 已连接
    readonly property int StateDisconnected: 3 // 断开
    readonly property int StateReconnecting: 4  // 重连中
    readonly property int StateError: 5         // 错误

    // ── 状态属性 ──────────────────────────────────────────────────────
    property int videoState: StateIdle
    property string errorMessage: ""
    property int errorCode: 0
    property bool autoReconnect: true
    property int maxReconnectAttempts: 3
    property int reconnectAttempts: 0
    property int reconnectInterval: 3000
    property bool isReconnecting: false

    // ── 只读便捷属性 ──────────────────────────────────────────────────
    readonly property bool isConnected: videoState === StateConnected
    readonly property bool isIdle: videoState === StateIdle
    readonly property bool hasError: videoState === StateError
    readonly property bool canReconnect: autoReconnect && reconnectAttempts < maxReconnectAttempts

    // ── 统一上下文 ─────────────────────────────────────────────────────
    readonly property var webrtcStreamManager: AppContext.webrtcStreamManager
    readonly property var webrtcClient: AppContext.webrtcClient
    readonly property var vehicleManager: AppContext.vehicleManager
    readonly property string chineseFont: AppContext.chineseFont

    // ── 错误状态对象 ──────────────────────────────────────────────────
    QtObject {
        id: videoErrorState
        
        function setError(code, message) {
            errorCode = code
            errorMessage = message
            videoState = StateError
            console.warn("[VideoView][Error] code=" + code + " message=" + message)
        }
        
        function clearError() {
            errorCode = 0
            errorMessage = ""
        }
    }

    // ── 状态检查 ──────────────────────────────────────────────────────
    function videoStreamsConnected() {
        var wsm = videoContainer.webrtcStreamManager
        if (wsm && wsm.anyConnected) {
            return true
        }
        var wc = videoContainer.webrtcClient
        if (wc && wc.isConnected) {
            return true
        }
        return false
    }

    function getStateText() {
        switch (videoState) {
        case StateIdle: return "等待视频流连接..."
        case StateConnecting: return "正在连接视频流..."
        case StateConnected: return videoStreamsConnected() ? "视频流已连接" : "视频流已断开"
        case StateDisconnected: return "视频流已断开"
        case StateReconnecting: return "正在重连视频流..."
        case StateError: return errorMessage || "视频流错误"
        default: return "未知状态"
        }
    }

    // ── 错误处理 ──────────────────────────────────────────────────────
    function handleVideoError(code, message) {
        videoErrorState.setError(code, message)
        if (canReconnect && !isReconnecting) {
            console.log("[VideoView] Attempting auto reconnect, attempt=" + (reconnectAttempts + 1))
            scheduleReconnect()
        }
    }

    function clearVideoError() {
        videoErrorState.clearError()
        if (videoState === StateError) {
            videoState = StateDisconnected
        }
    }

    // ── 重连机制 ──────────────────────────────────────────────────────
    function scheduleReconnect() {
        if (isReconnecting || reconnectAttempts >= maxReconnectAttempts) {
            console.log("[VideoView] Cannot reconnect: isReconnecting=" + isReconnecting 
                       + " attempts=" + reconnectAttempts + " max=" + maxReconnectAttempts)
            return
        }
        isReconnecting = true
        reconnectAttempts++
        videoState = StateReconnecting
        reconnectTimer.interval = reconnectInterval
        reconnectTimer.restart()
        console.log("[VideoView] Reconnect scheduled in " + reconnectInterval + "ms")
    }

    function performReconnect() {
        isReconnecting = false
        var wsm = videoContainer.webrtcStreamManager
        var vm = videoContainer.vehicleManager
        if (wsm && vm) {
            var whep = vm.lastWhepUrl || ""
            console.log("[VideoView] Performing reconnect to:", whep)
            wsm.connectFourStreams(whep)
        } else {
            console.error("[VideoView] Cannot reconnect: wsm=" + (wsm ? "ok" : "null") 
                         + " vm=" + (vm ? "ok" : "null"))
            videoErrorState.setError(2001, "重连失败：管理器不可用")
        }
    }

    function cancelReconnect() {
        reconnectTimer.stop()
        isReconnecting = false
        console.log("[VideoView] Reconnect cancelled")
    }

    function resetVideoState() {
        videoState = StateIdle
        reconnectAttempts = 0
        isReconnecting = false
        videoErrorState.clearError()
        cancelReconnect()
        console.log("[VideoView] State reset")
    }

    // ── 样式 ────────────────────────────────────────────────────────────
    gradient: Gradient {
        GradientStop { position: 0.0; color: ThemeModule.Theme.colorBackground }
        GradientStop { position: 1.0; color: ThemeModule.Theme.colorSurface }
    }

    // 视频显示区域
    Item {
        id: videoArea
        anchors.fill: parent
        anchors.margins: 20

        // 占位符
        Rectangle {
            id: videoPlaceholder
            anchors.fill: parent
            radius: 12
            gradient: Gradient {
                GradientStop { position: 0.0; color: ThemeModule.Theme.colorSurface }
                GradientStop { position: 1.0; color: ThemeModule.Theme.colorBackground }
            }
            border.color: videoContainer.hasError ? ThemeModule.Theme.colorDanger : ThemeModule.Theme.colorBorderActive
            border.width: 2
            
            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                z: -1
                radius: parent.radius + 3
                color: "#20000000"
            }
            
            Column {
                anchors.centerIn: parent
                spacing: 20
                
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 120
                    height: 120
                    radius: 60
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: ThemeModule.Theme.colorButtonBgHover }
                        GradientStop { position: 1.0; color: ThemeModule.Theme.colorButtonBg }
                    }
                    border.color: videoContainer.hasError ? ThemeModule.Theme.colorDanger : ThemeModule.Theme.colorBorderActive
                    border.width: 2
                    
                    Text {
                        anchors.centerIn: parent
                        text: videoContainer.hasError ? "⚠" : "📹"
                        font.pixelSize: 60
                    }
                }
                
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: videoContainer.getStateText()
                    color: {
                        if (videoContainer.hasError) return ThemeModule.Theme.colorDanger
                        if (videoContainer.isConnected) return ThemeModule.Theme.colorGood
                        return ThemeModule.Theme.colorTextDim
                    }
                    font.pixelSize: 24
                    font.family: videoContainer.chineseFont || font.family
                    font.bold: true
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: {
                        if (videoContainer.hasError) return "点击重连"
                        if (videoContainer.isReconnecting) return "重连中..."
                        if (videoContainer.isConnected) return "视频流正常"
                        return "等待视频流连接..."
                    }
                    color: ThemeModule.Theme.colorTextDim
                    font.pixelSize: 14
                    font.family: videoContainer.chineseFont || font.family
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: videoContainer.hasError || videoContainer.videoState === videoContainer.StateDisconnected
                    text: "重新连接"
                    enabled: !videoContainer.isReconnecting
                    onClicked: {
                        videoContainer.clearVideoError()
                        videoContainer.reconnectAttempts = 0
                        videoContainer.scheduleReconnect()
                    }
                    background: Rectangle {
                        radius: 8
                        color: parent.enabled ? (parent.pressed ? ThemeModule.Theme.colorPrimary : (parent.hovered ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorBorderActive)) : ThemeModule.Theme.colorBorder
                    }
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 14
                        font.family: videoContainer.chineseFont || font.family
                        color: parent.enabled ? ThemeModule.Theme.colorText : "#666666"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // 视频信息覆盖层
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 20
            width: infoColumn.implicitWidth + 24
            height: infoColumn.implicitHeight + 16
            radius: 8
            color: "#80000000"
            border.color: ThemeModule.Theme.colorBorderActive
            border.width: 1
            visible: videoContainer.isConnected
            
            Column {
                id: infoColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                
                Text {
                    text: "📹 视频流信息"
                    color: ThemeModule.Theme.colorBorderActive
                    font.pixelSize: 12
                    font.bold: true
                    font.family: videoContainer.chineseFont || font.family
                }
                
                Text {
                    text: "状态: " + (videoContainer.webrtcClient ? videoContainer.webrtcClient.statusText : "未连接")
                    color: ThemeModule.Theme.colorGood
                    font.pixelSize: 11
                    font.family: videoContainer.chineseFont || font.family
                }
            }
        }
    }

    // 全屏按钮
    Button {
        id: fullscreenButton
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 20
        width: 48
        height: 48
        text: window.visibility === Window.FullScreen ? "⛶" : "⛶"
        
        contentItem: Text {
            text: parent.text
            font.pixelSize: 20
            color: parent.enabled ? ThemeModule.Theme.colorText : "#666666"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        
        background: Rectangle {
            radius: 8
            color: parent.enabled ? (parent.pressed ? ThemeModule.Theme.colorPrimary : (parent.hovered ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorBorderActive)) : ThemeModule.Theme.colorBorder
            
            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                z: -1
                radius: parent.radius + 2
                color: parent.parent.enabled ? "#30000000" : "#00000000"
            }
        }
        
        onClicked: {
            if (window.visibility === Window.FullScreen) {
                window.showNormal()
            } else {
                window.showFullScreen()
            }
        }
    }

    // 重连定时器
    Timer {
        id: reconnectTimer
        interval: 3000
        repeat: false
        onTriggered: {
            console.log("[VideoView] Reconnect timer triggered")
            videoContainer.performReconnect()
        }
    }

    // ── 信号 ────────────────────────────────────────────────────────────
    signal reconnectRequested()
    signal connectionError(int code, string message)

    Component.onCompleted: {
        console.log("[VideoView] Initialized, autoReconnect=" + autoReconnect 
                   + " maxReconnectAttempts=" + maxReconnectAttempts)
    }

    Component.onDestruction: {
        cancelReconnect()
    }
}
