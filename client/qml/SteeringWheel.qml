import QtQuick 2.15
import QtQuick.Controls 2.15
import RemoteDriving 1.0
import "styles" as ThemeModule

/**
 * 方向盘可视化组件
 * 显示方向盘并随角度旋转
 * 统一使用 Theme
 */
Item {
    id: steeringWheel
    
    property real steeringAngle: 0  // 方向盘角度 -100 到 100
    property real wheelSize: Math.min(width, height) * 0.8
    property string chineseFont: AppContext ? AppContext.chineseFont : ""
    
    // 方向盘背景圆
    Rectangle {
        id: wheelCircle
        width: wheelSize
        height: wheelSize
        anchors.centerIn: parent
        radius: width / 2
        color: ThemeModule.Theme.drivingColorPanel
        border.color: ThemeModule.Theme.drivingColorBorderActive
        border.width: 3
        
        // 旋转动画
        rotation: steeringAngle * 1.8
        
        Behavior on rotation {
            NumberAnimation {
                duration: 100
                easing.type: Easing.OutCubic
            }
        }
        
        // 方向盘中心
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.3
            height: parent.height * 0.3
            radius: width / 2
            color: ThemeModule.Theme.drivingColorBorder
            border.color: ThemeModule.Theme.drivingColorBorderActive
            border.width: 2
        }
        
        // 方向盘辐条（3条）
        Repeater {
            model: 3
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.15
                height: parent.width * 0.6
                radius: width / 2
                color: ThemeModule.Theme.drivingColorBorderActive
                rotation: index * 120
                transformOrigin: Item.Center
            }
        }
        
        // 方向盘把手（12个点）
        Repeater {
            model: 12
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.08
                height: width
                radius: width / 2
                color: ThemeModule.Theme.drivingColorBorderActive
                x: parent.width / 2 - width / 2
                y: -parent.height / 2 + width / 2
                rotation: index * 30
                transformOrigin: Item.Center
            }
        }
    }
    
    // 角度指示器
    Text {
        anchors.bottom: wheelCircle.top
        anchors.bottomMargin: 10
        anchors.horizontalCenter: parent.horizontalCenter
        text: steeringAngle.toFixed(0) + "°"
        color: ThemeModule.Theme.drivingColorBorderActive
        font.pixelSize: 16
        font.bold: true
        font.family: steeringWheel.chineseFont || font.family
    }
}
