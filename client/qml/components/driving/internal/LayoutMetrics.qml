import QtQuick 2.15

/**
 * 远驾主界面布局度量（从 DrivingInterface 抽出；facade 根上仍以 alias 暴露 §3.1）。
 */
QtObject {
    id: root

    /** DrivingInterface 根 Item（width/height） */
    property Item host: null
    /** DrivingLayoutShell 实例（topBarRect、spacing、height） */
    property Item shell: null

    readonly property var layoutConfig: ({
        videoPanelWidthRatio: 0.5,
        videoPanelHeightRatio: 0.6,
        topBarRatio: 1 / 18,
        mainRowRatio: 1 - 1 / 18,
        sideColWidthRatio: 0.20,
        centerColWidthRatio: 0.60,
        leftVideoRatio: 0.58,
        leftMapRatio: 0.40,
        centerCameraRatio: 0.66,
        centerControlsRatio: 0.12,
        centerDashboardRatio: 0.20,
        controlSideColumnRatio: 0.35,
        reconnectIntervalMs: 2000,
        healthCheckIntervalMs: 5000,
        placeholderClearDelayMs: 2500,
        sideColMinWidth: 180,
        sideColMaxWidth: 390,
        sideColTopMinHeight: 100,
        sideColBottomMinHeight: 120,
        controlSpeedometerSize: 140,
        minControlHeight: 110,
        minDashboardHeight: 110,
        controlAreaMargin: 8,
        controlAreaSpacing: 8,
        dashboardMargin: 10,
        dashboardSpacing: 12,
        dashboardSplitterMargin: 8
    })

    readonly property real topBarRatio: layoutConfig.topBarRatio
    readonly property real mainRowRatio: layoutConfig.mainRowRatio
    readonly property real sideColWidthRatio: layoutConfig.sideColWidthRatio
    readonly property real leftColWidthRatio: layoutConfig.sideColWidthRatio
    readonly property real centerColWidthRatio: layoutConfig.centerColWidthRatio
    readonly property real rightColWidthRatio: layoutConfig.sideColWidthRatio
    readonly property real mainRowSpacing: 8
    readonly property real mainRowAvailW: Math.max(0, (host && host.width ? host.width : 1280) - 16 - mainRowSpacing * 2)
    readonly property real sideColAllocW: mainRowAvailW * layoutConfig.sideColWidthRatio
    readonly property real leftColAllocW: sideColAllocW
    readonly property real centerColAllocW: mainRowAvailW * layoutConfig.centerColWidthRatio
    readonly property real rightColAllocW: sideColAllocW
    readonly property int sideColMinWidth: layoutConfig.sideColMinWidth
    readonly property int sideColMaxWidth: layoutConfig.sideColMaxWidth
    readonly property real sideColMinHeight: Math.max(400, mainRowAvailH * 0.9)
    readonly property int sideColTopMinHeight: layoutConfig.sideColTopMinHeight
    readonly property int sideColBottomMinHeight: layoutConfig.sideColBottomMinHeight
    readonly property real leftVideoRatio: layoutConfig.leftVideoRatio
    readonly property real leftMapRatio: layoutConfig.leftMapRatio
    readonly property real centerCameraRatio: layoutConfig.centerCameraRatio
    readonly property real centerControlsRatio: layoutConfig.centerControlsRatio
    readonly property real centerDashboardRatio: layoutConfig.centerDashboardRatio
    readonly property int controlAreaMargin: layoutConfig.controlAreaMargin
    readonly property int controlAreaSpacing: layoutConfig.controlAreaSpacing
    readonly property real controlSideColumnRatio: layoutConfig.controlSideColumnRatio
    readonly property int controlSpeedometerSize: layoutConfig.controlSpeedometerSize
    readonly property int minControlHeight: layoutConfig.minControlHeight
    readonly property int minDashboardHeight: layoutConfig.minDashboardHeight
    readonly property int dashboardMargin: layoutConfig.dashboardMargin
    readonly property int dashboardSpacing: layoutConfig.dashboardSpacing
    readonly property int dashboardGearWidth: 104
    readonly property int dashboardTankWidth: 148
    readonly property int dashboardSpeedWidth: 164
    readonly property int dashboardStatusWidth: 156
    readonly property int dashboardProgressWidth: 158
    readonly property int dashboardGearSelectWidth: 220
    readonly property int dashboardSplitterMargin: layoutConfig.dashboardSplitterMargin
    readonly property real mainRowAvailH: {
        var base = (host && host.height ? host.height : 720)
        var sh = shell
        if (sh && sh.height > 0 && sh.topBarRect && sh.topBarRect.height > 0)
            return Math.max(200, sh.height - sh.topBarRect.height - sh.spacing)
        return Math.max(200, base * mainRowRatio)
    }
}
