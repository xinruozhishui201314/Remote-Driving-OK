import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as Components

/**
 * 左列视频带（左视 + 后视）。
 * 布局/主题经 facade.*；流对象经 facade.appServices.webrtcStreamManager（DrivingFacade v3）。
 */
// -------------------- 左列：左视图 + 后视图（与右列对称，共用 facade.sideColAllocW） --------------------
ColumnLayout {
    id: leftColLayout
    required property Item facade
    readonly property alias leftFrontPanel: leftFrontPanel
    readonly property alias leftRearPanel: leftRearPanel
    Layout.preferredWidth: Math.min(facade.sideColMaxWidth, Math.max(facade.sideColMinWidth, facade.sideColAllocW))
    Layout.minimumWidth: facade.sideColMinWidth
    Layout.maximumWidth: facade.sideColMaxWidth
    Layout.fillWidth: false
    Layout.fillHeight: true
    Layout.minimumHeight: facade.sideColMinHeight
    Layout.maximumHeight: facade.mainRowAvailH
    spacing: 4
    
    Components.VideoPanel {
        id: leftFrontPanel
        facade: leftColLayout.facade
        Layout.fillWidth: true
        Layout.preferredHeight: facade.mainRowAvailH * facade.leftVideoRatio
        Layout.minimumHeight: facade.sideColTopMinHeight
        title: "左视图"
        streamClient: facade.appServices.webrtcStreamManager ? facade.appServices.webrtcStreamManager.leftClient : null
        // ★★★ 诊断：VideoPanel 创建后立即验证 QML 信号连接 ★★★
        // 若此日志出现但 onVideoFrameReady 不触发 → streamClient 连接时序问题
        // 若 webrtcStreamManager.getQmlSignalReceiverCount() 返回 0 → QML 根本没连到信号
        Component.onCompleted: {
            console.log("[Client][UI][Video] ★★★ VideoPanel[leftFrontPanel] onCompleted ★★★")
            console.log("[Client][UI][Video] streamClient=" + streamClient + " ★ 对比 Connections.target 日志")
            var _wsm = facade.appServices.webrtcStreamManager
            // 诊断：调用 C++ getQmlSignalReceiverCount() 验证信号接收者
            if (_wsm && _wsm.getQmlSignalReceiverCount) {
                var rc = _wsm.getQmlSignalReceiverCount()
                console.log("[Client][UI][Video] ★★★ getQmlSignalReceiverCount()=" + rc + " ★★★ (0=QML未连，>0=有QML连接) ★★★")
            }
            // 诊断：打印 webrtcStreamManager.frontClient/rearClient 等属性是否可访问
            if (_wsm && _wsm.getStreamDebugInfo) {
                console.log("[Client][UI][Video] ★★★ StreamManager Debug ★★★")
                console.log(_wsm.getStreamDebugInfo())
            }
            // ★★★ 诊断：打印完整信号元数据（用于确认 QML 连接的是哪个 signal 重载）★★★
            if (_wsm && _wsm.getStreamSignalMetaInfo) {
                console.log("[Client][UI][Video] leftFrontPanel onCompleted 信号元数据:\n" + _wsm.getStreamSignalMetaInfo())
            }
        }
    }

    Components.VideoPanel {
        id: leftRearPanel
        facade: leftColLayout.facade
        Layout.fillWidth: true
        Layout.preferredHeight: facade.mainRowAvailH * facade.leftMapRatio
        Layout.minimumHeight: facade.sideColBottomMinHeight
        title: "后视图"
        streamClient: facade.appServices.webrtcStreamManager ? facade.appServices.webrtcStreamManager.rearClient : null

        // ★★★ 新增：与 leftFrontPanel 对称，创建时验证信号连接 ★★★
        Component.onCompleted: {
            console.log("[Client][UI][Video] ★★★ VideoPanel[leftRearPanel] onCompleted ★★★")
            console.log("[Client][UI][Video] leftRearPanel streamClient=" + streamClient
                        + " rearClient=" + (facade.appServices.webrtcStreamManager && facade.appServices.webrtcStreamManager.rearClient ? "ok" : "null"))
            var _wsm = facade.appServices.webrtcStreamManager
            if (_wsm && _wsm.getQmlSignalReceiverCount) {
                var rc = _wsm.getQmlSignalReceiverCount()
                console.log("[Client][UI][Video] ★★★ leftRearPanel onCompleted: getQmlSignalReceiverCount()=" + rc + " ★★★")
                if (rc === 0) {
                    console.error("[Client][UI][Video] ★★★ FATAL: leftRearPanel rc=0！信号无法到达！检查 Connections.target 绑定 ★★★")
                }
            }
            if (_wsm && _wsm.getStreamDebugInfo) {
                console.log("[Client][UI][Video] leftRearPanel C++ StreamManager: " + _wsm.getStreamDebugInfo())
            }
            // ★★★ 诊断：打印完整信号元数据 ★★★
            if (_wsm && _wsm.getStreamSignalMetaInfo) {
                console.log("[Client][UI][Video] leftRearPanel onCompleted 信号元数据:\n" + _wsm.getStreamSignalMetaInfo())
            }
        }
    }
}
