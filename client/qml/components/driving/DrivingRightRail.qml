import "../.."
import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as Components

/**
 * 右列：右视视频 + 高精地图占位。
 */
ColumnLayout {
    id: rightColMeasurer
    required property Item facade
    readonly property alias rightViewVideo: rightViewVideo
    readonly property alias hdMapRect: hdMapRect
    Layout.preferredWidth: Math.min(facade.sideColMaxWidth, Math.max(facade.sideColMinWidth, facade.sideColAllocW))
    Layout.minimumWidth: facade.sideColMinWidth
    Layout.maximumWidth: facade.sideColMaxWidth
    Layout.fillWidth: false
    Layout.fillHeight: true
    Layout.minimumHeight: facade.sideColMinHeight
    Layout.maximumHeight: facade.mainRowAvailH
    spacing: 4

    // 上半部分：右视图（与左视图对称）
    Components.VideoPanel {
        id: rightViewVideo
        Layout.fillWidth: true
        Layout.preferredHeight: facade.mainRowAvailH * facade.leftVideoRatio
        Layout.minimumHeight: facade.sideColTopMinHeight
        title: "右视图"
        streamClient: facade.appServices.webrtcStreamManager ? facade.appServices.webrtcStreamManager.rightClient : null

        // ★★★ 诊断：VideoPanel 实例化后（streamClient 已有值时）立即检查信号接收者 ★★★
        Component.onCompleted: {
            var sc = (streamClient !== null && streamClient !== undefined) ? ("ptr=" + streamClient) : "null/undefined"
            console.warn("[Client][UI][Video] ★★★ VideoPanel[rightViewVideo] onCompleted ★★★"
                        + " streamClient=" + sc
                        + " title=" + title
                        + " ★ 若 streamClient!=null 但 rc=0 → Connections.target 绑定失败")
            var _wsm = facade.appServices.webrtcStreamManager
            if (_wsm) {
                var rcFront = _wsm.getFrontSignalReceiverCount ? _wsm.getFrontSignalReceiverCount() : -1
                var rcRear = _wsm.getRearSignalReceiverCount ? _wsm.getRearSignalReceiverCount() : -1
                var rcLeft = _wsm.getLeftSignalReceiverCount ? _wsm.getLeftSignalReceiverCount() : -1
                var rcRight = _wsm.getRightSignalReceiverCount ? _wsm.getRightSignalReceiverCount() : -1
                console.warn("[Client][UI][Video] ★★★ rightViewVideo onCompleted: 各路 rc: front=" + rcFront + " rear=" + rcRear + " left=" + rcLeft + " right=" + rcRight + " ★★★")
            }
            // ★★★ 诊断：打印完整信号元数据 ★★★
            if (_wsm && _wsm.getStreamSignalMetaInfo) {
                console.log("[Client][UI][Video] rightViewVideo onCompleted 信号元数据:\n" + _wsm.getStreamSignalMetaInfo())
            }
        }
        onWidthChanged: if (width > 0) console.log("[Client][UI][Layout] 右视图 width=" + Math.round(width))
        onHeightChanged: if (height > 0) console.log("[Client][UI][Layout] 右视图 height=" + Math.round(height))
        onVisibleChanged: console.log("[Client][UI][Layout] 右视图 visible=" + visible)
    }

    // 下半部分：高精地图（与后视图对称）
    Rectangle {
        id: hdMapRect
        Layout.fillWidth: true
        Layout.preferredHeight: facade.mainRowAvailH * facade.leftMapRatio
        Layout.minimumHeight: facade.sideColBottomMinHeight
        radius: 6
        color: facade.colorPanel
        border.color: facade.colorBorderActive
        border.width: 1
        Component.onCompleted: console.log("[Client][UI][Layout] 高精地图 Rectangle onCompleted")
        
        Connections {
            target: facade.appServices.vehicleStatus
            ignoreUnknownSignals: true
            function onMapXChanged() { hdMapCanvas.requestPaint() }
            function onMapYChanged() { hdMapCanvas.requestPaint() }
        }
        onWidthChanged: if (width > 0) console.log("[Client][UI][Layout] 高精地图 width=" + Math.round(width))
        onHeightChanged: if (height > 0) console.log("[Client][UI][Layout] 高精地图 height=" + Math.round(height))
        onVisibleChanged: console.log("[Client][UI][Layout] 高精地图 visible=" + visible)

        Column {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4

            Row {
                width: parent.width

                Text {
                    text: "高精地图"
                    color: facade.colorTextPrimary
                    font.pixelSize: 12
                    font.bold: true
                    font.family: facade.chineseFont || font.family
                }

                Item { width: parent.width - 80; height: 1 }

                // 缩放控制
                Row {
                    spacing: 4
                    Rectangle {
                        width: 20; height: 20; radius: 3
                        color: facade.colorBorder
                        Text { anchors.centerIn: parent; text: "−"; color: facade.colorTextPrimary; font.pixelSize: 14 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                    }
                    Rectangle {
                        width: 20; height: 20; radius: 3
                        color: facade.colorBorder
                        Text { anchors.centerIn: parent; text: "+"; color: facade.colorTextPrimary; font.pixelSize: 14 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: parent.height - 28
                radius: 4
                color: facade.colorBackground

                Canvas {
                    id: hdMapCanvas
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.strokeStyle = facade.colorBorderActive
                        ctx.lineWidth = 1
                        
                        // 网格
                        ctx.globalAlpha = 0.3
                        var gridSize = 30
                        for (var x = gridSize; x < width; x += gridSize) {
                            ctx.beginPath()
                            ctx.moveTo(x, 0)
                            ctx.lineTo(x, height)
                            ctx.stroke()
                        }
                        for (var y = gridSize; y < height; y += gridSize) {
                            ctx.beginPath()
                            ctx.moveTo(0, y)
                            ctx.lineTo(width, y)
                            ctx.stroke()
                        }
                        ctx.globalAlpha = 1
                        
                        // 道路
                        ctx.strokeStyle = "#4A90E2"
                        ctx.lineWidth = 3
                        ctx.beginPath()
                        ctx.moveTo(width * 0.1, height * 0.5)
                        ctx.lineTo(width * 0.9, height * 0.5)
                        ctx.stroke()
                        
                        ctx.beginPath()
                        ctx.moveTo(width * 0.5, height * 0.1)
                        ctx.lineTo(width * 0.5, height * 0.9)
                        ctx.stroke()
                        
                        // 车辆位置（三角形）
                        // ★ 基于车端实时 GPS 坐标 (假设 facade.appServices.vehicleStatus 提供 mapX, mapY 归一化坐标 0.0-1.0)
                        var vx = width * 0.5;
                        var vy = height * 0.5;
                        if (facade.appServices.vehicleStatus && 
                            typeof facade.appServices.vehicleStatus.mapX === "number" && typeof facade.appServices.vehicleStatus.mapY === "number") {
                            vx = width * Math.max(0, Math.min(1, facade.appServices.vehicleStatus.mapX));
                            vy = height * Math.max(0, Math.min(1, facade.appServices.vehicleStatus.mapY));
                        }
                        
                        ctx.fillStyle = facade.colorAccent
                        ctx.beginPath()
                        ctx.moveTo(vx, vy - 10)
                        ctx.lineTo(vx - 7, vy + 5)
                        ctx.lineTo(vx + 7, vy + 5)
                        ctx.closePath()
                        ctx.fill()
                        
                        // 比例尺
                        ctx.fillStyle = facade.colorTextSecondary
                        ctx.font = "10px sans-serif"
                        ctx.fillText("100m", width - 35, height - 10)
                        
                        ctx.strokeStyle = facade.colorTextSecondary
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        ctx.moveTo(width - 40, height - 20)
                        ctx.lineTo(width - 10, height - 20)
                        ctx.stroke()
                    }
                }
            }
        }
    }
}
