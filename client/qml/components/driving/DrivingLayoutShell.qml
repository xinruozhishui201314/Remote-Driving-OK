import QtQuick 2.15
import QtQuick.Layouts 1.15

/**
 * 布局壳：顶栏 HUD + 三列主行（左视频带 | 中列 | 右视频带）。
 * 子组件仅接收 facade（DrivingInterface 根 item）；业务服务走 facade.appServices（见 CLIENT_UI_MODULE_CONTRACT §3.6）。
 */
ColumnLayout {
    id: rootColumnLayout
    anchors.fill: parent
    anchors.margins: 4
    spacing: 4

    required property Item facade

    DrivingTopChrome {
        id: topBarChrome
        facade: rootColumnLayout.facade
        Layout.fillWidth: true
        Layout.preferredHeight: 54
        Layout.minimumHeight: 48
    }

    GridLayout {
        id: mainRowLayout
        columns: 3
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.maximumHeight: facade.mainRowAvailH
        rowSpacing: 0
        columnSpacing: facade.mainRowSpacing

        DrivingLeftRail {
            id: leftColLayout
            facade: rootColumnLayout.facade
        }
        DrivingCenterColumn {
            id: centerColLayout
            facade: rootColumnLayout.facade
        }
        DrivingRightRail {
            id: rightColMeasurer
            facade: rootColumnLayout.facade
        }
    }

    readonly property Item topBarRect: topBarChrome
    readonly property alias mainRowLayout: mainRowLayout
    readonly property alias leftColLayout: leftColLayout
    readonly property alias centerColLayout: centerColLayout
    readonly property alias rightColMeasurer: rightColMeasurer
}
