import ".."
import QtQuick 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0

/**
 * SessionWorkspace — 三阶段互斥工作区（登录 / 选车 / 驾驶）
 *
 * ═══════════════════════════════════════════════════════════════════
 * 对外接口（ApplicationWindow 只与此组件耦合，不直接操作子 Stack 页）
 * ═══════════════════════════════════════════════════════════════════
 *
 * 注入属性：
 *   drivingShellActive : bool
 *       是否进入驾驶壳（与 ApplicationWindow.componentsReady 绑定）。
 *       true → 会话阶段为 SessionConstants.stageDriving，并激活 DrivingStageHost。
 *
 *   appFontFamily : string
 *       主字体族，传给 VehiclePickStage 等需要与窗口字体一致的子壳。
 *
 * 向上转发信号：
 *   openVehicleSelectionRequested()
 *       选车占位页用户点击「打开车辆选择」；父级应连接 vehicleSelectionDialog.open()。
 *
 * 对外只读 / 别名：
 *   sessionStage : int
 *       当前 StackLayout 索引，取值见 SessionConstants（便于日志与自动化断言）。
 *
 *   drivingInterfaceLoader : Loader
 *       驾驶界面 Loader 别名，供父级 Connections 绑定 DrivingInterface 信号。
 *
 * 依赖全局单例：AppContext（不在此重复注入 auth/vehicle 指针，避免双源真理）。
 */
Item {
    id: root

    property bool drivingShellActive: false
    property string appFontFamily: ""

    signal openVehicleSelectionRequested()

    readonly property int sessionStage: {
        if (drivingShellActive)
            return SessionConstants.stageDriving
        var am = AppContext.authManager
        if (am && am.isLoggedIn === true)
            return SessionConstants.stageVehiclePick
        return SessionConstants.stageLogin
    }

    property alias drivingInterfaceLoader: drivingHost.drivingInterfaceLoader

    StatusBar {
        id: statusBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 60
        z: 50
        opacity: root.drivingShellActive ? 1.0 : 0.0
        visible: root.drivingShellActive
        Behavior on opacity { NumberAnimation { duration: 300 } }
    }

    StackLayout {
        id: sessionStack
        anchors.top: root.drivingShellActive ? statusBar.bottom : parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        currentIndex: root.sessionStage

        LoginStage {}

        VehiclePickStage {
            appFontFamily: root.appFontFamily
            onOpenVehicleSelectionRequested: root.openVehicleSelectionRequested()
        }

        DrivingStageHost {
            id: drivingHost
            active: root.drivingShellActive
        }
    }

    Component.onCompleted: {
        var acOk = (typeof AppContext !== "undefined" && AppContext)
        console.log("[Client][UI][SessionWorkspace] onCompleted AppContext=" + (acOk ? "ok" : "MISSING")
                    + " sessionStage=" + sessionStage + " drivingShellActive=" + drivingShellActive)
    }
}
