/**
 * 网络质量状态条。
 */
import QtQuick 2.15
import "../styles"

Item {
    id: root
    required property var networkModel

    Row {
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3

        Repeater {
            model: 5
            Rectangle {
                width:  8
                height: (index + 1) * 4 + 4
                anchors.bottom: parent ? parent.bottom : undefined
                radius: 2
                color: {
                    const score = root.networkModel.qualityScore
                    const threshold = (index + 1) / 5.0
                    if (score >= threshold) {
                        if (score > 0.7) return Theme.colorGood
                        if (score > 0.4) return Theme.colorCaution
                        return Theme.colorWarn
                    }
                    return Qt.rgba(1, 1, 1, 0.2)
                }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 6
            text:  root.networkModel.qualityText
            color: {
                const score = root.networkModel.qualityScore
                if (score > 0.7) return Theme.colorGood
                if (score > 0.4) return Theme.colorCaution
                return Theme.colorWarn
            }
            font.pixelSize: Theme.fontSmall
        }
    }
}
