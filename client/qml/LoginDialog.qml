import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick 2.15 as QtQuick2

/**
 * 登录对话框
 */
    Popup {
    id: loginDialog
    width: 450
    height: 520
    modal: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: parent
    
    // 弹出动画
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 300 }
        NumberAnimation { property: "scale"; from: 0.9; to: 1.0; duration: 300; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 200 }
        NumberAnimation { property: "scale"; from: 1.0; to: 0.9; duration: 200 }
    }
    
    // 登录状态（必须在顶层定义，以便子组件访问）
    property bool isLoggingIn: false
    // 密码是否明文显示
    property bool passwordVisible: false
    // 开发测试模式：为测试账号预填充（仅开发/测试环境）
    property bool devTestMode: true  // 默认开启，方便开发测试

    // 中文字体（从主窗口继承或使用默认）
    property string chineseFont: {
        if (typeof window !== "undefined" && window.chineseFont) {
            return window.chineseFont
        }
        var fonts = ["WenQuanYi Zen Hei", "WenQuanYi Micro Hei", "Noto Sans CJK SC"]
        var availableFonts = Qt.fontFamilies()
        for (var i = 0; i < fonts.length; i++) {
            if (availableFonts.indexOf(fonts[i]) !== -1) {
                return fonts[i]
            }
        }
        return ""
    }

    // 背景遮罩（模糊效果）
    Rectangle {
        anchors.fill: parent
        color: "#80000000"
    }
    
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#2A2A3E" }
            GradientStop { position: 1.0; color: "#1E1E2E" }
        }
        border.color: "#4A90E2"
        border.width: 2
        radius: 12
        
        // 简单阴影效果（使用多个半透明矩形）
        Rectangle {
            anchors.fill: parent
            anchors.margins: -5
            z: -1
            radius: parent.radius + 5
            color: "#20000000"
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            z: -1
            radius: parent.radius + 3
            color: "#30000000"
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 40
            spacing: 24

            // 标题区域（带图标效果）
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 10
                
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 60
                    height: 60
                    radius: 30
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#4A90E2" }
                        GradientStop { position: 1.0; color: "#357ABD" }
                    }
                    
                    Image {
                        anchors.centerIn: parent
                        width: 32
                        height: 32
                        source: "icon/vehicle.svg"
                        fillMode: Image.PreserveAspectFit
                    }
                }
                
                Text {
                    text: "远程驾驶客户端"
                    color: "#FFFFFF"
                    font.pixelSize: 24
                    font.family: loginDialog.chineseFont || font.family
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }
                
                Text {
                    text: "登录您的账户"
                    color: "#B0B0B0"
                    font.pixelSize: 14
                    font.family: loginDialog.chineseFont || font.family
                    Layout.alignment: Qt.AlignHCenter
                }
            }

                // 服务器地址
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5

                Text {
                    text: "服务器地址:"
                    color: "#ffffff"
                    font.pixelSize: 12
                    font.family: loginDialog.chineseFont || font.family
                }

                TextField {
                    id: serverUrlField
                    Layout.fillWidth: true
                    placeholderText: "http://192.168.1.100:8080"
                    // 容器内运行可设置 DEFAULT_SERVER_URL=http://backend:8080，宿主机不设则用 localhost:8081
                    text: (typeof defaultServerUrlFromEnv !== 'undefined' && defaultServerUrlFromEnv) ? defaultServerUrlFromEnv : "http://localhost:8081"
                    color: "#FFFFFF"
                    placeholderTextColor: "#666666"
                    selectByMouse: true
                    background: Rectangle {
                        color: serverUrlField.focus ? "#3A3A4E" : "#2A2A3E"
                        border.color: serverUrlField.focus ? "#4A90E2" : "#444444"
                        border.width: serverUrlField.focus ? 2 : 1
                        radius: 6
                        Behavior on color { ColorAnimation { duration: 200 } }
                        Behavior on border.color { ColorAnimation { duration: 200 } }
                    }
                }

                // 开发测试模式说明（仅开发/测试环境可见）
                Text {
                    text: "注意：开发测试模式下点击右侧 [TEST] 按钮可自动填充测试账号"
                    color: "#FFB84D"
                    font.pixelSize: 11
                    font.family: loginDialog.chineseFont || font.family
                    visible: loginDialog.devTestMode
                    Layout.fillWidth: true
                }
            }

            // 开发测试：[TEST] 按钮预填充测试账号（仅开发/测试环境可见）
            RowLayout {
                Layout.fillWidth: true
                spacing: 5
                Button {
                    text: "开发测试：[TEST] 填充测试账号"
                    font.pixelSize: 10
                    visible: loginDialog.devTestMode
                    onClicked: {
                        usernameField.text = "e2e-test"
                        passwordField.text = "e2e-test-password"
                        console.log("[Client][UI] 开发测试：已填充测试账号 e2e-test")
                        if (usernameField.text.length > 0 && passwordField.text.length > 0) loginButton.clicked()
                    }
                }
            }

            // 用户名
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5

                Text {
                    text: "用户名:"
                    color: "#ffffff"
                    font.pixelSize: 12
                font.family: loginDialog.chineseFont || font.family
                }

                TextField {
                    id: usernameField
                    Layout.fillWidth: true
                    placeholderText: "请输入用户名"
                    color: "#FFFFFF"
                    placeholderTextColor: "#666666"
                    selectByMouse: true
                    background: Rectangle {
                        color: usernameField.focus ? "#3A3A4E" : "#2A2A3E"
                        border.color: usernameField.focus ? "#4A90E2" : "#444444"
                        border.width: usernameField.focus ? 2 : 1
                        radius: 6
                        Behavior on color { ColorAnimation { duration: 200 } }
                        Behavior on border.color { ColorAnimation { duration: 200 } }
                    }
                    Keys.onReturnPressed: {
                        if (passwordField.text.length > 0) {
                            passwordField.focus = false
                            if (usernameField.text.length > 0 && passwordField.text.length > 0) loginButton.clicked()
                        } else {
                            passwordField.focus = true
                        }
                    }
                    Component.onCompleted: {
                        if (typeof authManager !== 'undefined' && authManager.username && authManager.username.length > 0) {
                            usernameField.text = authManager.username
                            console.log("[Client][UI] 登录对话框 已填充上次账户名 len=" + authManager.username.length)
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: typeof authManager !== 'undefined' && authManager.usernameHistory && authManager.usernameHistory.length > 0
                    Repeater {
                        model: typeof authManager !== 'undefined' ? authManager.usernameHistory : []
                        Button {
                            text: modelData
                            font.pixelSize: 11
                            font.family: loginDialog.chineseFont || font.family
                            onClicked: {
                                usernameField.text = modelData
                                console.log("[Client][UI] 选择历史账户名 " + modelData)
                            }
                        }
                    }
                }
            }

            // 密码（框内眼睛图标切换可见/不可见）
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text {
                    text: "密码:"
                    color: "#ffffff"
                    font.pixelSize: 12
                    font.family: loginDialog.chineseFont || font.family
                }
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    TextField {
                        id: passwordField
                        anchors.fill: parent
                        rightPadding: 40
                        placeholderText: "请输入密码"
                        echoMode: loginDialog.passwordVisible ? TextInput.Normal : TextInput.Password
                        color: "#FFFFFF"
                        placeholderTextColor: "#666666"
                        selectByMouse: true
                        background: Rectangle {
                            color: passwordField.focus ? "#3A3A4E" : "#2A2A3E"
                            border.color: passwordField.focus ? "#4A90E2" : "#444444"
                            border.width: passwordField.focus ? 2 : 1
                            radius: 6
                            Behavior on color { ColorAnimation { duration: 200 } }
                            Behavior on border.color { ColorAnimation { duration: 200 } }
                        }
                        Keys.onReturnPressed: {
                            if (usernameField.text.length > 0 && passwordField.text.length > 0) loginButton.clicked()
                        }
                    }
                    // 密码框内右侧眼睛图标（SVG，不依赖 emoji 字体），点击切换明文/掩码
                    MouseArea {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 10
                        width: 26
                        height: 26
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            loginDialog.passwordVisible = !loginDialog.passwordVisible
                            console.log("[Client][UI] 密码框 可见=" + loginDialog.passwordVisible)
                        }
                        Image {
                            anchors.centerIn: parent
                            width: 20
                            height: 20
                            source: loginDialog.passwordVisible ? "icon/eye-off.svg" : "icon/eye.svg"
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                }
            }

            // 错误信息（美化样式）
            Rectangle {
                Layout.fillWidth: true
                height: errorText.text.length > 0 ? errorText.implicitHeight + 12 : 0
                color: "#3A1E1E"
                border.color: "#FF6B6B"
                border.width: 1
                radius: 6
                visible: errorText.text.length > 0
                
                Behavior on height { NumberAnimation { duration: 200 } }
                
                Text {
                    id: errorText
                    anchors.fill: parent
                    anchors.margins: 6
                    color: "#FF6B6B"
                    font.pixelSize: 12
                    font.family: loginDialog.chineseFont || font.family
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // 登录/取消按钮（现代化样式）
            Button {
                id: loginButton
                Layout.fillWidth: true
                height: 48
                text: isLoggingIn ? "取消" : "登录"
                enabled: !isLoggingIn ? (usernameField.text.length > 0 && passwordField.text.length > 0) : true
                
                contentItem: Text {
                    text: loginButton.text
                    font.pixelSize: 16
                    font.family: loginDialog.chineseFont || font.family
                    font.bold: true
                    color: loginButton.enabled ? "#FFFFFF" : "#666666"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                background: Rectangle {
                    radius: 8
                    gradient: Gradient {
                        GradientStop { 
                            position: 0.0
                            color: loginButton.enabled ? (loginButton.pressed ? "#357ABD" : (loginButton.hovered ? "#5AA0F2" : "#4A90E2"))
                            : "#2A2A3E"
                        }
                        GradientStop { 
                            position: 1.0
                            color: loginButton.enabled ? (loginButton.pressed ? "#2A5A8D" : (loginButton.hovered ? "#4A80D2" : "#357ABD"))
                            : "#1E1E2E"
                        }
                    }
                    border.color: loginButton.enabled ? "#5AA0F2" : "#444444"
                    border.width: 1
                    
                    // 简单阴影
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        z: -1
                        radius: parent.radius + 2
                        color: loginButton.enabled ? "#30000000" : "#00000000"
                    }
                }
                
                onClicked: {
                    if (isLoggingIn) {
                        cancelLogin()
                    } else {
                        startLogin()
                    }
                }
            }
            
            // 开始登录
            function startLogin() {
                errorText.text = ""
                isLoggingIn = true
                loginButton.text = "取消"
                authManager.login(usernameField.text, passwordField.text, serverUrlField.text)
            }
            
            // 取消登录
            function cancelLogin() {
                isLoggingIn = false
                loginButton.text = "登录"
                // 如果主页面已经显示，关闭它并返回登录界面
                if (authManager.isLoggedIn) {
                    authManager.logout()
                }
            }
        }
    }

    Connections {
        target: authManager
        
        function onLoginSucceeded(token, userInfo) {
            isLoggingIn = false
            loginButton.text = "登录"
            if (userInfo && userInfo.username && typeof authManager !== 'undefined') {
                authManager.addUsernameToHistory(userInfo.username)
            }
            
            // 延迟关闭登录对话框，显示主页面
            // 给用户一个短暂的时间窗口可以点击取消
            loginSuccessTimer.start()
        }
        
        function onLoginFailed(error) {
            // 登录失败，重置按钮状态
            isLoggingIn = false
            loginButton.text = "登录"
            errorText.text = error
        }
    }
    
    // 登录成功定时器（给用户时间点击取消）
    QtQuick2.Timer {
        id: loginSuccessTimer
        interval: 500  // 500ms 延迟，给用户时间点击取消
        onTriggered: {
            // 如果用户没有点击取消，关闭登录对话框
            if (authManager.isLoggedIn) {
                loginDialog.close()
            }
        }
    }
}
