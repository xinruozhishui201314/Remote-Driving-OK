/**
 * 方向盘角度指示器。
 */
import QtQuick 2.15
import "../styles"

Item {
    id: root
    required property double steeringAngle // [-1, 1]

    Canvas {
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const cx = width / 2, cy = height / 2
            const r = Math.min(cx, cy) - 6

            // Outer ring
            ctx.beginPath()
            ctx.arc(cx, cy, r, 0, Math.PI * 2)
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.3).toString()
            ctx.lineWidth = 2
            ctx.stroke()

            // Steering line
            const angle = -Math.PI / 2 + root.steeringAngle * Math.PI * 0.75
            ctx.save()
            ctx.translate(cx, cy)
            ctx.rotate(angle)
            ctx.beginPath()
            ctx.moveTo(-r * 0.6, 0)
            ctx.lineTo(r * 0.6, 0)
            ctx.strokeStyle = root.steeringAngle > 0.1 ? Theme.colorCaution.toString()
                            : root.steeringAngle < -0.1 ? Theme.colorCaution.toString()
                            : Theme.colorGood.toString()
            ctx.lineWidth = 3
            ctx.lineCap = "round"
            ctx.stroke()
            ctx.restore()
        }

        Connections {
            target: root
            function onSteeringAngleChanged() { parent.requestPaint() }
        }
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text:  (root.steeringAngle * 540).toFixed(0) + "°"
        color: Theme.colorTextDim
        font.pixelSize: Theme.fontSmall
    }
}
