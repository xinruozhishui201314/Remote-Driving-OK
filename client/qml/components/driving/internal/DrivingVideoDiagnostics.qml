import QtQuick 2.15

/**
 * --debug 下四路视频诊断（原 drivingInterface.dumpVideoDiagnostics）。
 * 流管理器经 facade.appServices.webrtcStreamManager（DrivingFacade v3）。
 */
QtObject {
    function dump(facade, teleop, shell) {
        if (!teleop || !teleop.isDebugMode)
            return
        var now = Date.now()
        if (now - teleop.lastDiagTime < 1000)
            return
        teleop.lastDiagTime = now

        var _wsm = (facade && facade.appServices) ? facade.appServices.webrtcStreamManager : null
        if (!_wsm) {
            console.error("[Client][UI][Video] webrtcStreamManager 不存在")
            return
        }
        console.warn("=== 视频流诊断 ===")
        console.warn("anyConnected=" + _wsm.anyConnected)
        console.warn("front=" + (_wsm.frontClient ? (_wsm.frontClient.isConnected + " / " + (_wsm.frontClient.statusText || "未知")) : "null"))
        console.warn("rear=" + (_wsm.rearClient ? (_wsm.rearClient.isConnected + " / " + (_wsm.rearClient.statusText || "未知")) : "null"))
        console.warn("left=" + (_wsm.leftClient ? (_wsm.leftClient.isConnected + " / " + (_wsm.leftClient.statusText || "未知")) : "null"))
        console.warn("right=" + (_wsm.rightClient ? (_wsm.rightClient.isConnected + " / " + (_wsm.rightClient.statusText || "未知")) : "null"))
        if (_wsm.getQmlSignalReceiverCount) {
            var rc = _wsm.getQmlSignalReceiverCount()
            console.warn("qmlSignalRc=" + rc + " (0=QML未连videoFrameReady)")
            if (rc === 0)
                console.error("[Client][UI][Video] ★★★ FATAL: qmlSignalRc=0！视频帧信号无法到达 QML！检查 Connections.target 绑定 ★★★")
        }
        if (_wsm.getStreamDebugInfo)
            console.log("C++ StreamManager: " + _wsm.getStreamDebugInfo())

        var leftCol = shell ? shell.leftColLayout : null
        var rightCol = shell ? shell.rightColMeasurer : null
        var centerCol = shell ? shell.centerColLayout : null
        var leftFrontPanel = leftCol ? leftCol.leftFrontPanel : null
        var leftRearPanel = leftCol ? leftCol.leftRearPanel : null
        var rightViewVideo = rightCol ? rightCol.rightViewVideo : null
        var mainCameraView = centerCol ? centerCol.mainCameraView : null
        if (typeof leftFrontPanel !== "undefined" && leftFrontPanel)
            console.warn("leftFrontPanel streamClient=" + (leftFrontPanel.streamClient ? "ok" : "null"))
        if (typeof leftRearPanel !== "undefined" && leftRearPanel)
            console.warn("leftRearPanel streamClient=" + (leftRearPanel.streamClient ? "ok" : "null"))
        if (typeof rightViewVideo !== "undefined" && rightViewVideo)
            console.warn("rightViewVideo streamClient=" + (rightViewVideo.streamClient ? "ok" : "null"))
        if (typeof mainCameraView !== "undefined" && mainCameraView)
            console.warn("mainCameraView streamClient=" + (mainCameraView.streamClient ? "ok" : "null"))
        console.warn("=== 诊断结束 ===")
    }
}
