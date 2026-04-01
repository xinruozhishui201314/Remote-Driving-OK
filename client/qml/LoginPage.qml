import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 登录页面（嵌入主窗口）
 * 美观的登录界面，作为应用首页
 */
Rectangle {
    id: loginPage
    
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
    
    // 登录状态
    property bool isLoggingIn: false
    // 密码是否明文显示（可选切换）
    property bool passwordVisible: false
    
    // 背景渐变
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#1E1E2E" }
        GradientStop { position: 0.5; color: "#2A2A3E" }
        GradientStop { position: 1.0; color: "#1E1E2E" }
    }
    
    // 装饰性背景元素
    Rectangle {
        anchors.fill: parent
        opacity: 0.1
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#4A90E2" }
            GradientStop { position: 1.0; color: "#50C878" }
        }
        rotation: -15
        transformOrigin: Item.TopLeft
    }
    
    // 主内容区域
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.4, 500)
        spacing: 40
        
        // Logo 和标题区域
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 20
            
            // Logo 图标
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 100
                height: 100
                radius: 50
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#4A90E2" }
                    GradientStop { position: 1.0; color: "#357ABD" }
                }
                
                // 简单阴影
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -5
                    z: -1
                    radius: parent.radius + 5
                    color: "#30000000"
                }
                
                Image {
                    anchors.centerIn: parent
                    width: 48
                    height: 48
                    source: "icon/vehicle.svg"
                    fillMode: Image.PreserveAspectFit
                }
            }
            
            // 标题
            Text {
                text: "远程驾驶客户端"
                color: "#FFFFFF"
                font.pixelSize: 32
                font.family: loginPage.chineseFont || font.family
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            
            // 副标题
            Text {
                text: "Remote Driving Client"
                color: "#B0B0B0"
                font.pixelSize: 16
                Layout.alignment: Qt.AlignHCenter
            }
            
            // 装饰线
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 80
                height: 4
                radius: 2
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#4A90E2" }
                    GradientStop { position: 1.0; color: "#50C878" }
                }
            }
        }
        
        // 登录表单
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: loginForm.implicitHeight + 40
            color: "#2A2A3E"
            border.color: "#4A90E2"
            border.width: 2
            radius: 16
            
            // 简单阴影
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
                id: loginForm
                anchors.fill: parent
                anchors.margins: 30
                spacing: 24
                
                // 服务器地址
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    
                    Text {
                        text: "服务器地址"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.family: loginPage.chineseFont || font.family
                        font.bold: true
                    }
                    
                    TextField {
                        id: serverUrlField
                        Layout.fillWidth: true
                        placeholderText: "http://192.168.1.100:8081"
                        // 容器内运行可设置 DEFAULT_SERVER_URL=http://backend:8080，宿主机不设则用 localhost:8081
                        text: (typeof defaultServerUrlFromEnv !== 'undefined' && defaultServerUrlFromEnv) ? defaultServerUrlFromEnv : "http://localhost:8081"
                        color: "#FFFFFF"
                        placeholderTextColor: "#666666"
                        selectByMouse: true
                        background: Rectangle {
                            color: serverUrlField.focus ? "#3A3A4E" : "#1A1A2A"
                            border.color: serverUrlField.focus ? "#4A90E2" : "#444444"
                            border.width: serverUrlField.focus ? 2 : 1
                            radius: 8
                            Behavior on color { ColorAnimation { duration: 200 } }
                            Behavior on border.color { ColorAnimation { duration: 200 } }
                        }
                    }
                }

                // 节点状态（Backend / Keycloak / ZLM）
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: nodeStatusColumn.implicitHeight + 24
                    color: "#1A1A2A"
                    border.color: "#444444"
                    radius: 8
                    ColumnLayout {
                        id: nodeStatusColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: "节点状态"
                                color: "#B0B0B0"
                                font.pixelSize: 12
                                font.family: loginPage.chineseFont || font.family
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: typeof nodeHealthChecker !== 'undefined' && nodeHealthChecker.isChecking ? "检测中..." : "检测"
                                enabled: typeof nodeHealthChecker !== 'undefined' && !nodeHealthChecker.isChecking
                                font.pixelSize: 12
                                font.family: loginPage.chineseFont || font.family
                                onClicked: {
                                    if (typeof nodeHealthChecker !== 'undefined')
                                        nodeHealthChecker.refresh(serverUrlField.text)
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Backend"; color: "#B0B0B0"; font.pixelSize: 11; font.family: loginPage.chineseFont || font.family; Layout.preferredWidth: 70 }
                            Text {
                                text: typeof nodeHealthChecker !== 'undefined' ? nodeHealthChecker.backendStatus : "—"
                                color: (typeof nodeHealthChecker !== 'undefined' && nodeHealthChecker.backendStatus === "正常") ? "#50C878" : (typeof nodeHealthChecker !== 'undefined' && (nodeHealthChecker.backendStatus === "不可达" || nodeHealthChecker.backendStatus === "异常") ? "#FF6B6B" : "#B0B0B0")
                                font.pixelSize: 11
                                font.family: loginPage.chineseFont || font.family
                            }
                            Text {
                                text: typeof nodeHealthChecker !== 'undefined' && nodeHealthChecker.backendMessage ? (" " + nodeHealthChecker.backendMessage) : ""
                                color: "#888888"
                                font.pixelSize: 10
                                font.family: loginPage.chineseFont || font.family
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Keycloak"; color: "#B0B0B0"; font.pixelSize: 11; font.family: loginPage.chineseFont || font.family; Layout.preferredWidth: 70 }
                            Text {
                                text: typeof nodeHealthChecker !== 'undefined' ? nodeHealthChecker.keycloakStatus : "—"
                                color: (typeof nodeHealthChecker !== 'undefined' && nodeHealthChecker.keycloakStatus === "正常") ? "#50C878" : (typeof nodeHealthChecker !== 'undefined' && (nodeHealthChecker.keycloakStatus === "不可达" || nodeHealthChecker.keycloakStatus === "异常") ? "#FF6B6B" : "#B0B0B0")
                                font.pixelSize: 11
                                font.family: loginPage.chineseFont || font.family
                            }
                            Text {
                                text: typeof nodeHealthChecker !== 'undefined' && nodeHealthChecker.keycloakMessage ? (" " + nodeHealthChecker.keycloakMessage) : ""
                                color: "#888888"
                                font.pixelSize: 10
                                font.family: loginPage.chineseFont || font.family
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "ZLM"; color: "#B0B0B0"; font.pixelSize: 11; font.family: loginPage.chineseFont || font.family; Layout.preferredWidth: 70 }
                            Text {
                                text: typeof nodeHealthChecker !== 'undefined' ? nodeHealthChecker.zlmStatus : "—"
                                color: (typeof nodeHealthChecker !== 'undefined' && nodeHealthChecker.zlmStatus === "正常") ? "#50C878" : (typeof nodeHealthChecker !== 'undefined' && (nodeHealthChecker.zlmStatus === "不可达" || nodeHealthChecker.zlmStatus === "异常") ? "#FF6B6B" : "#B0B0B0")
                                font.pixelSize: 11
                                font.family: loginPage.chineseFont || font.family
                            }
                            Text {
                                text: typeof nodeHealthChecker !== 'undefined' && nodeHealthChecker.zlmMessage ? (" " + nodeHealthChecker.zlmMessage) : ""
                                color: "#888888"
                                font.pixelSize: 10
                                font.family: loginPage.chineseFont || font.family
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                    }
                    Component.onCompleted: {
                        if (typeof nodeHealthChecker !== 'undefined' && serverUrlField.text.length > 0)
                            nodeHealthChecker.refresh(serverUrlField.text)
                    }
                }
                
                // 用户名（支持历史输入）
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: "用户名"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.family: loginPage.chineseFont || font.family
                        font.bold: true
                    }
                    TextField {
                        id: usernameField
                        Layout.fillWidth: true
                        placeholderText: "请输入用户名"
                        color: "#FFFFFF"
                        placeholderTextColor: "#666666"
                        selectByMouse: true
                        background: Rectangle {
                            color: usernameField.focus ? "#3A3A4E" : "#1A1A2A"
                            border.color: usernameField.focus ? "#4A90E2" : "#444444"
                            border.width: usernameField.focus ? 2 : 1
                            radius: 8
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
                                console.log("[Client][UI] 登录页 已填充上次账户名 len=" + authManager.username.length)
                            }
                        }
                    }
                    // 历史账户名快捷选择
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: typeof authManager !== 'undefined' && authManager.usernameHistory && authManager.usernameHistory.length > 0
                        Repeater {
                            model: typeof authManager !== 'undefined' ? authManager.usernameHistory : []
                            Button {
                                text: modelData
                                font.pixelSize: 11
                                font.family: loginPage.chineseFont || font.family
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
                    spacing: 8
                    Text {
                        text: "密码"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.family: loginPage.chineseFont || font.family
                        font.bold: true
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        TextField {
                            id: passwordField
                            anchors.fill: parent
                            rightPadding: 44
                            placeholderText: "请输入密码"
                            echoMode: loginPage.passwordVisible ? TextInput.Normal : TextInput.Password
                            color: "#FFFFFF"
                            placeholderTextColor: "#666666"
                            selectByMouse: true
                            background: Rectangle {
                                color: passwordField.focus ? "#3A3A4E" : "#1A1A2A"
                                border.color: passwordField.focus ? "#4A90E2" : "#444444"
                                border.width: passwordField.focus ? 2 : 1
                                radius: 8
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
                            anchors.rightMargin: 12
                            width: 28
                            height: 28
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                loginPage.passwordVisible = !loginPage.passwordVisible
                                console.log("[Client][UI] 密码框 可见=" + loginPage.passwordVisible)
                            }
                            Image {
                                anchors.centerIn: parent
                                width: 22
                                height: 22
                                source: loginPage.passwordVisible ? "icon/eye-off.svg" : "icon/eye.svg"
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
                    border.color: "#FF6B6B"
                    border.width: 1
                    radius: 8
                    visible: errorText.text.length > 0
                    
                    Behavior on height { NumberAnimation { duration: 200 } }
                    
                    Text {
                        id: errorText
                        anchors.fill: parent
                        anchors.margins: 6
                        color: "#FF6B6B"
                        font.pixelSize: 12
                        font.family: loginPage.chineseFont || font.family
                        wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // 登录按钮
                Button {
                    id: loginButton
                    Layout.fillWidth: true
                    height: 52
                    text: isLoggingIn ? "登录中..." : "登录"
                    enabled: !isLoggingIn ? (usernameField.text.length > 0 && passwordField.text.length > 0) : true
                    
                    contentItem: Text {
                        text: loginButton.text
                        font.pixelSize: 18
                        font.family: loginPage.chineseFont || font.family
                        font.bold: true
                        color: loginButton.enabled ? "#FFFFFF" : "#666666"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    background: Rectangle {
                        radius: 10
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
                        if (!isLoggingIn) {
                            startLogin()
                        }
                    }
                }
            }
        }
        
        // 底部信息
        Text {
            text: "© 2026 Remote Driving System"
            color: "#666666"
            font.pixelSize: 12
            Layout.alignment: Qt.AlignHCenter
        }
    }
    
    // 开始登录
    function startLogin() {
        errorText.text = ""
        isLoggingIn = true
        if (typeof authManager !== "undefined" && authManager) {
            authManager.login(usernameField.text, passwordField.text, serverUrlField.text)
        }
    }
    
    // 连接认证管理器信号
    Connections {
        target: authManager
        ignoreUnknownSignals: true
        
        function onLoginSucceeded(token, userInfo) {
            isLoggingIn = false
            errorText.text = ""
            if (userInfo && userInfo.username && typeof authManager !== 'undefined') {
                authManager.addUsernameToHistory(userInfo.username)
            }
        }
        
        function onLoginFailed(error) {
            isLoggingIn = false
            errorText.text = error
        }
    }
    
    // 页面进入动画：首帧即可见（opacity 1）避免 Docker/无 GPU 下黑屏；仅做缩放动画
    opacity: 1
    scale: 0.98
    
    Component.onCompleted: {
        scaleAnim.start()
    }
    
    NumberAnimation {
        id: scaleAnim
        target: loginPage
        property: "scale"
        from: 0.98
        to: 1.0
        duration: 300
        easing.type: Easing.OutCubic
    }
}
