/**
 * 数字速度表组件。
 */
import QtQuick 2.15
import "../styles"

Item {
    id: root
    required property double speed
    property double maxSpeed: 120

    // 背景圆
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color:  "transparent"
        border.color: Qt.rgba(1, 1, 1, 0.3)
        border.width: 2
    }

    // 速度数值
    Column {
        anchors.centerIn: parent
        spacing: 2

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text:  root.speed.toFixed(0)
            color: root.speed > 80 ? Theme.colorWarn : Theme.colorText
            font.pixelSize: 28
            font.bold: true
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text:  "km/h"
            color: Theme.colorTextDim
            font.pixelSize: Theme.fontSmall
        }
    }

    // 速度弧线（Canvas 绘制）
    Canvas {
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const cx = width / 2, cy = height / 2
            const r = Math.min(cx, cy) - 4
            const startAngle = Math.PI * 0.75
            const endAngle   = startAngle + Math.PI * 1.5 * (root.speed / root.maxSpeed)
            ctx.beginPath()
            ctx.arc(cx, cy, r, startAngle, endAngle)
            ctx.strokeStyle = root.speed > 80 ? "#FF5722" : "#4CAF50"
            ctx.lineWidth = 4
            ctx.lineCap = "round"
            ctx.stroke()
        }
        Connections {
            target: root
            function onSpeedChanged() { parent.requestPaint() }
        }
    }
}
