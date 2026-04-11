import ".."
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "../styles" as ThemeModule

/**
 * VehiclePickStage — 已登录、尚未进入驾驶壳时的选车引导占位
 *
 * 输入（由 SessionWorkspace 注入）：
 * - appFontFamily: string  主字体族，与 ApplicationWindow 一致
 *
 * 输出：
 * - openVehicleSelectionRequested()  用户请求打开车辆选择对话框（由父级连接 Popup.open）
 *
 * 不直接依赖具体 Popup id，保持壳层与对话框解耦。
 */
Rectangle {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true

    property string appFontFamily: ""

    signal openVehicleSelectionRequested()

    gradient: Gradient {
        GradientStop { position: 0.0; color: ThemeModule.Theme.colorBackground }
        GradientStop { position: 0.55; color: ThemeModule.Theme.colorSurface }
        GradientStop { position: 1.0; color: ThemeModule.Theme.colorBackground }
    }

    onVisibleChanged: {
        if (visible) {
            var am = AppContext.authManager
            var u = am && am.username ? String(am.username) : ""
            console.log("[Client][UI][VehiclePickStage] visible username=" + u)
        }
    }

    Component.onCompleted: {
        var acOk = (typeof AppContext !== "undefined" && AppContext)
        console.log("[Client][UI][VehiclePickStage] onCompleted AppContext=" + (acOk ? "ok" : "MISSING"))
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(520, parent.width * 0.88)
        spacing: 20

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: placeholderCardColumn.implicitHeight + 48
            radius: 12
            color: ThemeModule.Theme.colorSurface
            border.width: 1
            border.color: ThemeModule.Theme.colorBorderActive

            Column {
                id: placeholderCardColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 24
                spacing: 16

                Text {
                    width: parent.width
                    text: "请选择车辆"
                    font.pixelSize: 22
                    font.bold: true
                    font.family: root.appFontFamily || font.family
                    color: ThemeModule.Theme.colorTextPrimary
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    visible: AppContext.authManager
                             && AppContext.authManager.username
                             && String(AppContext.authManager.username).length > 0
                    text: AppContext.authManager ? ("当前账号：" + AppContext.authManager.username) : ""
                    font.pixelSize: 13
                    font.family: root.appFontFamily || font.family
                    color: ThemeModule.Theme.colorTextSecondary
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: "进入驾驶界面前需要选择一辆已授权车辆。若车辆选择窗口已关闭，可点击下方按钮重新打开。"
                    font.pixelSize: 13
                    font.family: root.appFontFamily || font.family
                    color: ThemeModule.Theme.colorTextSecondary
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "打开车辆选择"
                    font.family: root.appFontFamily || font.family
                    highlighted: true
                    onClicked: {
                        console.log("[Client][UI][VehiclePickStage] user requested vehicle selection dialog")
                        root.openVehicleSelectionRequested()
                    }
                }
            }
        }
    }
}
