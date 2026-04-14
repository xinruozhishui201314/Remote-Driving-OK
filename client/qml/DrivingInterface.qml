import QtQuick 2.15
import RemoteDriving 1.0
import "components/driving" as Drv
import "components/driving/internal" as DiInt
import "styles" as ThemeModule

/**
 * 远程驾驶主界面 — 布局度量 / 远驾状态 / 键盘 / 诊断见 components/driving/internal/*（DrivingFacade v3：internal 仅 facade.appServices；见 CLIENT_UI_MODULE_CONTRACT.md §2.1 / §3.6）。
 */
Rectangle {
    id: drivingInterface
    color: "#0F0F1A"

    /**
     * DrivingFacade v3：internal/* 仅经 facade.appServices 访问全局服务，禁止在 internal 内 import RemoteDriving / AppContext。
     */
    QtObject {
        id: appServicesBridge
        readonly property var mqttController: AppContext.mqttController
        readonly property var vehicleStatus: AppContext.vehicleStatus
        readonly property var safetyMonitor: AppContext.safetyMonitor
        readonly property var webrtcStreamManager: AppContext.webrtcStreamManager
        readonly property var vehicleControl: AppContext.vehicleControl
        readonly property var vehicleManager: AppContext.vehicleManager
        readonly property var systemStateMachine: AppContext.systemStateMachine
        /** 与 AppContext.videoStreamsConnected 同源（五件套经 facade.appServices 收敛） */
        readonly property bool videoStreamsConnected: AppContext.videoStreamsConnected

        function reportVideoFlickerQmlLayerEvent(where, detail) {
            AppContext.reportVideoFlickerQmlLayerEvent(where, detail)
        }
        /** 与 AppContext.sendUiCommand 同源；internal/* 经 facade.appServices 调用，禁止 import AppContext */
        function sendUiCommand(type, payload) {
            return AppContext.sendUiCommand(type, payload)
        }
    }
    readonly property var appServices: appServicesBridge

    property int _mainCameraHandlerLogCount: 0
    property int _mainCameraHandlerLogCount2: 0

    DiInt.LayoutMetrics {
        id: layoutMetrics
        host: drivingInterface
        shell: drivingLayoutShell
    }

    DiInt.DrivingLayoutDiagnostics {
        id: layoutDiag
        host: drivingInterface
        shell: drivingLayoutShell
        metrics: layoutMetrics
    }

    DiInt.TeleopPresentationState {
        id: teleopState
        facade: drivingInterface
    }

    Connections {
        target: teleopState
        function onGearChanged(gear) {
            drivingInterface.gearChanged(gear)
        }
        function onSpeedCommandSent(speed) {
            drivingInterface.speedCommandSent(speed)
        }
    }

    readonly property alias layoutConfig: layoutMetrics.layoutConfig
    readonly property alias topBarRatio: layoutMetrics.topBarRatio
    readonly property alias mainRowRatio: layoutMetrics.mainRowRatio
    readonly property alias sideColWidthRatio: layoutMetrics.sideColWidthRatio
    readonly property alias leftColWidthRatio: layoutMetrics.leftColWidthRatio
    readonly property alias centerColWidthRatio: layoutMetrics.centerColWidthRatio
    readonly property alias rightColWidthRatio: layoutMetrics.rightColWidthRatio
    readonly property alias mainRowSpacing: layoutMetrics.mainRowSpacing
    readonly property alias mainRowAvailW: layoutMetrics.mainRowAvailW
    readonly property alias sideColAllocW: layoutMetrics.sideColAllocW
    readonly property alias leftColAllocW: layoutMetrics.leftColAllocW
    readonly property alias centerColAllocW: layoutMetrics.centerColAllocW
    readonly property alias rightColAllocW: layoutMetrics.rightColAllocW
    readonly property alias sideColMinWidth: layoutMetrics.sideColMinWidth
    readonly property alias sideColMaxWidth: layoutMetrics.sideColMaxWidth
    readonly property alias sideColMinHeight: layoutMetrics.sideColMinHeight
    readonly property alias sideColTopMinHeight: layoutMetrics.sideColTopMinHeight
    readonly property alias sideColBottomMinHeight: layoutMetrics.sideColBottomMinHeight
    readonly property alias leftVideoRatio: layoutMetrics.leftVideoRatio
    readonly property alias leftMapRatio: layoutMetrics.leftMapRatio
    readonly property alias centerCameraRatio: layoutMetrics.centerCameraRatio
    readonly property alias centerControlsRatio: layoutMetrics.centerControlsRatio
    readonly property alias centerDashboardRatio: layoutMetrics.centerDashboardRatio
    readonly property alias controlAreaMargin: layoutMetrics.controlAreaMargin
    readonly property alias controlAreaSpacing: layoutMetrics.controlAreaSpacing
    readonly property alias controlSideColumnRatio: layoutMetrics.controlSideColumnRatio
    readonly property alias controlSpeedometerSize: layoutMetrics.controlSpeedometerSize
    readonly property alias minControlHeight: layoutMetrics.minControlHeight
    readonly property alias minDashboardHeight: layoutMetrics.minDashboardHeight
    readonly property alias dashboardMargin: layoutMetrics.dashboardMargin
    readonly property alias dashboardSpacing: layoutMetrics.dashboardSpacing
    readonly property alias dashboardGearWidth: layoutMetrics.dashboardGearWidth
    readonly property alias dashboardTankWidth: layoutMetrics.dashboardTankWidth
    readonly property alias dashboardSpeedWidth: layoutMetrics.dashboardSpeedWidth
    readonly property alias dashboardStatusWidth: layoutMetrics.dashboardStatusWidth
    readonly property alias dashboardProgressWidth: layoutMetrics.dashboardProgressWidth
    readonly property alias dashboardGearSelectWidth: layoutMetrics.dashboardGearSelectWidth
    readonly property alias dashboardSplitterMargin: layoutMetrics.dashboardSplitterMargin
    readonly property alias mainRowAvailH: layoutMetrics.mainRowAvailH

    readonly property color colorBackground: ThemeModule.Theme.drivingColorBackground
    readonly property color colorPanel: ThemeModule.Theme.drivingColorPanel
    readonly property color colorBorder: ThemeModule.Theme.drivingColorBorder
    readonly property color colorBorderActive: ThemeModule.Theme.drivingColorBorderActive
    readonly property color colorAccent: ThemeModule.Theme.drivingColorAccent
    readonly property color colorWarning: ThemeModule.Theme.drivingColorWarning
    readonly property color colorDanger: ThemeModule.Theme.drivingColorDanger
    readonly property color colorTextPrimary: ThemeModule.Theme.drivingColorTextPrimary
    readonly property color colorTextSecondary: ThemeModule.Theme.drivingColorTextSecondary
    readonly property color colorButtonBg: ThemeModule.Theme.drivingColorButtonBg
    readonly property color colorButtonBorder: ThemeModule.Theme.drivingColorButtonBorder
    readonly property color colorButtonBgHover: ThemeModule.Theme.drivingColorButtonBgHover
    readonly property color colorButtonBgPressed: ThemeModule.Theme.drivingColorButtonBgPressed

    property string chineseFont: AppContext.chineseFont || ""

    function logLayout(reason) {
        layoutDiag.logLayout(reason)
    }
    function logLayoutFull() {
        layoutDiag.logLayoutFull()
    }

    property bool isLayoutDebugEnabled: layoutDiag.isLayoutDebugEnabled

    Component.onCompleted: {
        layoutDiag.logLayout("onCompleted")
        if (layoutDiag.isLayoutDebugEnabled)
            layoutDiag.startLayoutLogBurst()
        Qt.callLater(function () { drivingInterface.forceActiveFocus() })
    }

    onActiveFocusChanged: {
        if (!AppContext.teleopTraceEnabled)
            return
        console.log("[Client][UI][Teleop][FOCUS] DrivingInterface.activeFocus=" + activeFocus
                    + "（false 时常为 TextField/按钮抢焦点，键盘控车不生效；点一下视频区或空白后再试）")
    }
    onWidthChanged: if (layoutDiag.isLayoutDebugEnabled)
        layoutDiag.logLayout("widthChanged")
    onHeightChanged: if (layoutDiag.isLayoutDebugEnabled)
        layoutDiag.logLayout("heightChanged")

    property alias currentGear: teleopState.currentGear
    readonly property alias forwardMode: teleopState.forwardMode
    property alias vehicleSpeed: teleopState.vehicleSpeed
    property alias targetSpeed: teleopState.targetSpeed
    property alias emergencyStopPressed: teleopState.emergencyStopPressed
    property alias steeringAngle: teleopState.steeringAngle
    readonly property alias displaySpeed: teleopState.displaySpeed
    readonly property alias displayGear: teleopState.displayGear
    readonly property alias displaySteering: teleopState.displaySteering
    readonly property alias waterTankLevel: teleopState.waterTankLevel
    readonly property alias trashBinLevel: teleopState.trashBinLevel
    readonly property alias cleaningCurrent: teleopState.cleaningCurrent
    readonly property alias cleaningTotal: teleopState.cleaningTotal
    property alias leftTurnActive: teleopState.leftTurnActive
    property alias rightTurnActive: teleopState.rightTurnActive
    property alias brakeLightActive: teleopState.brakeLightActive
    property alias workLightActive: teleopState.workLightActive
    property alias headlightActive: teleopState.headlightActive
    property alias warningLightActive: teleopState.warningLightActive
    property alias sweepActive: teleopState.sweepActive
    property alias waterSprayActive: teleopState.waterSprayActive
    property alias suctionActive: teleopState.suctionActive
    property alias dumpActive: teleopState.dumpActive
    property alias hornActive: teleopState.hornActive
    property alias workLampActive: teleopState.workLampActive
    property alias pendingConnectVideo: teleopState.pendingConnectVideo
    property alias streamStopped: teleopState.streamStopped
    readonly property alias isDebugMode: teleopState.isDebugMode
    property alias lastDiagTime: teleopState.lastDiagTime

    function sendControlCommand(type, payload) {
        AppContext.sendUiCommand(type, payload)
    }

    signal gearChanged(string gear)
    signal lightCommandSent(string lightType, bool active)
    signal sweepCommandSent(string sweepType, bool active)
    signal speedCommandSent(real speed)
    signal openMqttDialogRequested()

    QtObject {
        id: teleopBinder
        property alias currentGear: teleopState.currentGear
        property alias forwardMode: teleopState.forwardMode
        property alias vehicleSpeed: teleopState.vehicleSpeed
        property alias targetSpeed: teleopState.targetSpeed
        property alias emergencyStopPressed: teleopState.emergencyStopPressed
        property alias steeringAngle: teleopState.steeringAngle
        property alias displaySpeed: teleopState.displaySpeed
        property alias displayGear: teleopState.displayGear
        property alias displaySteering: teleopState.displaySteering
        property alias waterTankLevel: teleopState.waterTankLevel
        property alias trashBinLevel: teleopState.trashBinLevel
        property alias cleaningCurrent: teleopState.cleaningCurrent
        property alias cleaningTotal: teleopState.cleaningTotal
        property alias leftTurnActive: teleopState.leftTurnActive
        property alias rightTurnActive: teleopState.rightTurnActive
        property alias brakeLightActive: teleopState.brakeLightActive
        property alias workLightActive: teleopState.workLightActive
        property alias headlightActive: teleopState.headlightActive
        property alias warningLightActive: teleopState.warningLightActive
        property alias sweepActive: teleopState.sweepActive
        property alias waterSprayActive: teleopState.waterSprayActive
        property alias suctionActive: teleopState.suctionActive
        property alias dumpActive: teleopState.dumpActive
        property alias hornActive: teleopState.hornActive
        property alias workLampActive: teleopState.workLampActive
        property alias pendingConnectVideo: teleopState.pendingConnectVideo
        property alias streamStopped: teleopState.streamStopped
        property alias isDebugMode: teleopState.isDebugMode
        property alias lastDiagTime: teleopState.lastDiagTime
        readonly property alias reportedSpeedKmh: teleopState.reportedSpeedKmh

        function sendControlCommand(type, payload) {
            drivingInterface.sendControlCommand(type, payload)
        }
        function lightCommandSent(lightType, active) {
            drivingInterface.lightCommandSent(lightType, active)
        }
        function sweepCommandSent(sweepType, active) {
            drivingInterface.sweepCommandSent(sweepType, active)
        }
        function speedCommandSent(speed) {
            drivingInterface.speedCommandSent(speed)
        }
        function openMqttDialogRequested() {
            drivingInterface.openMqttDialogRequested()
        }
    }
    readonly property var teleop: teleopBinder

    DiInt.DrivingVideoDiagnostics {
        id: videoDiag
    }

    function dumpVideoDiagnostics() {
        videoDiag.dump(drivingInterface, teleopState, drivingLayoutShell)
    }

    Drv.DrivingLayoutShell {
        id: drivingLayoutShell
        anchors.fill: parent
        facade: drivingInterface
    }

    DiInt.TeleopKeyboardHandler {
        id: keyHandler
        facade: drivingInterface
        teleop: teleopState
    }

    focus: true
    Keys.onPressed: function (event) {
        keyHandler.handlePressed(event)
    }
    Keys.onReleased: function (event) {
        keyHandler.handleReleased(event)
    }
}
