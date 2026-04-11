import QtQuick 2.15

/**
 * 远驾会话展示状态 + 车端绑定（facade.teleop alias 指向本对象 §3.3–§3.5）。
 * 服务经 facade.appServices 注入（DrivingFacade v3）；禁止在本文件使用 AppContext / RemoteDriving。
 */
QtObject {
    id: root

    /** DrivingInterface 根 Item；须提供 appServices（含 sendUiCommand） */
    property Item facade: null

    readonly property var svcMqtt: (facade && facade.appServices) ? facade.appServices.mqttController : null
    readonly property var svcVehicleStatus: (facade && facade.appServices) ? facade.appServices.vehicleStatus : null

    property string currentGear: "N"
    readonly property bool forwardMode: currentGear !== "R"
    property real vehicleSpeed: 35
    property real targetSpeed: 0.0
    property bool emergencyStopPressed: false
    property real steeringAngle: 0

    property real displaySpeed: (svcMqtt && svcMqtt.isConnected && svcVehicleStatus) ? svcVehicleStatus.speed : vehicleSpeed
    property string displayGear: (svcMqtt && svcMqtt.isConnected && svcVehicleStatus) ? svcVehicleStatus.gear : currentGear
    property real displaySteering: (svcMqtt && svcMqtt.isConnected && svcVehicleStatus) ? svcVehicleStatus.steering : steeringAngle

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

    signal gearChanged(string gear)
    /** 键盘调目标速时发出；UI 面板仍经 facade.teleop 上的转发方法触发根 signal */
    signal speedCommandSent(real speed)

    onCurrentGearChanged: {
        gearChanged(currentGear)

        var remoteControlEnabled = false
        if (svcVehicleStatus)
            remoteControlEnabled = (svcVehicleStatus.drivingMode === "远驾")
        if (!remoteControlEnabled) {
            console.log("[Client][UI][Gear] ⚠ 远驾接管未启用，无法发送档位命令")
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
        if (svc && typeof svc.sendUiCommand === "function")
            svc.sendUiCommand("gear", { value: gearValue })
    }
}
