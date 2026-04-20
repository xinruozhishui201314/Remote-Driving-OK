import QtQuick 2.15

/**
 * 远驾会话展示状态 + 车端绑定（facade.teleop alias 指向本对象 §3.3–§3.5）。
 * 服务经 facade.appServices 注入（DrivingFacade v3）；禁止在本文件使用 AppContext / RemoteDriving。
 *
 * 根类型用 Item（零尺寸、不可见）：子对象 Connections 依赖父类型的 default property；
 * QtObject 根无 default property，运行期会报 “Cannot assign to non-existent default property”
 * 并导致 main 根为空（exit 93）。见 docs/CLIENT_UI_MODULE_CONTRACT.md §1.2。
 */
Item {
    id: root
    width: 0
    height: 0
    visible: false

    /** DrivingInterface 根 Item；须提供 appServices（含 sendUiCommand） */
    property Item facade: null

    readonly property var svcMqtt: (facade && facade.appServices) ? facade.appServices.mqttController : null
    readonly property var svcVehicleStatus: (facade && facade.appServices) ? facade.appServices.vehicleStatus : null

    property string currentGear: "N"
    readonly property bool forwardMode: currentGear !== "R"
    property real vehicleSpeed: 35
    property real targetSpeed: 0.0
    onTargetSpeedChanged: {
        if (targetSpeed > 0.1 || targetSpeed === 0) {
            console.log("[Client][UI][Teleop][State] targetSpeed changed to: " + targetSpeed.toFixed(1))
        }
    }
    /** W/S 按住时为 true：目标车速不同步车端，避免与键盘抢写 */
    property bool keyboardSpeedAdjustActive: false
    /** A/←/→ 按住时为 true：转向角不同步车端，避免与键盘抢写 */
    property bool keyboardSteerActive: false
    property bool emergencyStopPressed: false
    property real steeringAngle: 0

    property real displaySpeed: (svcMqtt && svcMqtt.mqttBrokerConnected && svcVehicleStatus) ? svcVehicleStatus.speed : vehicleSpeed
    property string displayGear: (svcMqtt && svcMqtt.mqttBrokerConnected && svcVehicleStatus) ? svcVehicleStatus.gear : currentGear
    
    // ★ 修复转向显示映射：从 [-1.0, 1.0] 归一化值转换为 [ -450°, 450° ] 度数显示
    property real displaySteering: {
        var raw = (svcMqtt && svcMqtt.mqttBrokerConnected && svcVehicleStatus) ? svcVehicleStatus.steering : (steeringAngle / 100.0);
        return raw * 450.0;
    }

    property real waterTankLevel: svcVehicleStatus ? svcVehicleStatus.waterTankLevel : 75
    property real trashBinLevel: svcVehicleStatus ? svcVehicleStatus.trashBinLevel : 40
    property int cleaningCurrent: svcVehicleStatus ? svcVehicleStatus.cleaningCurrent : 400
    property int cleaningTotal: svcVehicleStatus ? svcVehicleStatus.cleaningTotal : 500

    property bool leftTurnActive: false
    property bool rightTurnActive: false
    property bool brakeLightActive: false
    property bool workLightActive: false
    property bool headlightActive: true
    property bool warningLightActive: false

    property bool sweepActive: true
    property bool waterSprayActive: false
    property bool suctionActive: false
    property bool dumpActive: false
    property bool hornActive: false
    property bool workLampActive: false

    property bool pendingConnectVideo: false
    property bool streamStopped: false

    property bool isDebugMode: Qt.application.arguments.indexOf("--debug") >= 0
    property int lastDiagTime: 0

    /** 车端反馈车速（km/h），供 HUD 旁路显示；与 VehicleStatus.speed 同源 */
    readonly property real reportedSpeedKmh: (svcVehicleStatus && svcMqtt && svcMqtt.mqttBrokerConnected)
            ? svcVehicleStatus.speed : 0.0

    Connections {
        target: facade && facade.appServices ? facade.appServices.safetyMonitor : null
        enabled: target !== null
        function onEmergencyStopTriggered(reason) {
            console.log("[Client][UI][Teleop] SafetyMonitor: Emergency stop triggered: " + reason)
            root.emergencyStopPressed = true
        }
        function onSafetyStatusChanged(allOk) {
            if (allOk) {
                console.log("[Client][UI][Teleop] SafetyMonitor: Systems recovered, resetting E-Stop state")
                root.emergencyStopPressed = false
            }
        }
    }

    Connections {
        target: facade && facade.appServices ? facade.appServices.vehicleControl : null
        enabled: target !== null
        function onEmergencyStopActivated(reason) {
            console.log("[Client][UI][Teleop] Emergency stop activated: " + reason)
            root.emergencyStopPressed = true
        }
    }

    Connections {
        target: svcVehicleStatus
        enabled: svcVehicleStatus !== null && svcMqtt && svcMqtt.mqttBrokerConnected
        function onSpeedChanged() {
            vehicleSpeed = svcVehicleStatus.speed
            // 目标车速仅由键盘/输入框写入；车端反馈见 reportedSpeedKmh 与仪表盘 displaySpeed
        }
        function onSteeringChanged() {
            if (!keyboardSteerActive)
                steeringAngle = svcVehicleStatus.steering * 100.0
        }
        function onGearChanged() {
            var g = svcVehicleStatus.gear
            if (g === "R" || g === "N" || g === "D" || g === "P")
                currentGear = g
        }
    }

    signal gearChanged(string gear)
    /** 键盘调目标速时发出；UI 面板仍经 facade.teleop 上的转发方法触发根 signal */
    signal speedCommandSent(real speed)

    onCurrentGearChanged: {
        gearChanged(currentGear)

        var remoteOk = (svcVehicleStatus
                && (svcVehicleStatus.remoteControlEnabled === true || svcVehicleStatus.drivingMode === "远驾"))
        if (!remoteOk) {
            console.log("[Client][UI][Gear] ⚠ 远驾未接管：跳过 MQTT 档位指令 gear=" + currentGear
                        + "（请先连接 MQTT + 视频后点击「远驾接管」）")
            return
        }

        var gearValue = 0
        if (currentGear === "R")
            gearValue = -1
        else if (currentGear === "D")
            gearValue = 1
        else if (currentGear === "P")
            gearValue = 2

        console.log("[Client][UI][Gear] 档位变化: " + currentGear + " (数值: " + gearValue + ")，准备发送")

        var svc = facade && facade.appServices
        if (svc && typeof svc.setGear === "function")
            svc.setGear(gearValue)

        if (svc && typeof svc.sendUiCommand === "function")
            svc.sendUiCommand("gear", { value: gearValue })
    }
}
