import ".."
import DrivingFacade 1.0
import QtQuick 2.15
import QtQuick.Layouts 1.15

/**
 * DrivingStageHost — 驾驶主界面壳（Lazy Loader）
 *
 * 输入：
 * - active: bool  为 true 时实例化 DrivingInterface（与全局 componentsReady 同步）
 *
 * 输出：
 * - drivingInterfaceLoader: Loader 别名，供外部 Connections { target: drivingInterfaceLoader.item }
 *
 * 契约：未激活时不创建四路视频绑定，降低启动与切阶段成本。
 */
ColumnLayout {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true

    property bool active: false
    property alias drivingInterfaceLoader: drivingLoader

    spacing: 0
    opacity: active ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 300 } }

    Loader {
        id: drivingLoader
        Layout.fillWidth: true
        Layout.fillHeight: true
        active: root.active
        asynchronous: false
        sourceComponent: Component {
            DrivingInterface {
                anchors.fill: parent
            }
        }
        onLoaded: {
            console.log("[Client][UI][DrivingStageHost] DrivingInterface loaded")
        }
        onActiveChanged: {
            if (!active)
                console.log("[Client][UI][DrivingStageHost] Loader inactive, video bindings released")
        }
    }
}
