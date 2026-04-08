import QtQuick 2.15
import "../styles" as ThemeModule

/**
 * 仪表卡片组件
 * 用于仪表盘区域的卡片显示
 * 从 DrivingInterface.qml 提取，统一使用 Theme
 */
Rectangle {
    id: dashboardCard
    radius: 10
    color: ThemeModule.Theme.drivingColorPanel
    border.width: 1
    border.color: ThemeModule.Theme.drivingColorBorder
    
    gradient: Gradient {
        GradientStop { position: 0.0; color: ThemeModule.Theme.drivingColorPanel }
        GradientStop { position: 1.0; color: ThemeModule.Theme.drivingColorBackground }
    }
    
    // 顶部高光线
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 1
        anchors.rightMargin: 1
        anchors.topMargin: 1
        height: 1
        color: Qt.rgba(0.7, 0.82, 1.0, 0.28)
        radius: 10
    }
}
