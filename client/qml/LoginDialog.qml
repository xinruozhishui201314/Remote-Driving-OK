import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick 2.15 as QtQuick2
import "styles" as ThemeModule

/**
 * 登录对话框
 * 统一使用 AppContext 和 Theme
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
    
    // ── 属性 ────────────────────────────────────────────────────────────
    property bool isLoggingIn: false
    property bool passwordVisible: false
    property bool devTestMode: true  // 开发测试模式
    
    // ── 统一字体获取 ──────────────────────────────────────────────────
    readonly property string chineseFont: AppContext.chineseFont
    
    // ── 统一 AuthManager 访问 ──────────────────────────────────────────
    readonly property var authManager: AppContext.authManager
    
    // ── 背景遮罩 ─────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: "#80000000"
    }
    
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: ThemeModule.Theme.colorPanel }
            GradientStop { position: 1.0; color: ThemeModule.Theme.colorBackground }
        }
        border.color: ThemeModule.Theme.colorBorderActive
        border.width: 2
        radius: 12
        
        // 阴影效果
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

            // 标题区域
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 10
                
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 60
                    height: 60
                    radius: 30
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: ThemeModule.Theme.colorBorderActive }
                        GradientStop { position: 1.0; color: ThemeModule.Theme.colorPrimary }
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
                    color: ThemeModule.Theme.colorText
                    font.pixelSize: 24
                    font.family: loginDialog.chineseFont || font.family
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }
                
                Text {
                    text: "登录您的账户"
                    color: ThemeModule.Theme.colorTextDim
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
                    color: ThemeModule.Theme.colorText
                    font.pixelSize: 12
                    font.family: loginDialog.chineseFont || font.family
                }

                TextField {
                    id: serverUrlField
                    Layout.fillWidth: true
                    placeholderText: "http://192.168.1.100:8080"
                    text: (typeof defaultServerUrlFromEnv !== 'undefined' && defaultServerUrlFromEnv) ? defaultServerUrlFromEnv : "http://localhost:8081"
                    color: ThemeModule.Theme.colorText
                    placeholderTextColor: "#666666"
                    selectByMouse: true
                    background: Rectangle {
                        color: serverUrlField.focus ? ThemeModule.Theme.colorButtonBgHover : ThemeModule.Theme.colorButtonBg
                        border.color: serverUrlField.focus ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorButtonBorder
                        border.width: serverUrlField.focus ? 2 : 1
                        radius: 6
                        Behavior on color { ColorAnimation { duration: 200 } }
                        Behavior on border.color { ColorAnimation { duration: 200 } }
                    }
                }

                // 开发测试模式说明
                Text {
                    text: "注意：开发测试模式下点击右侧 [TEST] 按钮可自动填充测试账号"
                    color: "#FFB84D"
                    font.pixelSize: 11
                    font.family: loginDialog.chineseFont || font.family
                    visible: loginDialog.devTestMode
                    Layout.fillWidth: true
                }
            }

            // 开发测试按钮
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
                        console.log("[Client][UI][LoginDialog] 开发测试：已填充测试账号 e2e-test")
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
                    color: ThemeModule.Theme.colorText
                    font.pixelSize: 12
                    font.family: loginDialog.chineseFont || font.family
                }

                TextField {
                    id: usernameField
                    Layout.fillWidth: true
                    placeholderText: "请输入用户名"
                    color: ThemeModule.Theme.colorText
                    placeholderTextColor: "#666666"
                    selectByMouse: true
                    background: Rectangle {
                        color: usernameField.focus ? ThemeModule.Theme.colorButtonBgHover : ThemeModule.Theme.colorButtonBg
                        border.color: usernameField.focus ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorButtonBorder
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
                        var am = loginDialog.authManager
                        if (am && am.username && am.username.length > 0) {
                            usernameField.text = am.username
                            console.log("[Client][UI][LoginDialog] 已填充上次账户名")
                        }
                    }
                }
                
                // 历史账户名
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: loginDialog.authManager && loginDialog.authManager.usernameHistory && loginDialog.authManager.usernameHistory.length > 0
                    Repeater {
                        model: loginDialog.authManager ? loginDialog.authManager.usernameHistory : []
                        Button {
                            text: modelData
                            font.pixelSize: 11
                            font.family: loginDialog.chineseFont || font.family
                            onClicked: {
                                usernameField.text = modelData
                                console.log("[Client][UI][LoginDialog] 选择历史账户名 " + modelData)
                            }
                        }
                    }
                }
            }

            // 密码
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text {
                    text: "密码:"
                    color: ThemeModule.Theme.colorText
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
                        color: ThemeModule.Theme.colorText
                        placeholderTextColor: "#666666"
                        selectByMouse: true
                        background: Rectangle {
                            color: passwordField.focus ? ThemeModule.Theme.colorButtonBgHover : ThemeModule.Theme.colorButtonBg
                            border.color: passwordField.focus ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorButtonBorder
                            border.width: passwordField.focus ? 2 : 1
                            radius: 6
                            Behavior on color { ColorAnimation { duration: 200 } }
                            Behavior on border.color { ColorAnimation { duration: 200 } }
                        }
                        Keys.onReturnPressed: {
                            if (usernameField.text.length > 0 && passwordField.text.length > 0) loginButton.clicked()
                        }
                    }
                    // 密码可见切换
                    MouseArea {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 10
                        width: 26
                        height: 26
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            loginDialog.passwordVisible = !loginDialog.passwordVisible
                            console.log("[Client][UI][LoginDialog] 密码框 可见=" + loginDialog.passwordVisible)
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

            // 错误信息
            Rectangle {
                Layout.fillWidth: true
                height: errorText.text.length > 0 ? errorText.implicitHeight + 12 : 0
                color: "#3A1E1E"
                border.color: ThemeModule.Theme.colorDanger
                border.width: 1
                radius: 6
                visible: errorText.text.length > 0
                
                Behavior on height { NumberAnimation { duration: 200 } }
                
                Text {
                    id: errorText
                    anchors.fill: parent
                    anchors.margins: 6
                    color: ThemeModule.Theme.colorDanger
                    font.pixelSize: 12
                    font.family: loginDialog.chineseFont || font.family
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // 登录按钮
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
                    color: loginButton.enabled ? ThemeModule.Theme.colorText : "#666666"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                background: Rectangle {
                    radius: 8
                    gradient: Gradient {
                        GradientStop { 
                            position: 0.0
                            color: loginButton.enabled ? (loginButton.pressed ? "#357ABD" : (loginButton.hovered ? "#5AA0F2" : ThemeModule.Theme.colorBorderActive))
                            : ThemeModule.Theme.colorButtonBg
                        }
                        GradientStop { 
                            position: 1.0
                            color: loginButton.enabled ? (loginButton.pressed ? "#2A5A8D" : (loginButton.hovered ? "#4A80D2" : ThemeModule.Theme.colorPrimary))
                            : ThemeModule.Theme.colorSurface
                        }
                    }
                    border.color: loginButton.enabled ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorButtonBorder
                    border.width: 1
                    
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
            
            // ── 登录逻辑 ───────────────────────────────────────────────
            function startLogin() {
                errorText.text = ""
                isLoggingIn = true
                loginButton.text = "取消"
                var am = loginDialog.authManager
                if (am) {
                    console.log("[Client][UI][LoginDialog] 开始登录:", usernameField.text, "->", serverUrlField.text)
                    am.login(usernameField.text, passwordField.text, serverUrlField.text)
                } else {
                    isLoggingIn = false
                    loginButton.text = "登录"
                    errorText.text = "认证管理器不可用"
                }
            }
            
            function cancelLogin() {
                isLoggingIn = false
                loginButton.text = "登录"
                var am = loginDialog.authManager
                if (am && am.isLoggedIn) {
                    am.logout()
                }
            }
        }
    }

    // ── 信号连接 ─────────────────────────────────────────────────────
    Connections {
        target: loginDialog.authManager
        
        function onLoginSucceeded(token, userInfo) {
            isLoggingIn = false
            loginButton.text = "登录"
            var am = loginDialog.authManager
            if (am && userInfo && userInfo.username && typeof am.addUsernameToHistory === "function") {
                am.addUsernameToHistory(userInfo.username)
            }
            console.log("[Client][UI][LoginDialog] 登录成功")
            loginSuccessTimer.start()
        }
        
        function onLoginFailed(error) {
            isLoggingIn = false
            loginButton.text = "登录"
            errorText.text = error || "登录失败，请检查用户名和密码"
            console.log("[Client][UI][LoginDialog] 登录失败:", error)
        }
    }
    
    // 登录成功延迟关闭
    QtQuick2.Timer {
        id: loginSuccessTimer
        interval: 500
        onTriggered: {
            var am = loginDialog.authManager
            if (am && am.isLoggedIn) {
                loginDialog.close()
            }
        }
    }
}
