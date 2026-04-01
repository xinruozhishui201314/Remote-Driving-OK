import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
// import QtMultimedia 6.0  // 模块不可用，使用替代方案

/**
 * 视频显示组件
 * 显示从 WebRTC 接收的视频流
 */
Rectangle {
    id: videoContainer
    function videoStreamsConnected() {
        if (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected)
            return true
        if (typeof webrtcClient !== "undefined" && webrtcClient && webrtcClient.isConnected)
            return true
        return false
    }
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#0F0F1A" }
        GradientStop { position: 1.0; color: "#1A1A2A" }
    }

    // 视频显示区域
    Item {
        id: videoArea
        anchors.fill: parent
        anchors.margins: 20

        // 实际视频渲染（需要集成 WebRTC 视频帧）
        // 注意：Qt Multimedia 的 VideoOutput 需要 MediaPlayer 或 Camera
        // WebRTC 视频流需要自定义渲染或使用 QVideoSink
        
        Rectangle {
            id: videoPlaceholder
            anchors.fill: parent
            radius: 12
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#1A1A2A" }
                GradientStop { position: 1.0; color: "#0F0F1A" }
            }
            border.color: "#4A90E2"
            border.width: 2
            
            // 简单阴影
            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                z: -1
                radius: parent.radius + 3
                color: "#20000000"
            }
            
            Column {
                anchors.centerIn: parent
                spacing: 20
                
                // 视频图标
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 120
                    height: 120
                    radius: 60
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#2A3A4A" }
                        GradientStop { position: 1.0; color: "#1A2A3A" }
                    }
                    border.color: "#4A90E2"
                    border.width: 2
                    
                    Text {
                        anchors.centerIn: parent
                        text: "📹"
                        font.pixelSize: 60
                    }
                }
                
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: videoContainer.videoStreamsConnected() ? "视频流已连接" : "等待视频流连接..."
                    color: videoContainer.videoStreamsConnected() ? "#50C878" : "#B0B0B0"
                    font.pixelSize: 24
                    font.family: (typeof window !== "undefined" && window.chineseFont) ? window.chineseFont : font.family
                    font.bold: true
                }
                
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: videoContainer.videoStreamsConnected() ? "视频流正常" : "正在连接视频流..."
                    color: "#888888"
                    font.pixelSize: 14
                    font.family: (typeof window !== "undefined" && window.chineseFont) ? window.chineseFont : font.family
                }
            }
        }

        // 视频信息覆盖层（美化样式）
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 20
            width: infoColumn.implicitWidth + 24
            height: infoColumn.implicitHeight + 16
            radius: 8
            color: "#80000000"
            border.color: "#4A90E2"
            border.width: 1
            visible: videoContainer.videoStreamsConnected()
            
            Column {
                id: infoColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                
                Text {
                    text: "📹 视频流信息"
                    color: "#4A90E2"
                    font.pixelSize: 12
                    font.family: (typeof window !== "undefined" && window.chineseFont) ? window.chineseFont : font.family
                    font.bold: true
                }
                
                Text {
                    text: "状态: " + ((typeof webrtcClient !== "undefined" && webrtcClient) ? webrtcClient.statusText : "未连接")
                    color: "#50C878"
                    font.pixelSize: 11
                    font.family: (typeof window !== "undefined" && window.chineseFont) ? window.chineseFont : font.family
                }
            }
        }
    }

    // 全屏按钮（美化样式）
    Button {
        id: fullscreenButton
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 20
        width: 48
        height: 48
        text: window.visibility === Window.FullScreen ? "⛶" : "⛶"
        
        contentItem: Text {
            text: parent.text
            font.pixelSize: 20
            color: parent.enabled ? "#FFFFFF" : "#666666"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        
        background: Rectangle {
            radius: 8
            color: parent.enabled ? (parent.pressed ? "#357ABD" : (parent.hovered ? "#5AA0F2" : "#4A90E2")) : "#2A2A3E"
            border.color: parent.enabled ? "#5AA0F2" : "#444444"
            border.width: 1
            
            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                z: -1
                radius: parent.radius + 2
                color: parent.parent.enabled ? "#30000000" : "#00000000"
            }
        }
        
        onClicked: {
            if (window.visibility === Window.FullScreen) {
                window.showNormal()
            } else {
                window.showFullScreen()
            }
        }
    }
}
