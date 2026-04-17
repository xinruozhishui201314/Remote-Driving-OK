import "../.."
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
/**
 * 顶部 HUD：连接、远驾、网络、时间。
 * 通过 facade.teleop 读写会话/连接相关状态；主题色等仍用 facade.*。
 */
Rectangle {
    id: topBarRect
    required property Item facade
    // Layout.* 由 DrivingLayoutShell 在实例上设置，避免与外壳重复
    color: facade.colorPanel
    border.color: facade.colorBorder
    border.width: 1
    
    RowLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 10
        
        // 左侧状态图标
        Row {
            spacing: 6
            Repeater {
                model: [
                    { icon: "icon/light.svg", name: "近光", active: true },
                    { icon: "icon/highbeam.svg", name: "远光", active: true },
                    { icon: "icon/foglight.svg", name: "雾灯", active: false },
                    { icon: "icon/wiper.svg", name: "雨刷", active: false },
                    { icon: "icon/light.svg", name: "大灯", active: true },
                    { icon: "icon/warning.svg", name: "警示", active: true }
                ]
                Rectangle {
                    width: 26; height: 26; radius: 4
                    color: modelData.active ? facade.colorAccent : facade.colorBorder
                    border.color: modelData.active ? facade.colorAccent : facade.colorButtonBorder
                    border.width: 1
                    Image {
                        anchors.centerIn: parent
                        width: 16; height: 16
                        source: modelData.icon
                        fillMode: Image.PreserveAspectFit
                    }
                    ToolTip.visible: statusMa.containsMouse
                    ToolTip.text: modelData.name
                    ToolTip.delay: 200
                    MouseArea { id: statusMa; anchors.fill: parent; hoverEnabled: true }
                }
            }
        }
        
        // 连接按钮：使用当前 VIN 的会话配置连 MQTT 并触发车端推流 + 客户端拉流；点击即有反馈（拉流/连接中）
        Rectangle {
            Layout.preferredWidth: 100
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignVCenter
            radius: 6
            border.width: 2
            border.color: (facade.appServices.videoStreamsConnected) ? facade.colorAccent : (facade.appServices.mqttController && facade.appServices.mqttController.mqttBrokerConnected ? "#508050" : facade.colorButtonBorder)
            color: (facade.appServices.videoStreamsConnected) ? "#1A2A1A" : (facade.teleop.pendingConnectVideo ? "#2A3A2A" : (facade.appServices.mqttController && facade.appServices.mqttController.mqttBrokerConnected ? "#1A2A1A" : facade.colorButtonBg))
            Text {
                anchors.centerIn: parent
                text: {
                    if (facade.teleop.pendingConnectVideo) return "连接中..."
                    if (facade.appServices.videoStreamsConnected) return "已连接"
                    if (facade.teleop.streamStopped && facade.appServices.mqttController && facade.appServices.mqttController.mqttBrokerConnected) return "连接车辆"
                    if (facade.appServices.mqttController && facade.appServices.mqttController.mqttBrokerConnected) return "MQTT已连接"
                    return "连接车端"
                }
                color: (facade.appServices.videoStreamsConnected) ? facade.colorAccent : (facade.appServices.mqttController && facade.appServices.mqttController.mqttBrokerConnected ? "#70B070" : facade.colorTextPrimary)
                font.pixelSize: 14
                font.bold: true
                font.family: facade.chineseFont || font.family
            }
            MouseArea {
                id: connectBtnMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (facade.appServices.videoStreamsConnected) {
                        // 先发送停止推流指令给车端
                        if (facade.appServices.mqttController && facade.appServices.mqttController.mqttBrokerConnected) {
                            facade.appServices.mqttController.requestStreamStop()
                            console.log("[Client][UI][Stream] 已发送停止推流指令给车端")
                        }
                        // 然后断开视频流连接
                        if (facade.appServices.webrtcStreamManager) facade.appServices.webrtcStreamManager.disconnectAll()
                        // ★ 标记推流已停止，按钮文本将变为"连接车辆"
                        facade.teleop.streamStopped = true
                        console.log("[Client][UI][Stream] 推流已停止，按钮状态更新为 facade.teleop.streamStopped=true")
                        return
                    }
                    // ★ 重新连接时重置停止状态
                    facade.teleop.streamStopped = false
                    var currentVin = (facade.appServices.vehicleManager) ? facade.appServices.vehicleManager.currentVin : ""
                    console.log("[Client][UI][Connect] 点击连接 当前VIN=" + currentVin + " (仿真车选 carla-sim-001 连接 CARLA)")
                    var cfg = (facade.appServices.vehicleManager) ? facade.appServices.vehicleManager.lastControlConfig : ({})
                    var brokerUrl = (cfg && (cfg.mqtt_broker_url || cfg["mqtt_broker_url"])) ? (cfg.mqtt_broker_url || cfg["mqtt_broker_url"]) : ((facade.appServices.mqttController) ? facade.appServices.mqttController.brokerUrl : "")
                    var clientId = (cfg && (cfg.mqtt_client_id || cfg["mqtt_client_id"])) ? (cfg.mqtt_client_id || cfg["mqtt_client_id"]) : ""
                    if (clientId && facade.appServices.mqttController)
                        facade.appServices.mqttController.clientId = clientId
                    if (brokerUrl && facade.appServices.mqttController)
                        facade.appServices.mqttController.brokerUrl = brokerUrl
                    if (facade.appServices.mqttController && facade.appServices.mqttController.mqttBrokerConnected) {
                        var lw = (facade.appServices.vehicleManager && facade.appServices.vehicleManager.lastWhepUrl) ? facade.appServices.vehicleManager.lastWhepUrl : ""
                        console.warn("[Client][StreamE2E][QML_CONNECT_CLICK] mqttAlreadyConnected=1 currentVin=" + currentVin
                                    + " lastWhepUrl_len=" + lw.length
                                    + " ★ ZLM getMediaList 轮询就绪后 connectFourStreams；最长 45s 兜底")
                        console.log("[Client][UI][Connect] 环节: MQTT 已连接，发送 start_stream currentVin=" + currentVin + " (VIN 为空时客户端将报错且不发送)")
                        facade.appServices.mqttController.requestStreamStart()
                        var wsm = facade.appServices.webrtcStreamManager
                        if (wsm && wsm.scheduleConnectFourStreamsWhenZlmReady !== undefined) {
                            wsm.scheduleConnectFourStreamsWhenZlmReady(lw || "", 1000, 60000)
                            console.log("[Client][UI][Connect] 环节: 已启动 ZLM 四路就绪轮询 → connectFourStreams")
                        } else {
                            console.error("[Client][UI][Connect] webrtcStreamManager.scheduleConnectFourStreamsWhenZlmReady 不可用，回退 connectFourStreams")
                            if (wsm)
                                wsm.connectFourStreams(lw || "")
                        }
                        return
                    }
                    if (!brokerUrl || brokerUrl.length === 0) {
                        console.log("[Client][UI][Connect] ✗ brokerUrl 为空，请先在连接设置中填写 MQTT 地址")
                        facade.teleop.openMqttDialogRequested()
                        return
                    }
                    facade.teleop.pendingConnectVideo = true
                    console.log("[Client][UI][Connect] 正在连接 MQTT brokerUrl=" + brokerUrl + " currentVin=" + currentVin + " (选车后 VIN 应由 facade.appServices.vehicleManager 已设置)")
                    if (facade.appServices.mqttController)
                        facade.appServices.mqttController.connectToBroker()
                }
            }
            ToolTip.visible: connectBtnMa.containsMouse
            ToolTip.text: facade.teleop.pendingConnectVideo ? "正在连接 MQTT…" : ((facade.appServices.videoStreamsConnected) ? "点击断开视频流并停止车端推流" : ((facade.appServices.mqttController && facade.appServices.mqttController.mqttBrokerConnected) ? "MQTT 已连接：发 start_stream 后轮询 ZLM 就绪再拉流…" : "点击连接车端（连 MQTT 并拉取四路视频）"))
            ToolTip.delay: 300
        }
        
        // 远驾接管按钮：点击后发送远驾接管状态给车端，以便直接控制车辆
        // ★ 只有视频流已连接（"已连接"状态）时才能点击
        // ★ 按钮文本根据车端反馈的 remoteControlEnabled 状态显示
        Rectangle {
            id: remoteControlTakeoverRect
            Layout.preferredWidth: 100
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignVCenter
            radius: 6
            border.width: 2
            property bool remoteControlActive: false  // 本地状态（用于发送指令）
            property bool isVideoConnected: (facade.appServices.videoStreamsConnected)  // 视频流是否已连接
            property bool buttonEnabled: isVideoConnected  // 按钮是否可用（只有视频流连接时才能点击）
            property bool remoteControlConfirmed: (facade.appServices.vehicleStatus) ? facade.appServices.vehicleStatus.remoteControlEnabled : false  // 车端确认的远驾接管状态
            property bool hadVideoConnectedBefore: false  // ★ 仅当「曾连接过视频」再断开时才发 remote_control false，避免 connectFourStreams 时 disconnectAll 触发 flood
            
            border.color: {
                if (!buttonEnabled) return "#555555"  // 禁用时灰色边框
                return remoteControlConfirmed ? facade.colorAccent : facade.colorButtonBorder
            }
            color: {
                if (!buttonEnabled) return "#1A1A1A"  // 禁用时深灰色背景
                return remoteControlConfirmed ? "#1A2A1A" : facade.colorButtonBg
            }
            opacity: buttonEnabled ? 1.0 : 0.5  // 禁用时半透明
            
            Text {
                anchors.centerIn: parent
                // ★ 根据车端反馈状态显示文本
                text: {
                    if (!parent.buttonEnabled) return "远驾接管"
                    if (parent.remoteControlConfirmed) return "远驾已接管"
                    return "远驾接管"
                }
                color: {
                    if (!parent.buttonEnabled) return "#666666"  // 禁用时灰色文字
                    return parent.remoteControlConfirmed ? facade.colorAccent : facade.colorTextPrimary
                }
                font.pixelSize: 14
                font.bold: true
                font.family: facade.chineseFont || font.family
            }
            
            MouseArea {
                id: remoteControlBtnMa
                anchors.fill: parent
                hoverEnabled: true
                enabled: parent.buttonEnabled  // ★ 只有视频流连接时才能点击
                cursorShape: parent.buttonEnabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor  // 禁用时显示禁止光标
                onClicked: {
                    var mc = facade.appServices.mqttController
                    var ctrlOk = mc && mc.mqttBrokerConnected
                    if (!parent.buttonEnabled) {
                        console.log("[Client][UI][RemoteControl][Click] ✗ 未发送：视频流未连接")
                        return
                    }
                    if (!ctrlOk) {
                        console.warn("[Client][UI][RemoteControl][Click] ✗ 未发送：需 MQTT 已连接（远驾仅走 vehicle/control） mqtt="
                                     + (mc ? mc.mqttBrokerConnected : false))
                        return
                    }
                    console.log("[Client][UI][RemoteControl][Click] >>> 用户点击远驾接管 <<< mqttBrokerConnected=" + ctrlOk
                                + " buttonEnabled=" + parent.buttonEnabled + " remoteControlConfirmed=" + parent.remoteControlConfirmed)
                    var newState = !parent.remoteControlConfirmed
                    parent.remoteControlActive = newState
                    console.log("[Client][UI][RemoteControl][Click] 调用 requestRemoteControl(" + newState + ")")
                    mc.requestRemoteControl(newState)
                    var ssmRc = facade.appServices.systemStateMachine
                    if (ssmRc && typeof ssmRc.fireByName === "function") {
                        if (newState) ssmRc.fireByName("START_SESSION")
                        else ssmRc.fireByName("STOP_SESSION")
                    }
                    console.log("[Client][UI][RemoteControl] 发送 enable=" + newState + "（等待车端 status / MQTT 确认）")
                    
                    // ★ 核心修复：点击按钮后强制让 DrivingInterface 重新获得焦点，解决键盘控车因焦点丢失失效的问题
                    if (facade) {
                        facade.forceActiveFocus()
                        console.log("[Client][UI][RemoteControl] 已强制 DrivingInterface 获得 activeFocus")
                    }
                }
            }
            
            // ★ 监听车端远驾接管状态变化
            Connections {
                target: facade.appServices.vehicleStatus
                ignoreUnknownSignals: true
                function onRemoteControlEnabledChanged() {
                    var confirmed = (facade.appServices.vehicleStatus) ? facade.appServices.vehicleStatus.remoteControlEnabled : false
                    console.log("[Client][UI][RemoteControl] 车端远驾接管状态变化 ==========")
                    console.log("[Client][UI][RemoteControl] 状态确认: " + (confirmed ? "已启用" : "已禁用"))
                    console.log("[Client][UI][RemoteControl] 当前 remoteControlConfirmed: " + parent.remoteControlConfirmed)
                    console.log("[Client][UI][RemoteControl] 当前 remoteControlActive: " + parent.remoteControlActive)
                    
                    // 同步本地状态（注意：parent 指向 Rectangle，remoteControlActive 是其属性）
                    // remoteControlConfirmed 是计算属性，会自动更新
                    // remoteControlActive 用于跟踪本地状态
                    if (parent) {
                        var oldActive = parent.remoteControlActive
                        parent.remoteControlActive = confirmed
                        console.log("[Client][UI][RemoteControl] ✓ 已更新按钮本地状态: remoteControlActive " + oldActive + " -> " + confirmed)
                        console.log("[Client][UI][RemoteControl] 更新后 remoteControlConfirmed: " + parent.remoteControlConfirmed)
                        console.log("[Client][UI][RemoteControl] 按钮文本应显示: " + (parent.remoteControlConfirmed ? "远驾已接管" : "远驾接管"))
                    } else {
                        console.log("[Client][UI][RemoteControl] ⚠ parent 为空，无法更新按钮状态")
                    }
                    console.log("[Client][UI][RemoteControl] ========================================")
                }
                function onDrivingModeChanged() {
                    var mode = (facade.appServices.vehicleStatus) ? facade.appServices.vehicleStatus.drivingMode : "自驾"
                    console.log("[Client][UI][RemoteControl] 驾驶模式变化 ==========")
                    console.log("[Client][UI][RemoteControl] 新驾驶模式: " + mode)
                    console.log("[Client][UI][RemoteControl] 当前按钮文本应显示: " + (parent.remoteControlConfirmed ? "远驾已接管" : "远驾接管"))
                    console.log("[Client][UI][RemoteControl] ========================================")
                }
            }
            
            // ★ 监听视频流状态变化：仅当「曾连接过视频」再断开时才发 remote_control false，避免 6s 后 connectFourStreams→disconnectAll 触发大量 false
            Connections {
                target: facade.appServices.webrtcStreamManager
                ignoreUnknownSignals: true
                function onAnyConnectedChanged() {
                    var videoConnected = (facade.appServices.videoStreamsConnected)
                    // ── 诊断：anyConnectedChanged 时打印四路各自状态 ───────────────────
                    var wsm = facade.appServices.webrtcStreamManager
                    var frontConn = wsm && wsm.frontClient ? wsm.frontClient.isConnected : false
                    var frontStatus = wsm && wsm.frontClient ? (wsm.frontClient.statusText || "未知") : "N/A"
                    var rearConn = wsm && wsm.rearClient ? wsm.rearClient.isConnected : false
                    var leftConn = wsm && wsm.leftClient ? wsm.leftClient.isConnected : false
                    var rightConn = wsm && wsm.rightClient ? wsm.rightClient.isConnected : false
                    var qmlRc = -1
                    if (wsm && wsm.getQmlSignalReceiverCount) qmlRc = wsm.getQmlSignalReceiverCount()
                    console.warn("[Client][UI][RemoteControl] ★★★ anyConnectedChanged ★★★"
                                + " anyConnected=" + videoConnected
                                + " front=" + frontConn + " status=" + frontStatus
                                + " rear=" + rearConn + " left=" + leftConn + " right=" + rightConn
                                + " qmlSignalRc=" + qmlRc
                                + " ★★★")
                    if (videoConnected) {
                        remoteControlTakeoverRect.hadVideoConnectedBefore = true
                    } else {
                        if (remoteControlTakeoverRect.hadVideoConnectedBefore) {
                            remoteControlTakeoverRect.hadVideoConnectedBefore = false
                            console.log("[Client][UI][RemoteControl] ⚠ 视频流已断开（曾连接过），发送 remote_control false")
                            if (facade.appServices.mqttController && facade.appServices.mqttController.mqttBrokerConnected) {
                                facade.appServices.mqttController.requestRemoteControl(false)
                                var ssmStop = facade.appServices.systemStateMachine
                                if (ssmStop && typeof ssmStop.fireByName === "function")
                                    ssmStop.fireByName("STOP_SESSION")
                                console.log("[Client][UI][RemoteControl] ✓ 已发送远驾接管禁用指令到车端（视频流断开）")
                            }
                        }
                    }
                    console.log("[Client][UI][RemoteControl] anyConnected=" + videoConnected + "（聚合）远驾按钮" + (videoConnected ? "已启用" : "已禁用") + "；主视图是否出画请看 [Client][VideoFrame]/[Client][UI][Video]")
                }
            }

            ToolTip.visible: remoteControlBtnMa.containsMouse
            ToolTip.text: {
                if (!buttonEnabled) {
                    return "视频流未连接，请先连接车辆后再启用远驾接管"
                }
                var mc2 = facade.appServices.mqttController
                if (!mc2 || !mc2.mqttBrokerConnected) {
                    return "MQTT 未连接：远驾指令仅经 vehicle/control 下发，请先连接 Broker"
                }
                if (remoteControlConfirmed) {
                    return "点击取消远驾接管"
                }
                return "点击启用远驾接管（允许直接控制车辆）"
            }
            ToolTip.delay: 300
        }
        
        // 驾驶模式显示（遥控、自驾、远驾）
        Rectangle {
            Layout.preferredWidth: 80
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: 8
            radius: 6
            border.width: 1
            border.color: facade.colorButtonBorder
            color: facade.colorButtonBg
            
            property string drivingMode: (facade.appServices.vehicleStatus) ? facade.appServices.vehicleStatus.drivingMode : "自驾"
            
            Text {
                anchors.centerIn: parent
                text: parent.drivingMode
                color: {
                    switch (parent.drivingMode) {
                        case "远驾": return facade.colorAccent
                        case "遥控": return "#FFA500"  // 橙色
                        case "自驾": return facade.colorTextPrimary
                        default: return facade.colorTextPrimary
                    }
                }
                font.pixelSize: 13
                font.bold: true
                font.family: facade.chineseFont || font.family
            }
            
            // 监听驾驶模式变化
            Connections {
                target: facade.appServices.vehicleStatus
                ignoreUnknownSignals: true
                function onDrivingModeChanged() {
                    var mode = (facade.appServices.vehicleStatus) ? facade.appServices.vehicleStatus.drivingMode : "自驾"
                    console.log("[Client][UI] 驾驶模式更新: " + mode)
                }
            }
            
            ToolTip.visible: drivingModeMouseArea.containsMouse
            ToolTip.text: "当前驾驶模式: " + drivingMode
            ToolTip.delay: 300
            
            MouseArea {
                id: drivingModeMouseArea
                anchors.fill: parent
                hoverEnabled: true
            }
        }
        
        // ★ 清扫状态显示（在驾驶模式右边）
        Rectangle {
            Layout.preferredWidth: 80
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: 8
            radius: 6
            border.width: 1
            border.color: facade.colorButtonBorder
            color: {
                // ★ 根据清扫状态改变颜色：启用时亮起，禁用时暗色
                var sweepActive = (facade.appServices.vehicleStatus) ? facade.appServices.vehicleStatus.sweepActive : false
                return sweepActive ? facade.colorAccent : facade.colorButtonBg
            }
            
            property bool sweepActive: (facade.appServices.vehicleStatus) ? facade.appServices.vehicleStatus.sweepActive : false
            
            Text {
                anchors.centerIn: parent
                text: "清扫"
                color: {
                    if (parent.sweepActive) {
                        return "#FFFFFF"  // 启用时白色文字
                    }
                    return facade.colorTextSecondary  // 禁用时灰色文字
                }
                font.pixelSize: 13
                font.bold: true
                font.family: facade.chineseFont || font.family
            }
            
            // 监听清扫状态变化
            Connections {
                target: facade.appServices.vehicleStatus
                ignoreUnknownSignals: true
                function onSweepActiveChanged() {
                    var active = (facade.appServices.vehicleStatus) ? facade.appServices.vehicleStatus.sweepActive : false
                    console.log("[Client][UI][Sweep] 清扫状态更新: " + (active ? "启用" : "禁用"))
                }
            }
            
            ToolTip.visible: sweepStatusMouseArea.containsMouse
            ToolTip.text: "清扫状态: " + (sweepActive ? "清扫中" : "未清扫")
            ToolTip.delay: 300
            
            MouseArea {
                id: sweepStatusMouseArea
                anchors.fill: parent
                hoverEnabled: true
            }
        }
        
        // ★ 刹车状态显示（在清扫状态按钮右侧）
        Rectangle {
            Layout.preferredWidth: 80
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: 8
            radius: 6
            border.width: 1
            border.color: facade.colorButtonBorder
            color: {
                // ★ 根据刹车状态改变颜色：启用时红色亮起，禁用时暗色
                var brakeActive = (facade.appServices.vehicleStatus) ? facade.appServices.vehicleStatus.brakeActive : false
                return brakeActive ? facade.colorDanger : facade.colorButtonBg
            }
            
            property bool brakeActive: (facade.appServices.vehicleStatus) ? facade.appServices.vehicleStatus.brakeActive : false
            
            Text {
                anchors.centerIn: parent
                text: "刹车"
                color: {
                    if (parent.brakeActive) {
                        return "#FFFFFF"  // 启用时白色文字
                    }
                    return facade.colorTextSecondary  // 禁用时灰色文字
                }
                font.pixelSize: 13
                font.bold: true
                font.family: facade.chineseFont || font.family
            }
            
            // 监听刹车状态变化
            Connections {
                target: facade.appServices.vehicleStatus
                ignoreUnknownSignals: true
                function onBrakeActiveChanged() {
                    var active = (facade.appServices.vehicleStatus) ? facade.appServices.vehicleStatus.brakeActive : false
                    console.log("[Client][UI][Brake] 刹车状态更新: " + (active ? "启用" : "禁用"))
                }
            }
            
            ToolTip.visible: brakeStatusMouseArea.containsMouse
            ToolTip.text: "刹车状态: " + (brakeActive ? "刹车中" : "未刹车")
            ToolTip.delay: 300
            
            MouseArea {
                id: brakeStatusMouseArea
                anchors.fill: parent
                hoverEnabled: true
            }
        }
        
        Connections {
            target: facade.appServices.mqttController
            ignoreUnknownSignals: true
            function onMqttConnectResolved(succeeded, detail) {
                if (succeeded)
                    return
                if (facade.teleop.pendingConnectVideo) {
                    facade.teleop.pendingConnectVideo = false
                    console.warn("[Client][UI][Connect] mqttConnectResolved(false) detail=" + detail
                                 + " | 查日志 [CLIENT][MQTT][CHAIN]")
                }
            }
            function onConnectionStatusChanged(connected) {
                if (connected && facade.teleop.pendingConnectVideo) {
                    facade.teleop.pendingConnectVideo = false
                    if (facade.appServices.mqttController) facade.appServices.mqttController.requestStreamStart()
                    var lw2 = facade.appServices.vehicleManager ? facade.appServices.vehicleManager.lastWhepUrl : ""
                    var wsm2 = facade.appServices.webrtcStreamManager
                    console.warn("[Client][StreamE2E][QML_MQTT_CONNECTED] pendingConnectVideo→ZlmReady poll lastWhepUrl_len=" + String(lw2 || "").length)
                    if (wsm2 && wsm2.scheduleConnectFourStreamsWhenZlmReady !== undefined)
                        wsm2.scheduleConnectFourStreamsWhenZlmReady(lw2 || "", 1000, 60000)
                    else if (wsm2)
                        wsm2.connectFourStreams(lw2 || "")
                }
            }
        }
        Connections {
            target: facade.appServices.webrtcStreamManager
            ignoreUnknownSignals: true
            function onAnyConnectedChanged() {
                if (facade.appServices.videoStreamsConnected) {
                    facade.teleop.streamStopped = false
                    var wsm = facade.appServices.webrtcStreamManager
                    var f = wsm && wsm.frontClient && wsm.frontClient.isConnected
                    var r = wsm.rearClient && wsm.rearClient.isConnected
                    var l = wsm.leftClient && wsm.leftClient.isConnected
                    var rt = wsm.rightClient && wsm.rightClient.isConnected
                    console.log("[Client][UI][Video] anyConnected=true 重置 streamStopped=false 分路 f=" + f + " r=" + r + " l=" + l + " rt=" + rt + "（任一为真即 anyConnected，主视图仍可能缺 front 帧）")
                }
            }
        }
        
        Item { Layout.fillWidth: true }
        
        // 模式显示
        Row {
            spacing: 8
            Text { text: "◀"; color: facade.colorAccent; font.pixelSize: 16 }
            Text {
                text: facade.teleop.currentGear === "R" ? "倒车模式" : "前进模式"
                color: facade.colorTextPrimary
                font.pixelSize: 16
                font.bold: true
                font.family: facade.chineseFont || font.family
            }
            Text { text: "▶"; color: facade.colorAccent; font.pixelSize: 16 }
        }
        
        Item { Layout.fillWidth: true }
        // ★ 网络质量告警指示器（规范 §9.3）
        Rectangle {
            id: rttIndicator
            Layout.preferredWidth: 80
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: 8
            radius: 6
            border.width: 1
            
            property double rtt: (facade.appServices.vehicleStatus && typeof facade.appServices.vehicleStatus.networkRtt === "number") ? facade.appServices.vehicleStatus.networkRtt : 0
            property string statusText: rttIndicator.rtt > 300 ? "网络严重" : (rttIndicator.rtt > 150 ? "网络延迟" : "网络正常")
            
            border.color: rttIndicator.rtt > 300 ? facade.colorDanger : (rttIndicator.rtt > 150 ? facade.colorWarning : facade.colorAccent)
            color: rttIndicator.rtt > 300 ? "#331111" : (rttIndicator.rtt > 150 ? "#332200" : facade.colorButtonBg)
            
            SequentialAnimation on opacity {
                running: rttIndicator.rtt > 300
                loops: Animation.Infinite
                NumberAnimation { to: 0.6; duration: 500 }
                NumberAnimation { to: 1.0; duration: 500 }
            }

            Text {
                anchors.centerIn: parent
                text: rttIndicator.statusText
                color: rttIndicator.rtt > 300 ? facade.colorDanger : (rttIndicator.rtt > 150 ? facade.colorWarning : facade.colorTextPrimary)
                font.pixelSize: 13
                font.bold: true
                font.family: facade.chineseFont || font.family
            }
        }

        // 右侧工具图标
        Row {
            spacing: 6
            Repeater {
                model: [
                    { icon: "icon/sun.svg", name: "太阳" },
                    { icon: "icon/warning.svg", name: "警告" },
                    { icon: "icon/settings.svg", name: "设置" },
                    { icon: "icon/location.svg", name: "位置" },
                    { icon: "icon/weather.svg", name: "天气" }
                ]
                Rectangle {
                    width: 26; height: 26; radius: 4
                    color: facade.colorBorder
                    border.color: facade.colorButtonBorder
                    border.width: 1
                    Image {
                        anchors.centerIn: parent
                        width: 16; height: 16
                        source: modelData.icon
                        fillMode: Image.PreserveAspectFit
                    }
                    ToolTip.visible: toolMa.containsMouse
                    ToolTip.text: modelData.name
                    ToolTip.delay: 200
                    MouseArea { id: toolMa; anchors.fill: parent; hoverEnabled: true }
                }
            }
        }
        
        // 时间和温度
        Text {
            text: Qt.formatDateTime(new Date(), "hh:mm")
            color: facade.colorTextPrimary
            font.pixelSize: 14
            font.bold: true
        }
        Text {
            text: "007°C"
            color: facade.colorTextSecondary
            font.pixelSize: 12
        }
    }
}
