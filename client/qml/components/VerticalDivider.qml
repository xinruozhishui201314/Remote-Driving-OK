import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../styles" as ThemeModule

/**
 * 垂直分割线组件
 * 用于面板之间的视觉分隔
 */
Rectangle {
    id: verticalDivider
    width: 1
    Layout.fillHeight: true
    Layout.alignment: Qt.AlignCenter
    color: ThemeModule.Theme.colorBorder
}
