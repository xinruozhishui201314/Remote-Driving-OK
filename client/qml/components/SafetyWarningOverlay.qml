/**
 * 安全警告叠层（急停红边框 + 警告文本）。
 */
import QtQuick 2.15
import QtQuick.Controls 2.15
import "../styles"

Item {
    id: root
    required property var safetyModel

    // 急停红边框
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.colorEmergency
        border.width: root.safetyModel.emergencyStop ? 6 : 0
        visible: root.safetyModel.emergencyStop

        SequentialAnimation on border.width {
            running: root.safetyModel.emergencyStop
            loops: Animation.Infinite
            NumberAnimation { to: 8; duration: 300 }
            NumberAnimation { to: 4; duration: 300 }
        }
    }

    // 警告横幅
    Rectangle {
        anchors.centerIn: parent
        width: warningText.width + 48
        height: 60
        radius: 8
        color: Qt.rgba(0.8, 0, 0, 0.85)
        visible: root.safetyModel.emergencyStop

        Text {
            id: warningText
            anchors.centerIn: parent
            text:  "⚠ 紧急停车 ⚠"
            color: "white"
            font.bold: true
            font.pixelSize: Theme.fontTitle
        }
    }

    // 普通警告条
    Rectangle {
        anchors.top:        parent.top
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.topMargin:  Theme.topBarHeight + 8
        height: 36
        radius: 4
        color: Qt.rgba(1.0, 0.6, 0, 0.8)
        visible: root.safetyModel.warningCount > 0 && !root.safetyModel.emergencyStop

        Text {
            anchors.centerIn: parent
            text:  "⚠ " + root.safetyModel.lastWarning
            color: "white"
            font.pixelSize: Theme.fontNormal
        }
    }

    // 延迟警告
    Rectangle {
        anchors.top:        parent.top
        anchors.right:      parent.right
        anchors.topMargin:  Theme.topBarHeight + 8
        anchors.rightMargin: 8
        padding: 8
        radius: 4
        color:   Qt.rgba(1, 0.3, 0, 0.8)
        visible: root.safetyModel.latencyWarning && !root.safetyModel.emergencyStop

        Text {
            text:  "延迟过高: " + root.safetyModel.latencyMs.toFixed(0) + "ms"
            color: "white"
            font.pixelSize: Theme.fontSmall
        }
    }
}
