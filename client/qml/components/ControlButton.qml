import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../styles" as ThemeModule

/**
 * 通用控制按钮组件
 * 支持图标、文字、激活状态
 * 从 DrivingInterface.qml 提取，支持统一样式管理
 */
Rectangle {
    id: controlButton
    
    // ── 属性 ────────────────────────────────────────────────────────────
    property string label: ""
    property bool active: false
    property string buttonKey: ""
    property color activeColor: ThemeModule.Theme.colorAccent
    signal clicked()
    
    // ── 布局 ────────────────────────────────────────────────────────────
    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.minimumWidth: 50
    Layout.minimumHeight: 36
    
    // ── 样式 ────────────────────────────────────────────────────────────
    radius: 8
    border.width: active ? 2 : 1
    border.color: active ? activeColor : (mouseArea.containsMouse ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorButtonBorder)
    
    gradient: Gradient {
        GradientStop {
            position: 0.0
            color: {
                if (controlButton.active) return Qt.lighter(controlButton.activeColor, 1.15)
                if (mouseArea.pressed) return ThemeModule.Theme.colorButtonBgPressed
                if (mouseArea.containsMouse) return ThemeModule.Theme.colorButtonBgHover
                return ThemeModule.Theme.colorButtonBg
            }
        }
        GradientStop {
            position: 1.0
            color: {
                if (controlButton.active) return Qt.darker(controlButton.activeColor, 1.35)
                if (mouseArea.pressed) return Qt.darker(ThemeModule.Theme.colorButtonBgPressed, 1.2)
                if (mouseArea.containsMouse) return Qt.darker(ThemeModule.Theme.colorButtonBgHover, 1.2)
                return Qt.darker(ThemeModule.Theme.colorButtonBg, 1.25)
            }
        }
    }
    
    scale: mouseArea.pressed ? 0.96 : (mouseArea.containsMouse ? 1.02 : 1.0)
    opacity: mouseArea.pressed ? 0.95 : 1.0
    
    // ── 文字 ────────────────────────────────────────────────────────────
    Text {
        anchors.centerIn: parent
        text: controlButton.label
        color: controlButton.active ? "#FFFFFF" : "#DDE6FF"
        font.pixelSize: Math.max(10, Math.min(14, controlButton.width / 2.5))
        font.bold: controlButton.active
        font.family: ThemeModule.Theme.fontFamily
    }
    
    // ── 顶部高光 ────────────────────────────────────────────────────────
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 1
        height: parent.height * 0.35
        radius: 7
        color: controlButton.active ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(1, 1, 1, mouseArea.containsMouse ? 0.12 : 0.06)
    }
    
    // ── 动画 ────────────────────────────────────────────────────────────
    Behavior on scale {
        NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
    }
    Behavior on border.color {
        ColorAnimation { duration: 160 }
    }
    Behavior on opacity {
        NumberAnimation { duration: 90 }
    }
    
    // ── 交互 ────────────────────────────────────────────────────────────
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: controlButton.clicked()
    }
}
