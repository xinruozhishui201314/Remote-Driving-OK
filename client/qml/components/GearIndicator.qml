/**
 * 挡位指示器。
 */
import QtQuick 2.15
import "../styles"

Item {
    id: root
    required property int gear

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: Qt.rgba(0, 0, 0, 0.5)
        border.color: Qt.rgba(1, 1, 1, 0.3)
        border.width: 1
    }

    Text {
        anchors.centerIn: parent
        text: {
            if (root.gear === -1) return "R"
            if (root.gear === 0)  return "N"
            return root.gear.toString()
        }
        color: {
            if (root.gear === -1) return "#FFC107"
            if (root.gear === 0)  return Theme.colorTextDim
            return Theme.colorGood
        }
        font.pixelSize: 28
        font.bold: true
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 4
        text: "挡位"
        color: Theme.colorTextDim
        font.pixelSize: Theme.fontSmall
    }
}
