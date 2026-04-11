import ".."
import QtQuick 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0

/**
 * LoginStage — 登录会话壳（仅 UI 容器）
 *
 * 契约：
 * - 不接收业务对象注入；认证与节点检测统一经 AppContext / C++ 上下文。
 * - 由父级 StackLayout 赋予 Layout.fillWidth / Layout.fillHeight。
 */
Item {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true

    LoginPage {
        anchors.fill: parent
    }

    Component.onCompleted: {
        console.log("[Client][UI][LoginStage] ready sessionStage expected="
                    + SessionConstants.stageLogin)
    }
}
