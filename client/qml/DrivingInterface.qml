import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0

/**
 * 远程驾驶主界面 - 先整体布局再分配
 *
 * 布局策略：先整体分配，保证每一部分都能显示
 * 1. 顶层：顶部栏 + 主内容区（按比例分配高度）
 * 2. 主内容区：左列 | 中列 | 右列（按 22:55:23 分配宽度，先分配后填充）
 * 3. 每列内部：固定比例分配，保证左视图、主视图、右视图、高精地图、控制区等均有最小空间
 */
Rectangle {
    id: drivingInterface
    color: "#0F0F1A"
    
    // ==================== 布局诊断日志（[Client][UI][Layout] 便于精准定位） ====================
    function logLayout(reason) {
        var diw = width ? Math.round(width) : 0
        var dih = height ? Math.round(height) : 0
        var mainH = mainRowRatio > 0 ? Math.round(height * mainRowRatio) : 0
        var rightPrefW = Math.round(width * rightColWidthRatio)
        console.log("[Client][UI][Layout] " + (reason || "onChange") + " drivingInterface=" + diw + "x" + dih + " mainRowH=" + mainH + " rightColPrefW=" + rightPrefW)
    }
    function logLayoutFull() {
        var diW = width ? Math.round(width) : 0
        var diH = height ? Math.round(height) : 0
        var topBarH = topBarRect ? Math.round(topBarRect.height) : 0
        var mainRowH = mainRowLayout ? Math.round(mainRowLayout.height) : 0
        var mainRowY = mainRowLayout ? Math.round(mainRowLayout.y) : 0
        var leftW = leftColLayout ? Math.round(leftColLayout.width) : 0
        var leftH = leftColLayout ? Math.round(leftColLayout.height) : 0
        var leftX = leftColLayout ? Math.round(leftColLayout.x) : 0
        var leftY = leftColLayout ? Math.round(leftColLayout.y) : 0
        var centerW = centerColLayout ? Math.round(centerColLayout.width) : 0
        var centerH = centerColLayout ? Math.round(centerColLayout.height) : 0
        var centerX = centerColLayout ? Math.round(centerColLayout.x) : 0
        var centerY = centerColLayout ? Math.round(centerColLayout.y) : 0
        var rightW = rightColMeasurer ? Math.round(rightColMeasurer.width) : 0
        var rightH = rightColMeasurer ? Math.round(rightColMeasurer.height) : 0
        var rightX = rightColMeasurer ? Math.round(rightColMeasurer.x) : 0
        var rightY = rightColMeasurer ? Math.round(rightColMeasurer.y) : 0
        var rightViewW = rightViewVideo ? Math.round(rightViewVideo.width) : 0
        var rightViewH = rightViewVideo ? Math.round(rightViewVideo.height) : 0
        var rightViewContH = rightViewH   // 右视图区高度与 VideoPanel 一致
        var rightViewRelY = rightViewVideo ? Math.round(rightViewVideo.y) : 0
        var hdMapW = hdMapRect ? Math.round(hdMapRect.width) : 0
        var hdMapH = hdMapRect ? Math.round(hdMapRect.height) : 0
        var hdMapContH = hdMapH          // 高精地图区高度与 Rectangle 一致
        var hdMapRelY = hdMapRect ? Math.round(hdMapRect.y) : 0
        var ctrlH = centerControlsRect ? Math.round(centerControlsRect.height) : 0
        var dashH = centerDashboardRect ? Math.round(centerDashboardRect.height) : 0
        var rootH = rootColumnLayout && rootColumnLayout.height ? Math.round(rootColumnLayout.height) : (height ? Math.round(height) : 0)
        var rightColImplicitH = rightColMeasurer ? (rightColMeasurer.implicitHeight || 0) : 0
        var rightColLayoutMinH = rightColMeasurer && rightColMeasurer.Layout ? (rightColMeasurer.Layout.minimumHeight || 0) : 0
        var mainRowMax = rootH > 0 ? Math.max(0, rootH - topBarH - rootColumnLayout.spacing) : 0
        var overflow = mainRowH - mainRowMax
        // 关键子组件实际高度
        var leftFrontH = leftFrontPanel ? Math.round(leftFrontPanel.height) : 0
        var leftRearH = leftRearPanel ? Math.round(leftRearPanel.height) : 0
        var centerCamH = centerCameraRect ? Math.round(centerCameraRect.height) : 0
        // 理论预算（基于 mainRowAvailH 的比例）
        var leftFrontBudget = Math.round(mainRowAvailH * leftVideoRatio)
        var centerCamBudget = Math.round(mainRowAvailH * centerCameraRatio)
        var ctrlBudget = Math.round(mainRowAvailH * centerControlsRatio)
        var dashBudget = Math.round(mainRowAvailH * centerDashboardRatio)
        console.log("[Client][UI][Layout] === 全布局尺寸 ===")
        console.log("[Client][UI][Layout] drivingInterface=" + diW + "x" + diH + " rootColH=" + rootH + " (componentsReady 影响父 visible)")
        console.log("[Client][UI][Layout] topBar=" + topBarH + " mainRow=" + mainRowH + " leftCol=" + leftW + "x" + leftH + " centerCol=" + centerW + "x" + centerH + " rightCol=" + rightW + "x" + rightH + " (rightH=0 则 Item 无 implicitHeight 导致)")
        console.log("[Client][UI][Layout] 垂直预算 mainRowAvailH=" + Math.round(mainRowAvailH) + " mainRowMax=" + mainRowMax + " overflow=" + overflow)
        console.log("[Client][UI][Layout] 位置: mainRowY=" + mainRowY + " leftCol(x=" + leftX + ",y=" + leftY + ") centerCol(x=" + centerX + ",y=" + centerY + ") rightCol(x=" + rightX + ",y=" + rightY + ")")
        console.log("[Client][UI][Layout] 左列拆分: 上(左视图)=" + leftFrontH + " (预算=" + leftFrontBudget + ") 下(后视图)=" + leftRearH)
        console.log("[Client][UI][Layout] 中列拆分: 摄像头区=" + centerCamH + " (预算=" + centerCamBudget + ") 控制区=" + ctrlH + " (预算=" + ctrlBudget + ") 仪表盘=" + dashH + " (预算=" + dashBudget + ")")
        if (rightH === 0) console.log("[Client][UI][Layout] [诊断] rightCol height=0: implicitH=" + rightColImplicitH + " Layout.minimumHeight=" + rightColLayoutMinH + " -> 需 Layout.minimumHeight/preferredHeight")
        console.log("[Client][UI][Layout] 右列拆分: 容器=" + rightH + " 右视图区=" + rightViewContH + " 高精地图区=" + hdMapContH + " 右视图=" + rightViewW + "x" + rightViewH + " 高精地图=" + hdMapW + "x" + hdMapH)
        console.log("[Client][UI][Layout] 右列位置: rightViewY(mainRow基准)=" + rightViewRelY + " hdMapY(mainRow基准)=" + hdMapRelY + " → 绝对Y: rightViewTop=" + (mainRowY + rightViewRelY) + " hdMapTop=" + (mainRowY + hdMapRelY))
        // === 增强诊断：定位 rightCol(x=0,y=905) 根因 ===
        var mainRowW = mainRowLayout ? Math.round(mainRowLayout.width) : 0
        var mainRowChildren = mainRowLayout && mainRowLayout.children ? mainRowLayout.children.length : 0
        var mainRowType = mainRowLayout ? (mainRowLayout.columns !== undefined ? "GridLayout" : "RowLayout") : "null"
        var mainRowCols = mainRowLayout && mainRowLayout.columns !== undefined ? mainRowLayout.columns : -1
        var rightParentId = rightColMeasurer && rightColMeasurer.parent ? (rightColMeasurer.parent.id || "无id") : "null"
        var rightParentSame = rightColMeasurer && mainRowLayout && rightColMeasurer.parent === mainRowLayout
        var rightLayoutCol = rightColMeasurer && rightColMeasurer.Layout && rightColMeasurer.Layout.column !== undefined ? rightColMeasurer.Layout.column : -999
        var rightLayoutRow = rightColMeasurer && rightColMeasurer.Layout && rightColMeasurer.Layout.row !== undefined ? rightColMeasurer.Layout.row : -999
        var colGap = (mainRowLayout && mainRowLayout.columnSpacing !== undefined) ? mainRowLayout.columnSpacing : ((mainRowLayout && mainRowLayout.spacing !== undefined) ? mainRowLayout.spacing : 8)
        var sumWidth = leftW + colGap * 2 + centerW + rightW
        var widthOk = mainRowW >= sumWidth
        var leftRightDiff = Math.abs(leftW - rightW)
        var fillRatio = mainRowW > 0 ? (sumWidth / mainRowW * 100).toFixed(1) : 0
        console.log("[Client][UI][Layout] === 布局诊断 ===")
        console.log("[Client][UI][Layout] mainRowType=" + mainRowType + " columns=" + mainRowCols + " mainRowW=" + mainRowW + " children=" + mainRowChildren)
        console.log("[Client][UI][Layout] 宽度分配: mainRowAvailW=" + Math.round(mainRowAvailW) + " sideColAllocW=" + Math.round(sideColAllocW) + " centerColAllocW=" + Math.round(centerColAllocW))
        console.log("[Client][UI][Layout] 实际宽度: left=" + leftW + " center=" + centerW + " right=" + rightW + " sum=" + Math.round(sumWidth) + " 铺满率=" + fillRatio + "%")
        console.log("[Client][UI][Layout] 左右对称: left-right=" + leftRightDiff + " (差=0则对称)" + (leftRightDiff > 5 ? " [异常]左右列宽度不一致" : ""))
        console.log("[Client][UI][Layout] rightCol父节点: id=" + rightParentId + " 是否mainRowLayout=" + rightParentSame)
        console.log("[Client][UI][Layout] rightCol Layout.column=" + rightLayoutCol + " Layout.row=" + rightLayoutRow)
        if (mainRowLayout && mainRowLayout.children) {
            for (var i = 0; i < mainRowLayout.children.length; i++) {
                var c = mainRowLayout.children[i]
                var cid = c && c.id ? c.id : ("child" + i)
                var cx = c ? Math.round(c.x) : 0
                var cy = c ? Math.round(c.y) : 0
                if (c === rightColMeasurer) console.log("[Client][UI][Layout] mainRowLayout.children[" + i + "]=" + cid + " (右列) x=" + cx + " y=" + cy)
                else if (i < 3) console.log("[Client][UI][Layout] mainRowLayout.children[" + i + "]=" + cid + " x=" + cx + " y=" + cy)
            }
        }
        if (rightY !== 0) console.log("[Client][UI][Layout] [异常] rightCol.y=" + rightY + " 应=0，右列被排到第二行底部")
    }
    // 仅当 CLIENT_LAYOUT_DEBUG=1 时开启布局诊断，避免影响操作/查看流畅性
    property bool isLayoutDebugEnabled: (typeof layoutDebugEnabled !== "undefined" && layoutDebugEnabled) || false
    Component.onCompleted: {
        logLayout("onCompleted")
        if (isLayoutDebugEnabled) layoutLogTimer.start()
    }
    onWidthChanged: if (isLayoutDebugEnabled) logLayout("widthChanged")
    onHeightChanged: if (isLayoutDebugEnabled) logLayout("heightChanged")
    Timer {
        id: layoutLogTimer
        interval: 500
        repeat: true
        running: false
        property int repeatCount: 0
        onTriggered: {
            logLayoutFull()
            repeatCount++
            if (repeatCount >= 8) stop()
        }
    }
    
    // ==================== 比例常量（先整体分配，保证每部分显示） ====================
    readonly property real topBarRatio: 1/18
    readonly property real mainRowRatio: 1 - topBarRatio
    // 三列宽度比例：左右对称 20%+60%+20%=100%，铺满整个宽度
    readonly property real sideColWidthRatio: 0.20
    readonly property real leftColWidthRatio: sideColWidthRatio
    readonly property real centerColWidthRatio: 0.60
    readonly property real rightColWidthRatio: sideColWidthRatio
    // 主内容区可用宽度（扣除边距与列间距），适当增大间距与左右边距
    readonly property real mainRowSpacing: 8
    readonly property real mainRowAvailW: Math.max(0, (width || 1280) - 16 - mainRowSpacing * 2)
    // 先分配：三列按比例分配宽度，左右列共用 sideColAllocW 保证对称
    readonly property real sideColAllocW: mainRowAvailW * sideColWidthRatio
    readonly property real leftColAllocW: sideColAllocW
    readonly property real centerColAllocW: mainRowAvailW * centerColWidthRatio
    readonly property real rightColAllocW: sideColAllocW
    // 左右列共用：侧边列最小/最大宽度、最小高度（保证左右列布局完全一致）
    readonly property int sideColMinWidth: 180
    readonly property int sideColMaxWidth: 390
    readonly property real sideColMinHeight: Math.max(400, mainRowAvailH * 0.9)
    // 左右列子组件对称：上半(左视图/右视图)、下半(后视图/高精地图)
    readonly property int sideColTopMinHeight: 100
    readonly property int sideColBottomMinHeight: 120
    // 左/右列上下块统一比例，保证 左视图↔右视图、后视图↔高精地图 水平平齐
    readonly property real leftVideoRatio: 0.58   // 左视图、右视图 共用
    readonly property real leftMapRatio: 0.40    // 后视图、高精地图 共用（留 2% 给 spacing）
    readonly property real centerCameraRatio: 0.66  // 三块比例和<1，留出 spacing，避免 overflow
    readonly property real centerControlsRatio: 0.12
    readonly property real centerDashboardRatio: 0.20
    // 主视图下方：控制区与仪表盘布局常量（统一管理便于调参）
    readonly property int controlAreaMargin: 8
    readonly property int controlAreaSpacing: 8
    readonly property real controlSideColumnRatio: 0.35   // 车辆灯光/清扫功能 各占控制区宽度比例
    readonly property int controlSpeedometerSize: 140     // 速度表外径上限
    readonly property int minControlHeight: 110
    readonly property int minDashboardHeight: 110
    readonly property int dashboardMargin: 10
    readonly property int dashboardSpacing: 12
    readonly property int dashboardGearWidth: 104
    readonly property int dashboardTankWidth: 148
    readonly property int dashboardSpeedWidth: 164
    readonly property int dashboardStatusWidth: 156
    readonly property int dashboardProgressWidth: 158
    readonly property int dashboardGearSelectWidth: 220
    readonly property int dashboardSplitterMargin: 8
    // 主内容区可用高度（从根 ColumnLayout 实际高度扣除顶部栏和间距），用于垂直预算，避免 mainRow 溢出
    // 当 rootColumnLayout 未完成布局时用 drivingInterface.height 兜底，避免 mainRowAvailH=0 导致布局异常
    readonly property real mainRowAvailH: {
        var base = (height || 720)
        if (rootColumnLayout && topBarRect && rootColumnLayout.height > 0)
            return Math.max(200, rootColumnLayout.height - topBarRect.height - rootColumnLayout.spacing)
        return Math.max(200, base * mainRowRatio)
    }
    
    // ==================== 主题颜色 ====================
    readonly property color colorBackground: "#0F0F1A"
    readonly property color colorPanel: "#1A1A2A"
    readonly property color colorBorder: "#2A2A3E"
    readonly property color colorBorderActive: "#4A90E2"
    readonly property color colorAccent: "#50C878"
    readonly property color colorWarning: "#E8A030"
    readonly property color colorDanger: "#E85050"
    readonly property color colorTextPrimary: "#FFFFFF"
    readonly property color colorTextSecondary: "#B0B0C0"
    readonly property color colorButtonBg: "#1E2433"
    readonly property color colorButtonBorder: "#3A4A6E"
    readonly property color colorButtonBgHover: "#26314A"
    readonly property color colorButtonBgPressed: "#1A2235"
    
    // ==================== 字体设置 ====================
    property string chineseFont: {
        if (typeof window !== "undefined" && window.chineseFont) return window.chineseFont
        var fonts = ["WenQuanYi Zen Hei", "WenQuanYi Micro Hei", "Noto Sans CJK SC"]
        var availableFonts = Qt.fontFamilies()
        for (var i = 0; i < fonts.length; i++) {
            if (availableFonts.indexOf(fonts[i]) !== -1) return fonts[i]
        }
        return ""
    }
    
    // ==================== 车辆状态属性 ====================
    property string currentGear: "N"
    property bool forwardMode: currentGear !== "R"
    property real vehicleSpeed: 35
    property real targetSpeed: 0.0  // ★ 默认值改为0.0
    property bool emergencyStopPressed: false  // ★ 急停按钮状态
    property real steeringAngle: 0
    // 主界面显示用：有车端状态时优先显示车端（速度、档位、转向），否则用本地/占位
    property real displaySpeed: (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected && typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.speed : vehicleSpeed
    property string displayGear: (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected && typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.gear : currentGear
    property real displaySteering: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.steering : steeringAngle
    // ★ 绑定到车端遥测数据（VehicleStatus 已提供默认值）
    property real waterTankLevel: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.waterTankLevel : 75
    property real trashBinLevel: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.trashBinLevel : 40
    property int cleaningCurrent: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.cleaningCurrent : 400
    property int cleaningTotal: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.cleaningTotal : 500
    
    // ==================== 灯光状态 ====================
    property bool leftTurnActive: false
    property bool rightTurnActive: false
    property bool brakeLightActive: false
    property bool workLightActive: false
    property bool headlightActive: true
    property bool warningLightActive: false
    
    // ==================== 清扫功能状态 ====================
    property bool sweepActive: true
    property bool waterSprayActive: false
    property bool suctionActive: false
    property bool dumpActive: false
    property bool hornActive: false
    property bool workLampActive: false
    
    // ==================== 通用指令发送（DataChannel 优先，MQTT 降级） ====================
    /**
     * 统一控制指令发送入口
     * 优先使用 WebRTC DataChannel (低延迟)，不可用时降级至 MQTT (高可靠)
     * @param type 指令类型 (如 "gear", "speed", "brake", "light", "sweep")
     * @param payload 指令负载数据 (对象格式，如 {value: 1})
     */
    function sendControlCommand(type, payload) {
        var legacyOnly = (typeof clientLegacyControlOnly !== "undefined" && clientLegacyControlOnly)
        if (!legacyOnly && typeof vehicleControl !== "undefined" && vehicleControl && typeof vehicleControl.sendUiCommand === "function") {
            vehicleControl.sendUiCommand(type, payload)
            return
        }
        var msg = JSON.stringify({
            schemaVersion: "1.0",
            vin: (typeof vehicleManager !== "undefined" && vehicleManager) ? vehicleManager.currentVin : "",
            sessionId: "", // TODO: 填入实际 sessionId
            seq: 0,        // TODO: 实现递增 seq
            timestampMs: Date.now(),
            payload: payload,
            type: type
        })

        // 1. 优先尝试 WebRTC DataChannel (规范 §5.5: 客户端→ZLM→车端)
        // 假设 frontClient 或任意已连接 Client 具备 sendDataChannelMessage 方法
        if (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && 
            webrtcStreamManager.anyConnected && webrtcStreamManager.frontClient &&
            typeof webrtcStreamManager.frontClient.sendDataChannelMessage === "function") {
            console.log("[Command] Sending via WebRTC DataChannel (Priority):", type);
            webrtcStreamManager.frontClient.sendDataChannelMessage(msg);
            return;
        }

        // 2. 降级至 MQTT (规范 §8.2: 链路劣化回退)
        // ★ 统一发送 JSON 格式，与 DataChannel 保持一致，便于车端统一解析
        if (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected) {
            console.log("[Command] Sending via MQTT (Fallback):", type);
            if (typeof mqttController.publish === "function") {
                // 推荐方式：直接发送 JSON 字符串到统一 Topic，车端只需一个监听器
                mqttController.publish("vehicle/control", msg);
            } else {
                // 兼容旧方式：若 mqttController 无通用 publish 方法，则映射到特定命令方法
                console.warn("[Command] mqttController.publish not found, fallback to legacy methods");
                switch (type) {
                    case "gear":
                        if (typeof mqttController.sendGearCommand === "function") mqttController.sendGearCommand(payload.value);
                        break;
                    case "brake":
                        if (typeof mqttController.sendBrakeCommand === "function") mqttController.sendBrakeCommand(payload.value);
                        break;
                    case "speed":
                        if (typeof mqttController.sendSpeedCommand === "function") mqttController.sendSpeedCommand(payload.value);
                        break;
                    case "light":
                        if (typeof mqttController.sendLightCommand === "function") mqttController.sendLightCommand(payload.name, payload.active);
                        break;
                    case "sweep":
                        if (typeof mqttController.sendSweepCommand === "function") mqttController.sendSweepCommand(payload.name, payload.active);
                        break;
                    default:
                        console.warn("[Command] Unknown MQTT command type:", type);
                }
            }
        } else {
            console.warn("[Command] No channel available (WebRTC disconnected, MQTT disconnected)");
        }
    }

    // ==================== 信号 ====================
    signal gearChanged(string gear)
    signal lightCommandSent(string lightType, bool active)
    signal sweepCommandSent(string sweepType, bool active)
    signal speedCommandSent(real speed)
    /** 请求打开 MQTT 连接设置对话框（仅当无 VIN 绑定配置且未连接时） */
    signal openMqttDialogRequested()
    /** 点击「连接」后若需先连 MQTT，则 MQTT 连上后自动触发推流/拉流 */
    property bool pendingConnectVideo: false
    /** 是否已停止推流（用于按钮文本状态） */
    property bool streamStopped: false
    
    onCurrentGearChanged: {
        forwardMode = currentGear !== "R"
        gearChanged(currentGear)
        
        // ★ 检查远驾接管状态
        var remoteControlEnabled = false;
        if (typeof vehicleStatus !== "undefined" && vehicleStatus) {
            remoteControlEnabled = (vehicleStatus.drivingMode === "远驾");
        }
        if (!remoteControlEnabled) {
            console.log("[GEAR] ⚠ 远驾接管未启用，无法发送档位命令")
            return;
        }

        var gearValue = 0;  // 默认空档
        if (currentGear === "R") gearValue = -1;
        else if (currentGear === "D") gearValue = 1;
        else if (currentGear === "P") gearValue = 2;

        console.log("[GEAR] 档位变化: " + currentGear + " (数值: " + gearValue + ")，准备发送");
        
        // ★ 使用统一发送接口 (DataChannel 优先)
        sendControlCommand("gear", { value: gearValue });
    }
    
    // ==================== 可复用组件 ====================
    
    // 分隔线组件
    component VerticalDivider: Rectangle {
        Layout.preferredWidth: 1
        Layout.fillHeight: true
        Layout.maximumHeight: 50
        color: colorBorder
        opacity: 0.5
    }
    
    // 控制按钮组件
    component ControlButton: Rectangle {
        id: controlBtn
        property string label: ""
        property bool active: false
        property string buttonKey: ""
        property color activeColor: colorAccent
        signal clicked()
        
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumWidth: 50
        Layout.minimumHeight: 36
        
        radius: 8
        border.width: active ? 2 : 1
        border.color: active ? activeColor : (mouseArea.containsMouse ? colorBorderActive : colorButtonBorder)
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: {
                    if (controlBtn.active) return Qt.lighter(controlBtn.activeColor, 1.15)
                    if (mouseArea.pressed) return colorButtonBgPressed
                    if (mouseArea.containsMouse) return colorButtonBgHover
                    return colorButtonBg
                }
            }
            GradientStop {
                position: 1.0
                color: {
                    if (controlBtn.active) return Qt.darker(controlBtn.activeColor, 1.35)
                    if (mouseArea.pressed) return Qt.darker(colorButtonBgPressed, 1.2)
                    if (mouseArea.containsMouse) return Qt.darker(colorButtonBgHover, 1.2)
                    return Qt.darker(colorButtonBg, 1.25)
                }
            }
        }
        scale: mouseArea.pressed ? 0.96 : (mouseArea.containsMouse ? 1.02 : 1.0)
        opacity: mouseArea.pressed ? 0.95 : 1.0
        
        Text {
            anchors.centerIn: parent
            text: controlBtn.label
            color: controlBtn.active ? "#FFFFFF" : "#DDE6FF"
            font.pixelSize: Math.max(10, Math.min(13, controlBtn.width / 4))
            font.bold: controlBtn.active
            font.family: chineseFont || font.family
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 1
            height: parent.height * 0.35
            radius: 7
            color: controlBtn.active ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(1, 1, 1, mouseArea.containsMouse ? 0.12 : 0.06)
        }

        Behavior on scale {
            NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
        }
        Behavior on border.color {
            ColorAnimation { duration: 160 }
        }
        Behavior on opacity {
            NumberAnimation { duration: 90 }
        }
        
        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: controlBtn.clicked()
        }
    }

    component DashboardCard: Rectangle {
        radius: 10
        color: "#131A2A"
        border.width: 1
        border.color: "#2F3F63"
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1A2236" }
            GradientStop { position: 1.0; color: "#10182A" }
        }
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            anchors.topMargin: 1
            height: 1
            color: Qt.rgba(0.7, 0.82, 1.0, 0.28)
            radius: 10
        }
    }
    
    // 视频窗口组件（可绑定 webrtcStreamManager 的 front/rear/left/right Client）
    // 有视频帧时由 VideoRenderer 显示；无帧时显示占位与状态文字
    component VideoPanel: Rectangle {
        property string title: ""
        property bool showPlaceholder: true
        property QtObject streamClient: null  // WebRtcClient*，用于显示连接状态与 videoFrameReady

        // 视频帧状态：占位符仅在无视频帧时显示；渲染器始终参与 update 以避免首帧延迟
        property bool hasVideoFrame: false
        property int placeholderClearDelay: 2500  // ms，连接断开后延迟清除

        radius: 6
        color: colorPanel
        border.color: colorBorderActive
        border.width: 1

        // 延迟清除 hasVideoFrame（避免 ICE 瞬时 Disconnected 造成闪烁）
        Timer {
            id: placeholderClearTimer
            interval: placeholderClearDelay
            repeat: false
            onTriggered: {
                hasVideoFrame = false
                console.log("[Client][UI][Video] VideoPanel placeholder 显示（连接断开≥2.5s）")
            }
        }

        Column {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4

            Text {
                text: title
                color: colorTextPrimary
                font.pixelSize: 12
                font.bold: true
                font.family: chineseFont || font.family
            }

            Item {
                width: parent.width
                height: parent.height - 24
                clip: true

                // 视频渲染器：z=5，始终可见，无帧时显示黑底
                VideoRenderer {
                    id: videoRenderer
                    anchors.fill: parent
                    z: 5
                    visible: true
                }

                Connections {
                    target: streamClient
                    ignoreUnknownSignals: true
                    function onVideoFrameReady(frame) {
                        placeholderClearTimer.stop()
                        if (frame && videoRenderer) {
                            videoRenderer.setFrame(frame)
                            hasVideoFrame = true
                            console.log("[Client][UI][Video] VideoPanel onVideoFrameReady hasVideoFrame=true")
                        } else {
                            console.warn("[Client][UI][Video] VideoPanel onVideoFrameReady: 空帧或渲染器缺失")
                        }
                    }
                    function onConnectionStatusChanged(connected) {
                        console.log("[Client][UI][Video] VideoPanel onConnectionStatusChanged connected=" + connected)
                        if (connected) {
                            placeholderClearTimer.stop()
                        } else {
                            placeholderClearTimer.restart()
                        }
                    }
                }

                // 占位图标：仅在无视频帧且 showPlaceholder 时显示（z=15，覆盖在 VideoRenderer 之上）
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    visible: showPlaceholder && !hasVideoFrame
                    z: 15
                    Text {
                        anchors.centerIn: parent
                        text: "📹"
                        font.pixelSize: 28
                        color: colorBorderActive
                    }
                }

                // 连接状态文字：z=16，始终显示
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
    
    // 进度条组件
    component ProgressBar: Rectangle {
        property real value: 0  // 0-100
        property color barColor: colorBorderActive
        
        height: 6
        radius: 3
        color: colorBorder
        
        Rectangle {
            width: parent.width * Math.min(1, Math.max(0, value / 100))
            height: parent.height
            radius: 3
            color: barColor
            
            Behavior on width {
                NumberAnimation { duration: 200 }
            }
        }
    }
    
    // ==================== 主布局 ====================
    ColumnLayout {
        id: rootColumnLayout
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4
        
        // ==================== 顶部信息栏 ====================
        Rectangle {
            id: topBarRect
            Layout.fillWidth: true
            // 使用固定高度，避免对 drivingInterface.height 的反向依赖导致 mainRow 高度溢出
            Layout.preferredHeight: 54
            Layout.minimumHeight: 48
            color: colorPanel
            border.color: colorBorder
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
                            color: modelData.active ? colorAccent : colorBorder
                            border.color: modelData.active ? colorAccent : colorButtonBorder
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
                    border.color: (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected) ? colorAccent : (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected ? "#508050" : colorButtonBorder)
                    color: (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected) ? "#1A2A1A" : (pendingConnectVideo ? "#2A3A2A" : (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected ? "#1A2A1A" : colorButtonBg))
                    Text {
                        anchors.centerIn: parent
                        text: {
                            if (pendingConnectVideo) return "连接中..."
                            if (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected) return "已连接"
                            if (streamStopped && typeof mqttController !== "undefined" && mqttController && mqttController.isConnected) return "连接车辆"
                            if (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected) return "MQTT已连接"
                            return "连接车端"
                        }
                        color: (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected) ? colorAccent : (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected ? "#70B070" : colorTextPrimary)
                        font.pixelSize: 14
                        font.bold: true
                        font.family: chineseFont || font.family
                    }
                    MouseArea {
                        id: connectBtnMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected) {
                                // 先发送停止推流指令给车端
                                if (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected) {
                                    mqttController.requestStreamStop()
                                    console.log("[QML] 已发送停止推流指令给车端")
                                }
                                // 然后断开视频流连接
                                webrtcStreamManager.disconnectAll()
                                // ★ 标记推流已停止，按钮文本将变为"连接车辆"
                                streamStopped = true
                                console.log("[QML] 推流已停止，按钮状态更新为 streamStopped=true")
                                return
                            }
                            // ★ 重新连接时重置停止状态
                            streamStopped = false
                            var currentVin = (typeof vehicleManager !== "undefined" && vehicleManager) ? vehicleManager.currentVin : ""
                            console.log("[CLIENT][连接车端] 点击连接 当前VIN=" + currentVin + " (仿真车选 carla-sim-001 连接 CARLA)")
                            var cfg = (typeof vehicleManager !== "undefined" && vehicleManager) ? vehicleManager.lastControlConfig : ({})
                            var brokerUrl = (cfg && (cfg.mqtt_broker_url || cfg["mqtt_broker_url"])) ? (cfg.mqtt_broker_url || cfg["mqtt_broker_url"]) : ((typeof mqttController !== "undefined" && mqttController) ? mqttController.brokerUrl : "")
                            var clientId = (cfg && (cfg.mqtt_client_id || cfg["mqtt_client_id"])) ? (cfg.mqtt_client_id || cfg["mqtt_client_id"]) : ""
                            if (clientId && typeof mqttController !== "undefined" && mqttController)
                                mqttController.clientId = clientId
                            if (brokerUrl && typeof mqttController !== "undefined" && mqttController)
                                mqttController.brokerUrl = brokerUrl
                            if (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected) {
                                // 先发 start_stream，再延迟 25s 拉流，给车端（CARLA Bridge 约 5~25s）启动推流并注册到 ZLM
                                console.log("[CLIENT][连接车端] 环节: MQTT 已连接，发送 start_stream currentVin=" + currentVin + " (若为空车端可能不响应，请先选车 carla-sim-001)")
                                mqttController.requestStreamStart()
                                connectVideoDelayTimer.whepUrl = (typeof vehicleManager !== "undefined" && vehicleManager) ? vehicleManager.lastWhepUrl : ""
                                connectVideoDelayTimer.start()
                                console.log("[CLIENT][连接车端] 环节: 已启动拉流延迟定时器 25s，到时将调用 connectFourStreams")
                                return
                            }
                            if (!brokerUrl || brokerUrl.length === 0) {
                                console.log("[CLIENT][连接车端] ✗ brokerUrl 为空，请先在连接设置中填写 MQTT 地址")
                                openMqttDialogRequested()
                                return
                            }
                            pendingConnectVideo = true
                            console.log("[CLIENT][连接车端] 正在连接 MQTT brokerUrl=" + brokerUrl + " currentVin=" + currentVin + " (选车后 VIN 应由 vehicleManager 已设置)")
                            if (typeof mqttController !== "undefined" && mqttController)
                                mqttController.connectToBroker()
                        }
                    }
                    ToolTip.visible: connectBtnMa.containsMouse
                    ToolTip.text: pendingConnectVideo ? "正在连接 MQTT…" : ((typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected) ? "点击断开视频流并停止车端推流" : ((typeof mqttController !== "undefined" && mqttController && mqttController.isConnected) ? "MQTT 已连接，约 10s 后自动拉流，请稍候…" : "点击连接车端（连 MQTT 并拉取四路视频）"))
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
                    property bool isVideoConnected: (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected)  // 视频流是否已连接
                    property bool buttonEnabled: isVideoConnected  // 按钮是否可用（只有视频流连接时才能点击）
                    property bool remoteControlConfirmed: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.remoteControlEnabled : false  // 车端确认的远驾接管状态
                    property bool hadVideoConnectedBefore: false  // ★ 仅当「曾连接过视频」再断开时才发 remote_control false，避免 connectFourStreams 时 disconnectAll 触发 flood
                    
                    border.color: {
                        if (!buttonEnabled) return "#555555"  // 禁用时灰色边框
                        return remoteControlConfirmed ? colorAccent : colorButtonBorder
                    }
                    color: {
                        if (!buttonEnabled) return "#1A1A1A"  // 禁用时深灰色背景
                        return remoteControlConfirmed ? "#1A2A1A" : colorButtonBg
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
                            return parent.remoteControlConfirmed ? colorAccent : colorTextPrimary
                        }
                        font.pixelSize: 14
                        font.bold: true
                        font.family: chineseFont || font.family
                    }
                    
                    MouseArea {
                        id: remoteControlBtnMa
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: parent.buttonEnabled  // ★ 只有视频流连接时才能点击
                        cursorShape: parent.buttonEnabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor  // 禁用时显示禁止光标
                        onClicked: {
                            // ★ 首行日志：精确定位是否进入点击逻辑
                            var isConn = (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected)
                                                    
                            // Plan 4.1: Ensure we don't attempt actions if MQTT is disconnected
                            if (!isConn) {
                                console.log("[REMOTE_CONTROL][CLICK] ✗ 未发送：MQTT 未连接")
                                return
                            }
                            console.log("[REMOTE_CONTROL][CLICK] >>> 用户点击远驾接管按钮 <<< isConnected=" + isConn + " buttonEnabled=" + parent.buttonEnabled + " remoteControlConfirmed=" + parent.remoteControlConfirmed)
                            if (!parent.buttonEnabled) {
                                console.log("[REMOTE_CONTROL][CLICK] ✗ 未发送：视频流未连接")
                                return
                            }
                            if (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected) {
                                // 如果已经是"远驾已接管"状态，点击则取消接管
                                var newState = !parent.remoteControlConfirmed
                                parent.remoteControlActive = newState
                                console.log("[REMOTE_CONTROL][CLICK] 调用 requestRemoteControl(" + newState + ") topic=vehicle/control")
                                mqttController.requestRemoteControl(newState)
                                if (typeof systemStateMachine !== "undefined" && systemStateMachine && typeof systemStateMachine.fireByName === "function") {
                                    if (newState) systemStateMachine.fireByName("START_SESSION")
                                    else systemStateMachine.fireByName("STOP_SESSION")
                                }
                                console.log("[REMOTE_CONTROL] ========== [QML] 点击远驾接管按钮 ==========")
                                console.log("[REMOTE_CONTROL] 发送指令: enable=" + newState + "（等待车端 vehicle/status 确认）")
                            } else {
                                console.log("[REMOTE_CONTROL][CLICK] ✗ 未发送：MQTT 未连接 isConnected=" + isConn)
                            }
                        }
                    }
                    
                    // ★ 监听车端远驾接管状态变化
                    Connections {
                        target: vehicleStatus
                        ignoreUnknownSignals: true
                        function onRemoteControlEnabledChanged() {
                            var confirmed = (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.remoteControlEnabled : false
                            console.log("[REMOTE_CONTROL] ========== [QML] 车端远驾接管状态变化 ==========")
                            console.log("[REMOTE_CONTROL] 状态确认: " + (confirmed ? "已启用" : "已禁用"))
                            console.log("[REMOTE_CONTROL] 当前 remoteControlConfirmed: " + parent.remoteControlConfirmed)
                            console.log("[REMOTE_CONTROL] 当前 remoteControlActive: " + parent.remoteControlActive)
                            
                            // 同步本地状态（注意：parent 指向 Rectangle，remoteControlActive 是其属性）
                            // remoteControlConfirmed 是计算属性，会自动更新
                            // remoteControlActive 用于跟踪本地状态
                            if (parent) {
                                var oldActive = parent.remoteControlActive
                                parent.remoteControlActive = confirmed
                                console.log("[REMOTE_CONTROL] ✓ 已更新按钮本地状态: remoteControlActive " + oldActive + " -> " + confirmed)
                                console.log("[REMOTE_CONTROL] 更新后 remoteControlConfirmed: " + parent.remoteControlConfirmed)
                                console.log("[REMOTE_CONTROL] 按钮文本应显示: " + (parent.remoteControlConfirmed ? "远驾已接管" : "远驾接管"))
                            } else {
                                console.log("[REMOTE_CONTROL] ⚠ parent 为空，无法更新按钮状态")
                            }
                            console.log("[REMOTE_CONTROL] ========================================")
                        }
                        function onDrivingModeChanged() {
                            var mode = (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.drivingMode : "自驾"
                            console.log("[REMOTE_CONTROL] ========== [QML] 驾驶模式变化 ==========")
                            console.log("[REMOTE_CONTROL] 新驾驶模式: " + mode)
                            console.log("[REMOTE_CONTROL] 当前按钮文本应显示: " + (parent.remoteControlConfirmed ? "远驾已接管" : "远驾接管"))
                            console.log("[REMOTE_CONTROL] ========================================")
                        }
                    }
                    
                    // ★ 监听视频流状态变化：仅当「曾连接过视频」再断开时才发 remote_control false，避免 6s 后 connectFourStreams→disconnectAll 触发大量 false
                    Connections {
                        target: webrtcStreamManager
                        ignoreUnknownSignals: true
                        function onAnyConnectedChanged() {
                            var videoConnected = (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected)
                            if (videoConnected) {
                                remoteControlTakeoverRect.hadVideoConnectedBefore = true
                            } else {
                                if (remoteControlTakeoverRect.hadVideoConnectedBefore) {
                                    remoteControlTakeoverRect.hadVideoConnectedBefore = false
                                    console.log("[REMOTE_CONTROL] ⚠ [QML] 视频流已断开（曾连接过），发送 remote_control false")
                                    if (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected) {
                                        mqttController.requestRemoteControl(false)
                                        if (typeof systemStateMachine !== "undefined" && systemStateMachine && typeof systemStateMachine.fireByName === "function")
                                            systemStateMachine.fireByName("STOP_SESSION")
                                        console.log("[REMOTE_CONTROL] ✓ 已发送远驾接管禁用指令到车端（视频流断开）")
                                    }
                                }
                            }
                            console.log("[REMOTE_CONTROL] [QML] anyConnected=" + videoConnected + "（聚合）远驾按钮" + (videoConnected ? "已启用" : "已禁用") + "；主视图是否出画请看 [Client][VideoFrame]/[Client][UI][Video]")
                        }
                    }
                    
                    ToolTip.visible: remoteControlBtnMa.containsMouse
                    ToolTip.text: {
                        if (!buttonEnabled) {
                            return "视频流未连接，请先连接车辆后再启用远驾接管"
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
                    border.color: colorButtonBorder
                    color: colorButtonBg
                    
                    property string drivingMode: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.drivingMode : "自驾"
                    
                    Text {
                        anchors.centerIn: parent
                        text: parent.drivingMode
                        color: {
                            switch (parent.drivingMode) {
                                case "远驾": return colorAccent
                                case "遥控": return "#FFA500"  // 橙色
                                case "自驾": return colorTextPrimary
                                default: return colorTextPrimary
                            }
                        }
                        font.pixelSize: 13
                        font.bold: true
                        font.family: chineseFont || font.family
                    }
                    
                    // 监听驾驶模式变化
                    Connections {
                        target: vehicleStatus
                        ignoreUnknownSignals: true
                        function onDrivingModeChanged() {
                            var mode = (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.drivingMode : "自驾"
                            console.log("[QML] 驾驶模式更新: " + mode)
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
                    border.color: colorButtonBorder
                    color: {
                        // ★ 根据清扫状态改变颜色：启用时亮起，禁用时暗色
                        var sweepActive = (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.sweepActive : false
                        return sweepActive ? colorAccent : colorButtonBg
                    }
                    
                    property bool sweepActive: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.sweepActive : false
                    
                    Text {
                        anchors.centerIn: parent
                        text: "清扫"
                        color: {
                            if (parent.sweepActive) {
                                return "#FFFFFF"  // 启用时白色文字
                            }
                            return colorTextSecondary  // 禁用时灰色文字
                        }
                        font.pixelSize: 13
                        font.bold: true
                        font.family: chineseFont || font.family
                    }
                    
                    // 监听清扫状态变化
                    Connections {
                        target: vehicleStatus
                        ignoreUnknownSignals: true
                        function onSweepActiveChanged() {
                            var active = (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.sweepActive : false
                            console.log("[SWEEP] [QML] 清扫状态更新: " + (active ? "启用" : "禁用"))
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
                    border.color: colorButtonBorder
                    color: {
                        // ★ 根据刹车状态改变颜色：启用时红色亮起，禁用时暗色
                        var brakeActive = (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.brakeActive : false
                        return brakeActive ? colorDanger : colorButtonBg
                    }
                    
                    property bool brakeActive: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.brakeActive : false
                    
                    Text {
                        anchors.centerIn: parent
                        text: "刹车"
                        color: {
                            if (parent.brakeActive) {
                                return "#FFFFFF"  // 启用时白色文字
                            }
                            return colorTextSecondary  // 禁用时灰色文字
                        }
                        font.pixelSize: 13
                        font.bold: true
                        font.family: chineseFont || font.family
                    }
                    
                    // 监听刹车状态变化
                    Connections {
                        target: vehicleStatus
                        ignoreUnknownSignals: true
                        function onBrakeActiveChanged() {
                            var active = (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.brakeActive : false
                            console.log("[BRAKE] [QML] 刹车状态更新: " + (active ? "启用" : "禁用"))
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
                
                Timer {
                    id: connectVideoDelayTimer
                    interval: 25000
                    repeat: false
                    property string whepUrl: ""
                    onTriggered: {
                        var u = whepUrl || ""
                        console.log("[CLIENT][连接车端] 环节: 25s 延迟到，开始拉流（CARLA/Bridge 约 5~25s 推流就绪）whepUrl=" + (u.length > 60 ? u.substring(0, 60) + "..." : u))
                        if (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager) {
                            webrtcStreamManager.connectFourStreams(u)
                            console.log("[CLIENT][连接车端] 环节: 已调用 connectFourStreams，四路流将向 ZLM 发起 WebRTC 拉流")
                        }
                    }
                }
                Connections {
                    target: mqttController
                    ignoreUnknownSignals: true
                    function onConnectionStatusChanged(connected) {
                        if (connected && drivingInterface.pendingConnectVideo) {
                            drivingInterface.pendingConnectVideo = false
                            if (mqttController) mqttController.requestStreamStart()
                            connectVideoDelayTimer.whepUrl = vehicleManager ? vehicleManager.lastWhepUrl : ""
                            connectVideoDelayTimer.start()
                        }
                    }
                }
                Connections {
                    target: webrtcStreamManager
                    ignoreUnknownSignals: true
                    function onAnyConnectedChanged() {
                        if (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected) {
                            drivingInterface.streamStopped = false
                            var wsm = webrtcStreamManager
                            var f = wsm.frontClient && wsm.frontClient.isConnected
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
                    Text { text: "◀"; color: colorAccent; font.pixelSize: 16 }
                    Text {
                        text: currentGear === "R" ? "倒车模式" : "前进模式"
                        color: colorTextPrimary
                        font.pixelSize: 16
                        font.bold: true
                        font.family: chineseFont || font.family
                    }
                    Text { text: "▶"; color: colorAccent; font.pixelSize: 16 }
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
                    
                    property double rtt: (typeof vehicleStatus !== "undefined" && vehicleStatus && typeof vehicleStatus.networkRtt === "number") ? vehicleStatus.networkRtt : 0
                    property string statusText: rttIndicator.rtt > 300 ? "网络严重" : (rttIndicator.rtt > 150 ? "网络延迟" : "网络正常")
                    
                    border.color: rttIndicator.rtt > 300 ? colorDanger : (rttIndicator.rtt > 150 ? colorWarning : colorAccent)
                    color: rttIndicator.rtt > 300 ? "#331111" : (rttIndicator.rtt > 150 ? "#332200" : colorButtonBg)
                    
                    SequentialAnimation on opacity {
                        running: rttIndicator.rtt > 300
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.6; duration: 500 }
                        NumberAnimation { to: 1.0; duration: 500 }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: rttIndicator.statusText
                        color: rttIndicator.rtt > 300 ? colorDanger : (rttIndicator.rtt > 150 ? colorWarning : colorTextPrimary)
                        font.pixelSize: 13
                        font.bold: true
                        font.family: chineseFont || font.family
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
                            color: colorBorder
                            border.color: colorButtonBorder
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
                    color: colorTextPrimary
                    font.pixelSize: 14
                    font.bold: true
                }
                Text {
                    text: "007°C"
                    color: colorTextSecondary
                    font.pixelSize: 12
                }
            }
        }
        
        // ==================== 主内容三列布局（GridLayout columns:3 强制单行，避免 RowLayout 将右列换行到底部） ====================
        GridLayout {
            id: mainRowLayout
            columns: 3
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumHeight: mainRowAvailH
            rowSpacing: 0
            columnSpacing: mainRowSpacing
            
            // -------------------- 左列：左视图 + 后视图（与右列对称，共用 sideColAllocW） --------------------
            ColumnLayout {
                id: leftColLayout
                Layout.preferredWidth: Math.min(sideColMaxWidth, Math.max(sideColMinWidth, sideColAllocW))
                Layout.minimumWidth: sideColMinWidth
                Layout.maximumWidth: sideColMaxWidth
                Layout.fillWidth: false
                Layout.fillHeight: true
                Layout.minimumHeight: sideColMinHeight
                Layout.maximumHeight: mainRowAvailH
                spacing: 4
                
                VideoPanel {
                    id: leftFrontPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: mainRowAvailH * leftVideoRatio
                    Layout.minimumHeight: sideColTopMinHeight
                    title: "左视图"
                    streamClient: typeof webrtcStreamManager !== "undefined" && webrtcStreamManager ? webrtcStreamManager.leftClient : null
                }
                
                VideoPanel {
                    id: leftRearPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: mainRowAvailH * leftMapRatio
                    Layout.minimumHeight: sideColBottomMinHeight
                    title: "后视图"
                    streamClient: typeof webrtcStreamManager !== "undefined" && webrtcStreamManager ? webrtcStreamManager.rearClient : null
                }
            }
            
            // -------------------- 中列：主视图（fillWidth 铺满剩余空间，左右列对称后中列占 60%） --------------------
            ColumnLayout {
                id: centerColLayout
                Layout.preferredWidth: centerColAllocW
                Layout.minimumWidth: 380
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.maximumHeight: mainRowAvailH
                spacing: 4
                
                // 组件1（同级）：主视图
                Rectangle {
                    id: centerCameraRect
                    Layout.fillWidth: true
                    // 使用 mainRowAvailH 而非直接乘以 drivingInterface.height，保证总和受垂直预算约束
                    Layout.preferredHeight: mainRowAvailH * centerCameraRatio
                    radius: 6
                    color: colorPanel
                    border.color: colorBorderActive
                    border.width: 1
                    
                    Column {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 4
                        
                        Text {
                            text: "主视图"
                            color: colorTextPrimary
                            font.pixelSize: 12
                            font.bold: true
                            font.family: chineseFont || font.family
                        }
                        
                        Rectangle {
                            id: mainCameraView
                            width: parent.width
                            height: parent.height - 24
                            radius: 4
                            color: colorBackground
                            
                            // 视频帧状态跟踪（占位 Canvas/文案是否盖在视频上；渲染器始终参与 update，避免 visible=false 丢首帧）
                            property bool hasVideoFrame: false
                            
                            Timer {
                                id: frontFrameClearTimer
                                interval: 2500
                                repeat: false
                                onTriggered: {
                                    mainCameraView.hasVideoFrame = false
                                    console.log("[Client][UI][Video] 主视图 hasVideoFrame=false（连接断开≥2.5s 后清除，避免 ICE 瞬时 Disconnected 闪黑）")
                                }
                            }
                            
                            // 视频渲染器：始终可见，无帧时 paint 为黑底；占位内容用更高 z 叠在上面
                            VideoRenderer {
                                id: frontVideoRenderer
                                anchors.fill: parent
                                z: 10
                                visible: true
                            }
                            
                            Connections {
                                target: (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager) ? webrtcStreamManager.frontClient : null
                                ignoreUnknownSignals: true
                                function onVideoFrameReady(image) {
                                    frontFrameClearTimer.stop()
                                    var imgSize = image ? ((image.width && image.height) ? (image.width + "x" + image.height) : "unknown") : "null"
                                    var videoLogEnabled = Qt.application.arguments.indexOf("--enable-video-log") >= 0 ||
                                                          (typeof process !== "undefined" && process.env && process.env.ENABLE_VIDEO_FRAME_LOG === "1")
                                    if (videoLogEnabled) {
                                        console.log("[Client][UI][Video] 主视图 onVideoFrameReady size=" + imgSize)
                                    }
                                    if (image && frontVideoRenderer) {
                                        frontVideoRenderer.setFrame(image)
                                        mainCameraView.hasVideoFrame = true
                                        if (videoLogEnabled) {
                                            console.log("[Client][UI][Video] 主视图 setFrame 已调用")
                                        }
                                    } else {
                                        console.warn("[Client][UI][Video] 主视图帧无效或渲染器缺失", image, frontVideoRenderer)
                                        mainCameraView.hasVideoFrame = false
                                    }
                                }
                                function onConnectionStatusChanged(connected) {
                                    console.log("[Client][UI][Video] 主视图 onConnectionStatusChanged connected=" + connected)
                                    if (connected) {
                                        frontFrameClearTimer.stop()
                                    } else {
                                        frontFrameClearTimer.restart()
                                    }
                                }
                            }
                            
                            // 前进/倒车视图绘制（无视频时显示）
                            Canvas {
                                anchors.fill: parent
                                z: 18
                                visible: forwardMode && !mainCameraView.hasVideoFrame  // 只在无视频且前进模式时显示
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)
                                    
                                    // 引导线
                                    ctx.strokeStyle = colorAccent
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
                                visible: !forwardMode && !mainCameraView.hasVideoFrame  // 只在无视频且倒车模式时显示
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)
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
                                visible: !mainCameraView.hasVideoFrame  // 只在无视频时显示
                                text: forwardMode ? "主视图" : "后视辅助"
                                font.pixelSize: 22
                                color: colorBorderActive
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
                                    text: (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.frontClient) ? 
                                          (webrtcStreamManager.frontClient.isConnected ? 
                                           (mainCameraView.hasVideoFrame ? "视频已连接" : "等待视频流...") : 
                                           webrtcStreamManager.frontClient.statusText) : 
                                          "等待连接"
                                    color: (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.frontClient && webrtcStreamManager.frontClient.isConnected && mainCameraView.hasVideoFrame) ? 
                                           colorAccent : colorTextSecondary
                                    font.pixelSize: 12
                                    font.family: chineseFont || font.family
                                }
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
                                    opacity: leftTurnActive ? 1 : 0.45
                                    color: leftTurnActive ? "#1A3A1A" : "#0A0A12"
                                    border.width: 2
                                    border.color: leftTurnActive ? colorAccent : colorBorder
                                    
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 8
                                        Text {
                                            text: "◀"
                                            color: leftTurnActive ? colorAccent : "#606080"
                                            font.pixelSize: 18
                                        }
                                        Text {
                                            text: "左转灯"
                                            color: leftTurnActive ? colorTextPrimary : "#808090"
                                            font.pixelSize: 13
                                            font.family: chineseFont || font.family
                                        }
                                    }
                                    
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            leftTurnActive = !leftTurnActive
                                            if (rightTurnActive) rightTurnActive = false
                                            lightCommandSent("leftTurn", leftTurnActive)
                                        }
                                    }
                                    
                                    // 闪烁动画
                                    SequentialAnimation on opacity {
                                        running: leftTurnActive
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.5; duration: 500 }
                                        NumberAnimation { to: 1.0; duration: 500 }
                                    }
                                }
                                
                                // 右转灯按钮
                                Rectangle {
                                    width: 120; height: 44; radius: 8
                                    opacity: rightTurnActive ? 1 : 0.45
                                    color: rightTurnActive ? "#1A3A1A" : "#0A0A12"
                                    border.width: 2
                                    border.color: rightTurnActive ? colorAccent : colorBorder
                                    
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 8
                                        Text {
                                            text: "右转灯"
                                            color: rightTurnActive ? colorTextPrimary : "#808090"
                                            font.pixelSize: 13
                                            font.family: chineseFont || font.family
                                        }
                                        Text {
                                            text: "▶"
                                            color: rightTurnActive ? colorAccent : "#606080"
                                            font.pixelSize: 18
                                        }
                                    }
                                    
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            rightTurnActive = !rightTurnActive
                                            if (leftTurnActive) leftTurnActive = false
                                            lightCommandSent("rightTurn", rightTurnActive)
                                        }
                                    }
                                    
                                    SequentialAnimation on opacity {
                                        running: rightTurnActive
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
                    Layout.preferredHeight: mainRowAvailH * centerControlsRatio
                    Layout.minimumHeight: minControlHeight
                    radius: 6
                    color: colorPanel
                    border.color: colorBorder
                    border.width: 1
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: controlAreaMargin
                        spacing: controlAreaSpacing
                        
                        // 左侧：车辆灯光控制（用中列分配宽度比例，避免首帧 width 未就绪）
                        ColumnLayout {
                            Layout.preferredWidth: centerColAllocW * controlSideColumnRatio
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
                                
                                ControlButton {
                                    label: "刹车灯"
                                    active: brakeLightActive
                                    onClicked: {
                                        brakeLightActive = !brakeLightActive
                                        sendControlCommand("light", { name: "brakeLight", active: brakeLightActive })
                                    }
                                }
                                ControlButton {
                                    label: "工作灯"
                                    active: workLightActive
                                    onClicked: {
                                        workLightActive = !workLightActive
                                        sendControlCommand("light", { name: "workLight", active: workLightActive })
                                    }
                                }
                                ControlButton {
                                    label: "左转灯"
                                    active: leftTurnActive
                                    onClicked: {
                                        leftTurnActive = !leftTurnActive
                                        if (rightTurnActive) rightTurnActive = false
                                        sendControlCommand("light", { name: "leftTurn", active: leftTurnActive })
                                    }
                                }
                                ControlButton {
                                    label: "右转灯"
                                    active: rightTurnActive
                                    onClicked: {
                                        rightTurnActive = !rightTurnActive
                                        if (leftTurnActive) leftTurnActive = false
                                        sendControlCommand("light", { name: "rightTurn", active: rightTurnActive })
                                    }
                                }
                                ControlButton {
                                    label: "近光"
                                    active: headlightActive
                                    onClicked: {
                                        headlightActive = !headlightActive
                                        sendControlCommand("light", { name: "headlight", active: headlightActive })
                                    }
                                }
                                ControlButton {
                                    label: "警示"
                                    active: warningLightActive
                                    onClicked: {
                                        warningLightActive = !warningLightActive
                                        sendControlCommand("light", { name: "warning", active: warningLightActive })
                                    }
                                }
                            }
                            Item { Layout.fillHeight: true }
                        }
                        
                        // 中间：速度圆圈（用 mainRowAvailH 比例估算高度，避免首帧依赖）
                        Item {
                            id: speedometerContainer
                            Layout.preferredWidth: Math.min(Math.max(0, mainRowAvailH * centerControlsRatio - controlAreaMargin * 2), controlSpeedometerSize)
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
                                border.color: colorBorder
                                
                                // 速度弧线
                                Canvas {
                                    id: speedArc
                                    anchors.fill: parent
                                    
                                    property real speedValue: drivingInterface.displaySpeed
                                    property real steerValue: drivingInterface.displaySteering
                                    
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
                                        var arcColor = speedRatio <= 0.6 ? colorAccent : 
                                                       (speedRatio <= 0.85 ? colorWarning : colorDanger)
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
                                        
                                        ctx.fillStyle = colorTextPrimary
                                        ctx.beginPath()
                                        ctx.moveTo(0, -innerRadius + 15)
                                        ctx.lineTo(-4, 8)
                                        ctx.lineTo(4, 8)
                                        ctx.closePath()
                                        ctx.fill()
                                        
                                        // 中心圆点
                                        ctx.beginPath()
                                        ctx.arc(0, 0, 6, 0, Math.PI * 2)
                                        ctx.fillStyle = colorBorderActive
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
                                        target: drivingInterface
                                        function onVehicleSpeedChanged() { speedArc.requestPaint() }
                                        function onSteeringAngleChanged() { speedArc.requestPaint() }
                                    }
                                }
                                
                                // 速度数值显示
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 0
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: Math.round(drivingInterface.displaySpeed).toString()
                                        color: colorTextPrimary
                                        font.pixelSize: speedometerBg.width * 0.28
                                        font.bold: true
                                    }
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "km/h"
                                        color: colorTextSecondary
                                        font.pixelSize: speedometerBg.width * 0.1
                                    }
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "↻ " + drivingInterface.displaySteering.toFixed(0) + "°"
                                        color: "#70B0FF"
                                        font.pixelSize: speedometerBg.width * 0.09
                                        font.family: chineseFont || font.family
                                        visible: Math.abs(steeringAngle) > 1
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "档位 " + drivingInterface.displayGear
                                        color: "#9CB2DF"
                                        font.pixelSize: speedometerBg.width * 0.09
                                        font.bold: true
                                        font.family: chineseFont || font.family
                                    }
                                }
                            }
                        }
                        
                        // 右侧：清扫功能控制
                        ColumnLayout {
                            Layout.preferredWidth: centerColAllocW * controlSideColumnRatio
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
                                
                                ControlButton {
                                    label: "清扫"
                                    active: sweepActive
                                    onClicked: {
                                        // ★ 检查远驾接管状态
                                        var remoteControlEnabled = false;
                                        if (typeof vehicleStatus !== "undefined" && vehicleStatus) {
                                            remoteControlEnabled = (vehicleStatus.drivingMode === "远驾");
                                        }
                                        if (!remoteControlEnabled) {
                                            console.log("[SWEEP] ⚠ 远驾接管未启用，无法发送清扫命令")
                                            return;
                                        }

                                        sweepActive = !sweepActive
                                        console.log("[SWEEP] 清扫状态: " + (sweepActive ? "启用" : "禁用") + "，准备发送");
                                        
                                        // ★ 使用统一发送接口 (DataChannel 优先)
                                        sendControlCommand("sweep", { name: "sweep", active: sweepActive });
                                        sweepCommandSent("sweep", sweepActive)
                                    }
                                }
                                ControlButton {
                                    label: "洒水"
                                    active: waterSprayActive
                                    onClicked: {
                                        waterSprayActive = !waterSprayActive
                                        sendControlCommand("sweep", { name: "waterSpray", active: waterSprayActive })
                                    }
                                }
                                ControlButton {
                                    label: "吸污"
                                    active: suctionActive
                                    onClicked: {
                                        suctionActive = !suctionActive
                                        sendControlCommand("sweep", { name: "suction", active: suctionActive })
                                    }
                                }
                                ControlButton {
                                    label: "卸料"
                                    active: dumpActive
                                    onClicked: {
                                        dumpActive = !dumpActive
                                        sendControlCommand("sweep", { name: "dump", active: dumpActive })
                                    }
                                }
                                ControlButton {
                                    label: "喇叭"
                                    active: hornActive
                                    onClicked: {
                                        hornActive = !hornActive
                                        sendControlCommand("sweep", { name: "horn", active: hornActive })
                                    }
                                }
                                ControlButton {
                                    label: "灯光"
                                    active: workLampActive
                                    onClicked: {
                                        workLampActive = !workLampActive
                                        sendControlCommand("sweep", { name: "workLamp", active: workLampActive })
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
                    Layout.preferredHeight: mainRowAvailH * centerDashboardRatio
                    Layout.minimumHeight: minDashboardHeight
                    readonly property int cardHeight: Math.max(104, height - dashboardMargin * 2)
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
                        anchors.margins: dashboardMargin
                        spacing: dashboardSpacing
                        
                        // ======================== 档位显示 ========================
                        Item {
                            Layout.preferredWidth: dashboardGearWidth
                            Layout.fillHeight: true
                            
                            DashboardCard {
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
                                                text: drivingInterface.displayGear
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
                                        font.family: chineseFont || font.family
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                }
                            }
                        }
                        
                        Rectangle {
                            Layout.preferredWidth: 1
                            Layout.fillHeight: true
                            Layout.topMargin: dashboardSplitterMargin
                            Layout.bottomMargin: dashboardSplitterMargin
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "transparent" }
                                GradientStop { position: 0.3; color: "#2A3456" }
                                GradientStop { position: 0.7; color: "#2A3456" }
                                GradientStop { position: 1.0; color: "transparent" }
                            }
                        }
                        
                        // ======================== 水箱 + 垃圾箱 ========================
                        ColumnLayout {
                            Layout.preferredWidth: dashboardTankWidth
                            Layout.fillHeight: true
                            spacing: 6
                            
                            Item { Layout.fillHeight: true }

                            Text {
                                text: "箱体状态"
                                color: "#9CB2DF"
                                font.pixelSize: 11
                                font.bold: true
                                font.family: chineseFont || font.family
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
                                border.color: waterTankLevel < 20 ? "#C05A5A" : "#2F4D75"
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
                                        font.family: chineseFont || font.family
                                    }
                                    
                                    Item { Layout.fillWidth: true }
                                    
                                    Text {
                                        id: waterTankPercentText
                                        text: Math.round(waterTankLevel) + "%"
                                        color: waterTankLevel < 20 ? "#FF6B6B" : "#55BBFF"
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
                                    width: parent.width * Math.min(waterTankLevel / 100.0, 1.0)
                                    height: 3
                                    radius: 2
                                    color: waterTankLevel < 20 ? "#FF6B6B" : "#3388FF"
                                    opacity: 0.8
                                    
                                    Behavior on width {
                                        NumberAnimation { duration: 500; easing.type: Easing.OutCubic }
                                    }
                                }
                                
                                Component.onCompleted: {
                                    console.log("[WATER_TANK] [QML] 水箱组件初始化: " + waterTankLevel + "%")
                                }
                            }
                            
                            // 监听水箱水位变化
                            Connections {
                                target: drivingInterface
                                ignoreUnknownSignals: true
                                function onWaterTankLevelChanged() {
                                    var percent = Math.round(drivingInterface.waterTankLevel)
                                    console.log("[WATER_TANK] [QML] 水箱水位变化: " + drivingInterface.waterTankLevel + " -> " + percent + "%")
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
                                border.color: trashBinLevel > 80 ? Qt.rgba(1, 0.4, 0.4, 0.6) : "#2F4D75"
                                border.width: 1
                                
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 4
                                    
                                    // // 垃圾箱图标容器（尺寸调小以适应更窄的布局）
                                    // Rectangle {
                                    //     width: 18; height: 18; radius: 5
                                    //     color: trashBinLevel > 80 ? Qt.rgba(1, 0.3, 0.3, 0.15) : Qt.rgba(0.3, 0.8, 0.5, 0.15)
                                        
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
                                        font.family: chineseFont || font.family
                                    }
                                    
                                    Item { Layout.fillWidth: true }
                                    
                                    Text {
                                        id: trashBinPercentText
                                        text: Math.round(trashBinLevel) + "%"
                                        color: trashBinLevel > 80 ? "#FF6B6B" : "#66DDAA"
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
                                    width: parent.width * Math.min(trashBinLevel / 100.0, 1.0)
                                    height: 3
                                    radius: 2
                                    color: trashBinLevel > 80 ? "#FF6B6B" : "#44BB88"
                                    opacity: 0.8
                                    
                                    Behavior on width {
                                        NumberAnimation { duration: 500; easing.type: Easing.OutCubic }
                                    }
                                }
                                
                                // 高水位脉冲动画
                                SequentialAnimation on border.color {
                                    running: trashBinLevel > 80
                                    loops: Animation.Infinite
                                    ColorAnimation { to: "#FF4444"; duration: 800 }
                                    ColorAnimation { to: "#553333"; duration: 800 }
                                }
                                
                                Component.onCompleted: {
                                    console.log("[TRASH_BIN] [QML] 垃圾箱组件初始化: " + trashBinLevel + "%")
                                }
                            }
                            
                            // 监听垃圾箱水位变化
                            Connections {
                                target: drivingInterface
                                ignoreUnknownSignals: true
                                function onTrashBinLevelChanged() {
                                    var percent = Math.round(drivingInterface.trashBinLevel)
                                    var isHigh = percent > 80
                                    console.log("[TRASH_BIN] [QML] 垃圾箱水位变化: " + drivingInterface.trashBinLevel + " -> " + percent + "% (高水位: " + (isHigh ? "是" : "否") + ")")
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
                            Layout.topMargin: dashboardSplitterMargin
                            Layout.bottomMargin: dashboardSplitterMargin
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "transparent" }
                                GradientStop { position: 0.3; color: "#2A3456" }
                                GradientStop { position: 0.7; color: "#2A3456" }
                                GradientStop { position: 1.0; color: "transparent" }
                            }
                        }
                        
                        // ======================== 速度控制 ========================
                        Item {
                            Layout.preferredWidth: dashboardSpeedWidth
                            Layout.fillHeight: true
                            
                            DashboardCard {
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
                                        font.family: chineseFont || font.family
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
                                                    color: emergencyStopPressed ? "#FF3A3A" : "#2C344C"
                                                }
                                                GradientStop {
                                                    position: 1.0
                                                    color: emergencyStopPressed ? "#C91010" : "#212A40"
                                                }
                                            }
                                        
                                            border.width: 2
                                            border.color: emergencyStopPressed ? "#FF7B7B" : "#4B5F87"
                                        
                                            // 急停图标+文字
                                            Column {
                                                anchors.centerIn: parent
                                                spacing: 1
                                            
                                            Text {
                                                text: "⛔"
                                                font.pixelSize: emergencyStopPressed ? 10 : 12
                                                anchors.horizontalCenter: parent.horizontalCenter
                                            }
                                            Text {
                                                text: "急停"
                                                color: emergencyStopPressed ? "#FFFFFF" : "#CCDDEE"
                                                font.pixelSize: 10
                                                font.bold: true
                                                font.family: chineseFont || font.family
                                                anchors.horizontalCenter: parent.horizontalCenter
                                            }
                                        }
                                        
                                            // 按下光晕
                                            Rectangle {
                                                anchors.fill: parent
                                                radius: parent.radius
                                                color: "transparent"
                                                border.width: emergencyStopPressed ? 3 : 0
                                                border.color: Qt.rgba(1, 0.3, 0.3, 0.5)
                                                visible: emergencyStopPressed
                                            
                                            // 脉冲动画
                                            SequentialAnimation on opacity {
                                                running: emergencyStopPressed
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
                                                console.log("[EMERGENCY_STOP] ========== [QML] 急停按钮点击 ==========")
                                                console.log("[EMERGENCY_STOP] 当前目标速度: " + targetSpeed)
                                                console.log("[EMERGENCY_STOP] 急停按钮状态: " + (emergencyStopPressed ? "已按下" : "未按下"))
                                                
                                                // Plan 4.1: E-Stop is highest priority. Send regardless of remoteControlEnabled state check.
                                                // However, we keep the logging.
                                                var remoteControlEnabled = false;
                                                if (typeof vehicleStatus !== "undefined" && vehicleStatus) {
                                                    remoteControlEnabled = (vehicleStatus.drivingMode === "远驾");
                                                }
                                                
                                                emergencyStopPressed = true
                                                console.log("[EMERGENCY_STOP] 急停按钮状态已更新为: 已按下（红色）")
                                                
                                                // Disable UI immediately to prevent double-click
                                                emergencyStopButton.enabled = false;
                                                
                                                    // ★ 使用统一发送接口 (DataChannel 优先)
                                                    targetSpeed = 0.0
                                                    targetSpeedInput.text = "0.0"
                                                    
                                                    emergencyStopPressed = true
                                                    emergencyStopButton.enabled = false;

                                                    if (typeof systemStateMachine !== "undefined" && systemStateMachine && typeof systemStateMachine.fireByName === "function")
                                                        systemStateMachine.fireByName("EMERGENCY_STOP")
                                                    if (typeof vehicleControl !== "undefined" && vehicleControl && typeof vehicleControl.requestEmergencyStop === "function")
                                                        vehicleControl.requestEmergencyStop()
                                                    else {
                                                        sendControlCommand("brake", { value: 1.0 })
                                                        sendControlCommand("speed", { value: 0.0 })
                                                    }
                                                    console.log("[EMERGENCY_STOP] ✓ 已通过统一接口发送急停命令")
                                                    console.log("[EMERGENCY_STOP] ========================================")
                                                }
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
                                            text: targetSpeed.toFixed(1)
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
                                                console.log("[SPEED] ========== [QML] 目标速度输入完成 ==========")
                                                console.log("[SPEED] 输入文本: " + text)
                                                
                                                var newSpeed = parseFloat(text)
                                                if (isNaN(newSpeed)) {
                                                    console.log("[SPEED] ⚠ 输入无效，恢复为当前值: " + targetSpeed.toFixed(1))
                                                    text = targetSpeed.toFixed(1)
                                                    return
                                                }
                                                newSpeed = Math.max(0.0, Math.min(100.0, newSpeed))
                                                targetSpeed = newSpeed
                                                text = targetSpeed.toFixed(1)
                                                
                                                console.log("[SPEED] 新目标速度: " + targetSpeed.toFixed(1) + " km/h")
                                                
                                                if (targetSpeed > 0.0) {
                                                    emergencyStopPressed = false
                                                    console.log("[SPEED] 目标速度非0，急停按钮状态已重置为: 未按下（正常颜色）")
                                                }
                                                
                                                var remoteControlEnabled = false;
                                                if (typeof vehicleStatus !== "undefined" && vehicleStatus) {
                                                    remoteControlEnabled = (vehicleStatus.drivingMode === "远驾");
                                                }
                                                if (!remoteControlEnabled) {
                                                    console.log("[SPEED] ⚠ 远驾接管未启用，无法发送速度命令")
                                                    console.log("[SPEED] 当前驾驶模式: " + (typeof vehicleStatus !== "undefined" && vehicleStatus ? vehicleStatus.drivingMode : "未知"))
                                                    return;
                                                }
                                                
                                                // ★ 使用统一发送接口 (DataChannel 优先)
                                                sendControlCommand("speed", { value: targetSpeed })
                                                console.log("[SPEED] ✓ 已通过统一接口发送速度命令")
                                                console.log("[SPEED] ========================================")
                                            }
                                            
                                            onTextChanged: {
                                                var num = parseFloat(text)
                                                if (!isNaN(num)) {
                                                    targetSpeed = Math.max(0.0, Math.min(100.0, num))
                                                    if (targetSpeed > 0.0 && emergencyStopPressed) {
                                                        emergencyStopPressed = false
                                                        console.log("[SPEED] [实时更新] 目标速度非0，急停按钮状态已重置")
                                                    }
                                                }
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
                            Layout.topMargin: dashboardSplitterMargin
                            Layout.bottomMargin: dashboardSplitterMargin
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "transparent" }
                                GradientStop { position: 0.3; color: "#2A3456" }
                                GradientStop { position: 0.7; color: "#2A3456" }
                                GradientStop { position: 1.0; color: "transparent" }
                            }
                        }
                        
                        // ======================== 设备状态 ========================
                        Item {
                            Layout.preferredWidth: dashboardStatusWidth
                            Layout.fillHeight: true
                            
                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: "设备状态"
                                    color: "#7B8DB8"
                                    font.pixelSize: 11
                                    font.bold: true
                                    font.family: chineseFont || font.family
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
                                        target: vehicleStatus
                                        ignoreUnknownSignals: true
                                        function onDeviceStatusChanged() {
                                            // 遍历列表并更新状态 (假设 vehicleStatus 有对应的布尔属性)
                                            for (var i = 0; i < parent.deviceList.length; i++) {
                                                var item = parent.deviceList[i];
                                                if (typeof vehicleStatus[item.key] === "boolean") {
                                                    item.status = vehicleStatus[item.key];
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
                                                    font.family: chineseFont || font.family
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
                            Layout.topMargin: dashboardSplitterMargin
                            Layout.bottomMargin: dashboardSplitterMargin
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "transparent" }
                                GradientStop { position: 0.3; color: "#2A3456" }
                                GradientStop { position: 0.7; color: "#2A3456" }
                                GradientStop { position: 1.0; color: "transparent" }
                            }
                        }
                        
                        // ======================== 清扫状态 ========================
                        Item {
                            Layout.preferredWidth: dashboardProgressWidth
                            Layout.fillHeight: true
                            
                            DashboardCard {
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
                                            font.family: chineseFont || font.family
                                        }
                                    }
                                
                                    // 进度环形显示
                                    Item {
                                    width: 52; height: 52
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    
                                    property real progressValue: cleaningTotal > 0 ? (cleaningCurrent / cleaningTotal) : 0
                                    
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
                                            var percent = cleaningTotal > 0 ? Math.round(cleaningCurrent * 100 / cleaningTotal) : 0
                                            return percent + "%"
                                        }
                                        color: "#55CCFF"
                                        font.pixelSize: 14
                                        font.bold: true
                                        font.family: "Consolas"
                                    }
                                    }
                                
                                    Component.onCompleted: {
                                        var cleaningPercent = cleaningTotal > 0 ? Math.round(cleaningCurrent * 100 / cleaningTotal) : 0
                                        console.log("[CLEANING_PROGRESS] [QML] 清扫进度组件初始化: " + cleaningCurrent + " / " + cleaningTotal + " m (" + cleaningPercent + "%)")
                                    }
                                }
                            }
                            
                            // 监听清扫进度变化
                            Connections {
                                target: drivingInterface
                                ignoreUnknownSignals: true
                                function onCleaningCurrentChanged() {
                                    var percent = cleaningTotal > 0 ? Math.round(cleaningCurrent * 100 / cleaningTotal) : 0
                                    console.log("[CLEANING_PROGRESS] [QML] 清扫进度变化: " + cleaningCurrent + " / " + cleaningTotal + " m -> " + percent + "%")
                                    if (typeof cleaningPercentText !== "undefined") {
                                        cleaningPercentText.text = percent + "%"
                                    }
                                    progressCanvas.requestPaint()
                                }
                                function onCleaningTotalChanged() {
                                    var percent = cleaningTotal > 0 ? Math.round(cleaningCurrent * 100 / cleaningTotal) : 0
                                    console.log("[CLEANING_PROGRESS] [QML] 清扫总量变化: " + cleaningTotal + " m (当前: " + cleaningCurrent + " m, 进度: " + percent + "%)")
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
                            Layout.preferredWidth: dashboardGearSelectWidth
                            Layout.fillHeight: true
                            
                            DashboardCard {
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
                                        font.family: chineseFont || font.family
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
                                            
                                            property bool isSelected: modelData === drivingInterface.displayGear
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
                                                onClicked: currentGear = modelData
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
            
            // -------------------- 右列：右视图 + 高精地图（与左列对称，共用 sideColAllocW） --------------------
            ColumnLayout {
                id: rightColMeasurer
                Layout.preferredWidth: Math.min(sideColMaxWidth, Math.max(sideColMinWidth, sideColAllocW))
                Layout.minimumWidth: sideColMinWidth
                Layout.maximumWidth: sideColMaxWidth
                Layout.fillWidth: false
                Layout.fillHeight: true
                Layout.minimumHeight: sideColMinHeight
                Layout.maximumHeight: mainRowAvailH
                spacing: 4

                // 上半部分：右视图（与左视图对称）
                VideoPanel {
                    id: rightViewVideo
                    Layout.fillWidth: true
                    Layout.preferredHeight: mainRowAvailH * leftVideoRatio
                    Layout.minimumHeight: sideColTopMinHeight
                    title: "右视图"
                    streamClient: typeof webrtcStreamManager !== "undefined" && webrtcStreamManager ? webrtcStreamManager.rightClient : null
                    Component.onCompleted: console.log("[Client][UI][Layout] 右视图 VideoPanel onCompleted")
                    onWidthChanged: if (width > 0) console.log("[Client][UI][Layout] 右视图 width=" + Math.round(width))
                    onHeightChanged: if (height > 0) console.log("[Client][UI][Layout] 右视图 height=" + Math.round(height))
                    onVisibleChanged: console.log("[Client][UI][Layout] 右视图 visible=" + visible)
                }

                // 下半部分：高精地图（与后视图对称）
                Rectangle {
                    id: hdMapRect
                    Layout.fillWidth: true
                    Layout.preferredHeight: mainRowAvailH * leftMapRatio
                    Layout.minimumHeight: sideColBottomMinHeight
                    radius: 6
                    color: colorPanel
                    border.color: colorBorderActive
                    border.width: 1
                    Component.onCompleted: console.log("[Client][UI][Layout] 高精地图 Rectangle onCompleted")
                    
                    Connections {
                        target: vehicleStatus
                        ignoreUnknownSignals: true
                        function onMapXChanged() { hdMapCanvas.requestPaint() }
                        function onMapYChanged() { hdMapCanvas.requestPaint() }
                    }
                    onWidthChanged: if (width > 0) console.log("[Client][UI][Layout] 高精地图 width=" + Math.round(width))
                    onHeightChanged: if (height > 0) console.log("[Client][UI][Layout] 高精地图 height=" + Math.round(height))
                    onVisibleChanged: console.log("[Client][UI][Layout] 高精地图 visible=" + visible)

                    Column {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 4

                        Row {
                            width: parent.width

                            Text {
                                text: "高精地图"
                                color: colorTextPrimary
                                font.pixelSize: 12
                                font.bold: true
                                font.family: chineseFont || font.family
                            }

                            Item { width: parent.width - 80; height: 1 }

                            // 缩放控制
                            Row {
                                spacing: 4
                                Rectangle {
                                    width: 20; height: 20; radius: 3
                                    color: colorBorder
                                    Text { anchors.centerIn: parent; text: "−"; color: colorTextPrimary; font.pixelSize: 14 }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                                }
                                Rectangle {
                                    width: 20; height: 20; radius: 3
                                    color: colorBorder
                                    Text { anchors.centerIn: parent; text: "+"; color: colorTextPrimary; font.pixelSize: 14 }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: parent.height - 28
                            radius: 4
                            color: colorBackground

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
                                    
                                    // 车辆位置（三角形）
                                    // ★ 基于车端实时 GPS 坐标 (假设 vehicleStatus 提供 mapX, mapY 归一化坐标 0.0-1.0)
                                    var vx = width * 0.5;
                                    var vy = height * 0.5;
                                    if (typeof vehicleStatus !== "undefined" && vehicleStatus && 
                                        typeof vehicleStatus.mapX === "number" && typeof vehicleStatus.mapY === "number") {
                                        vx = width * Math.max(0, Math.min(1, vehicleStatus.mapX));
                                        vy = height * Math.max(0, Math.min(1, vehicleStatus.mapY));
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
                                    
                                    ctx.strokeStyle = colorTextSecondary
                                    ctx.lineWidth = 2
                                    ctx.beginPath()
                                    ctx.moveTo(width - 40, height - 20)
                                    ctx.lineTo(width - 10, height - 20)
                                    ctx.stroke()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // ==================== 键盘快捷键支持 ====================
    focus: true
    Keys.onPressed: function(event) {
        if (typeof safetyMonitor !== "undefined" && safetyMonitor && typeof safetyMonitor.notifyOperatorActivity === "function")
            safetyMonitor.notifyOperatorActivity()
        switch (event.key) {
            case Qt.Key_P: currentGear = "P"; break
            case Qt.Key_N: currentGear = "N"; break
            case Qt.Key_R: currentGear = "R"; break
            case Qt.Key_D: currentGear = "D"; break
            case Qt.Key_Left: 
                leftTurnActive = !leftTurnActive
                if (rightTurnActive) rightTurnActive = false
                break
            case Qt.Key_Right:
                rightTurnActive = !rightTurnActive
                if (leftTurnActive) leftTurnActive = false
                break
            case Qt.Key_Up:
                targetSpeed = Math.min(100, targetSpeed + 5)
                speedCommandSent(targetSpeed)
                break
            case Qt.Key_Down:
                targetSpeed = Math.max(0, targetSpeed - 5)
                speedCommandSent(targetSpeed)
                break
        }
    }
}
