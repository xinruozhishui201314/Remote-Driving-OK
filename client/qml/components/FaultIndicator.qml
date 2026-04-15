import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "../styles" as ThemeModule

/**
 * 故障码显示组件
 * 根据 Google Material Design 规范：
 * 1. 使用高对比度颜色区分严重级别
 * 2. 悬停展示详细信息（Progressive Disclosure）
 * 3. 紧凑布局，不占用过多状态栏空间
 */
Item {
    id: root
    implicitWidth: layout.implicitWidth
    implicitHeight: 30

    RowLayout {
        id: layout
        anchors.fill: parent
        spacing: 8

        Repeater {
            model: faultManager ? faultManager.activeCodes : []
            
            delegate: Rectangle {
                id: faultBadge
                property var info: faultManager.getFaultInfo(modelData)
                
                implicitWidth: codeText.implicitWidth + 12
                height: 22
                radius: 4
                color: {
                    if (!info) return ThemeModule.Theme.colorTextDim
                    switch (info.severity) {
                        case 0: return "#2196F3" // INFO - Blue
                        case 1: return ThemeModule.Theme.colorCaution // WARN - Orange
                        case 2: return ThemeModule.Theme.colorDanger // ERROR - Red
                        case 3: return "#B00020" // CRITICAL - Deep Red
                        default: return ThemeModule.Theme.colorTextDim
                    }
                }
                
                Text {
                    id: codeText
                    anchors.centerIn: parent
                    text: modelData
                    color: "white"
                    font.pixelSize: 11
                    font.bold: true
                }

                // 悬停交互层
                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                }

                // 详细信息弹出框 (Google Style Tooltip/Info Card)
                ToolTip {
                    id: detailTooltip
                    visible: mouseArea.containsMouse
                    delay: 300
                    timeout: 5000
                    
                    contentItem: ColumnLayout {
                        spacing: 4
                        Text {
                            text: info ? info.name : "未知故障"
                            color: "white"
                            font.bold: true
                            font.pixelSize: 14
                            font.family: ThemeModule.Theme.chineseFont
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: "white"
                            opacity: 0.2
                        }
                        Text {
                            text: info ? info.message : "无详细描述"
                            color: "#E0E0E0"
                            font.pixelSize: 12
                            Layout.maximumWidth: 250
                            wrapMode: Text.WordWrap
                            font.family: ThemeModule.Theme.chineseFont
                        }
                        Text {
                            visible: info && info.recommendedAction !== ""
                            text: "建议操作: " + (info ? info.recommendedAction : "")
                            color: "#81C784" // Light Green for actions
                            font.pixelSize: 12
                            font.italic: true
                            Layout.maximumWidth: 250
                            wrapMode: Text.WordWrap
                            font.family: ThemeModule.Theme.chineseFont
                        }
                        
                        Button {
                            text: "忽略"
                            Layout.alignment: Qt.AlignRight
                            onClicked: faultManager.clearFault(modelData)
                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: 10
                                color: "white"
                            }
                            background: Rectangle {
                                color: parent.hovered ? "#444" : "#222"
                                radius: 4
                            }
                        }
                    }

                    background: Rectangle {
                        color: "#323232"
                        radius: 8
                        border.color: faultBadge.color
                        border.width: 1
                    }
                }
                
                // 呼吸动画（如果是 ERROR 或 CRITICAL）
                SequentialAnimation on opacity {
                    running: info && info.severity >= 2
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.6; duration: 800; easing.type: Easing.InOutQuad }
                    NumberAnimation { from: 0.6; to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
                }
            }
        }
        
        // 当没有故障时显示的占位符（可选）
        Text {
            visible: !faultManager || !faultManager.hasFaults
            text: "系统正常"
            color: ThemeModule.Theme.colorGood
            font.pixelSize: 12
            font.family: ThemeModule.Theme.chineseFont
        }
    }
}
