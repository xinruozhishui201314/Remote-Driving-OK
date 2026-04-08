import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RemoteDriving 1.0
import "styles" as ThemeModule

/**
 * 登录页面
 * 统一使用 AppContext 和 Theme
 */
Rectangle {
    id: loginPage

    // ── 错误状态管理 ──────────────────────────────────────────────────
    QtObject {
        id: loginErrorState
        property string errorMessage: ""
        property int errorCode: 0
        
        function showError(code, message) {
            errorCode = code
            errorMessage = message
            console.log("[Client][UI][LoginPage] Error: code=" + code + " message=" + message)
        }
        
        function clearError() {
            errorCode = 0
            errorMessage = ""
        }
    }

    // ── 统一属性 ─────────────────────────────────────────────────────
    property bool isLoggingIn: false
    property bool passwordVisible: false
    
    // 统一字体获取
    readonly property string chineseFont: AppContext.chineseFont
    
    // 统一 AuthManager 访问
    readonly property var authManager: AppContext.authManager
    readonly property var nodeHealthChecker: AppContext.nodeHealthChecker
    
    // 背景渐变
    gradient: Gradient {
        GradientStop { position: 0.0; color: ThemeModule.Theme.colorBackground }
        GradientStop { position: 0.5; color: ThemeModule.Theme.colorSurface }
        GradientStop { position: 1.0; color: ThemeModule.Theme.colorBackground }
    }
    
    // 装饰性背景元素
    Rectangle {
        anchors.fill: parent
        opacity: 0.1
        gradient: Gradient {
            GradientStop { position: 0.0; color: ThemeModule.Theme.colorBorderActive }
            GradientStop { position: 1.0; color: ThemeModule.Theme.colorAccent }
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
            
            // Logo
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 100
                height: 100
                radius: 50
                gradient: Gradient {
                    GradientStop { position: 0.0; color: ThemeModule.Theme.colorBorderActive }
                    GradientStop { position: 1.0; color: ThemeModule.Theme.colorPrimary }
                }
                
                // 阴影
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
                color: ThemeModule.Theme.colorText
                font.pixelSize: 32
                font.family: loginPage.chineseFont || font.family
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            
            // 副标题
            Text {
                text: "Remote Driving Client"
                color: ThemeModule.Theme.colorTextDim
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
                    GradientStop { position: 0.0; color: ThemeModule.Theme.colorBorderActive }
                    GradientStop { position: 1.0; color: ThemeModule.Theme.colorAccent }
                }
            }
        }
        
        // 登录表单
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: loginForm.implicitHeight + 40
            color: ThemeModule.Theme.colorSurface
            border.color: ThemeModule.Theme.colorBorderActive
            border.width: 2
            radius: 16
            
            // 阴影
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
                        color: ThemeModule.Theme.colorText
                        font.pixelSize: 14
                        font.family: loginPage.chineseFont || font.family
                        font.bold: true
                    }
                    
                    TextField {
                        id: serverUrlField
                        Layout.fillWidth: true
                        placeholderText: "http://192.168.1.100:8081"
                        text: (typeof defaultServerUrlFromEnv !== 'undefined' && defaultServerUrlFromEnv) ? defaultServerUrlFromEnv : "http://localhost:8081"
                        color: ThemeModule.Theme.colorText
                        placeholderTextColor: "#666666"
                        selectByMouse: true
                        background: Rectangle {
                            color: serverUrlField.focus ? ThemeModule.Theme.colorButtonBgHover : ThemeModule.Theme.colorButtonBg
                            border.color: serverUrlField.focus ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorButtonBorder
                            border.width: serverUrlField.focus ? 2 : 1
                            radius: 8
                            Behavior on color { ColorAnimation { duration: 200 } }
                            Behavior on border.color { ColorAnimation { duration: 200 } }
                        }
                    }
                }

                // 节点状态
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: nodeStatusColumn.implicitHeight + 24
                    color: ThemeModule.Theme.colorButtonBg
                    border.color: ThemeModule.Theme.colorButtonBorder
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
                                color: ThemeModule.Theme.colorTextDim
                                font.pixelSize: 12
                                font.family: loginPage.chineseFont || font.family
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: {
                                    var nhc = loginPage.nodeHealthChecker
                                    return nhc && nhc.isChecking ? "检测中..." : "检测"
                                }
                                enabled: {
                                    var nhc = loginPage.nodeHealthChecker
                                    return nhc && !nhc.isChecking
                                }
                                font.pixelSize: 12
                                font.family: loginPage.chineseFont || font.family
                                onClicked: {
                                    var nhc = loginPage.nodeHealthChecker
                                    if (nhc && typeof nhc.refresh === "function") {
                                        nhc.refresh(serverUrlField.text)
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Backend"; color: ThemeModule.Theme.colorTextDim; font.pixelSize: 11; font.family: loginPage.chineseFont || font.family; Layout.preferredWidth: 70 }
                            Text {
                                text: {
                                    var nhc = loginPage.nodeHealthChecker
                                    return nhc ? nhc.backendStatus : "—"
                                }
                                color: {
                                    var nhc = loginPage.nodeHealthChecker
                                    if (!nhc) return ThemeModule.Theme.colorTextDim
                                    var s = nhc.backendStatus
                                    if (s === "正常") return ThemeModule.Theme.colorAccent
                                    if (s === "不可达" || s === "异常") return ThemeModule.Theme.colorDanger
                                    return ThemeModule.Theme.colorTextDim
                                }
                                font.pixelSize: 11
                                font.family: loginPage.chineseFont || font.family
                            }
                            Text {
                                text: {
                                    var nhc = loginPage.nodeHealthChecker
                                    return nhc && nhc.backendMessage ? " " + nhc.backendMessage : ""
                                }
                                color: ThemeModule.Theme.colorTextDim
                                font.pixelSize: 10
                                font.family: loginPage.chineseFont || font.family
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Keycloak"; color: ThemeModule.Theme.colorTextDim; font.pixelSize: 11; font.family: loginPage.chineseFont || font.family; Layout.preferredWidth: 70 }
                            Text {
                                text: {
                                    var nhc = loginPage.nodeHealthChecker
                                    return nhc ? nhc.keycloakStatus : "—"
                                }
                                color: {
                                    var nhc = loginPage.nodeHealthChecker
                                    if (!nhc) return ThemeModule.Theme.colorTextDim
                                    var s = nhc.keycloakStatus
                                    if (s === "正常") return ThemeModule.Theme.colorAccent
                                    if (s === "不可达" || s === "异常") return ThemeModule.Theme.colorDanger
                                    return ThemeModule.Theme.colorTextDim
                                }
                                font.pixelSize: 11
                                font.family: loginPage.chineseFont || font.family
                            }
                            Text {
                                text: {
                                    var nhc = loginPage.nodeHealthChecker
                                    return nhc && nhc.keycloakMessage ? " " + nhc.keycloakMessage : ""
                                }
                                color: ThemeModule.Theme.colorTextDim
                                font.pixelSize: 10
                                font.family: loginPage.chineseFont || font.family
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "ZLM"; color: ThemeModule.Theme.colorTextDim; font.pixelSize: 11; font.family: loginPage.chineseFont || font.family; Layout.preferredWidth: 70 }
                            Text {
                                text: {
                                    var nhc = loginPage.nodeHealthChecker
                                    return nhc ? nhc.zlmStatus : "—"
                                }
                                color: {
                                    var nhc = loginPage.nodeHealthChecker
                                    if (!nhc) return ThemeModule.Theme.colorTextDim
                                    var s = nhc.zlmStatus
                                    if (s === "正常") return ThemeModule.Theme.colorAccent
                                    if (s === "不可达" || s === "异常") return ThemeModule.Theme.colorDanger
                                    return ThemeModule.Theme.colorTextDim
                                }
                                font.pixelSize: 11
                                font.family: loginPage.chineseFont || font.family
                            }
                            Text {
                                text: {
                                    var nhc = loginPage.nodeHealthChecker
                                    return nhc && nhc.zlmMessage ? " " + nhc.zlmMessage : ""
                                }
                                color: ThemeModule.Theme.colorTextDim
                                font.pixelSize: 10
                                font.family: loginPage.chineseFont || font.family
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                    }
                    Component.onCompleted: {
                        var nhc = loginPage.nodeHealthChecker
                        if (nhc && serverUrlField.text.length > 0 && typeof nhc.refresh === "function") {
                            nhc.refresh(serverUrlField.text)
                        }
                    }
                }
                
                // 用户名
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: "用户名"
                        color: ThemeModule.Theme.colorText
                        font.pixelSize: 14
                        font.family: loginPage.chineseFont || font.family
                        font.bold: true
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
                            var am = loginPage.authManager
                            if (am && am.username && am.username.length > 0) {
                                usernameField.text = am.username
                                console.log("[Client][UI][LoginPage] 已填充上次账户名")
                            }
                        }
                    }
                    // 历史账户名
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: {
                            var am = loginPage.authManager
                            return am && am.usernameHistory && am.usernameHistory.length > 0
                        }
                        Repeater {
                            model: {
                                var am = loginPage.authManager
                                return am ? am.usernameHistory : []
                            }
                            Button {
                                text: modelData
                                font.pixelSize: 11
                                font.family: loginPage.chineseFont || font.family
                                onClicked: {
                                    usernameField.text = modelData
                                    console.log("[Client][UI][LoginPage] 选择历史账户名 " + modelData)
                                }
                            }
                        }
                    }
                }

                // 密码
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: "密码"
                        color: ThemeModule.Theme.colorText
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
                            color: ThemeModule.Theme.colorText
                            placeholderTextColor: "#666666"
                            selectByMouse: true
                            background: Rectangle {
                                color: passwordField.focus ? ThemeModule.Theme.colorButtonBgHover : ThemeModule.Theme.colorButtonBg
                                border.color: passwordField.focus ? ThemeModule.Theme.colorBorderActive : ThemeModule.Theme.colorButtonBorder
                                border.width: passwordField.focus ? 2 : 1
                                radius: 8
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
                            anchors.rightMargin: 12
                            width: 28
                            height: 28
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                loginPage.passwordVisible = !loginPage.passwordVisible
                                console.log("[Client][UI][LoginPage] 密码框 可见=" + loginPage.passwordVisible)
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
                    height: loginErrorState.errorMessage.length > 0 ? errorText.implicitHeight + 12 : 0
                    color: "#3A1E1E"
                    border.color: ThemeModule.Theme.colorDanger
                    border.width: 1
                    radius: 8
                    visible: loginErrorState.errorMessage.length > 0
                    
                    Behavior on height { NumberAnimation { duration: 200 } }
                    
                    Text {
                        id: errorText
                        anchors.fill: parent
                        anchors.margins: 6
                        color: ThemeModule.Theme.colorDanger
                        font.pixelSize: 12
                        font.family: loginPage.chineseFont || font.family
                        wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignVCenter
                        text: loginErrorState.errorCode > 0
                            ? "[" + loginErrorState.errorCode + "] " + loginErrorState.errorMessage
                            : loginErrorState.errorMessage
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
                        color: loginButton.enabled ? ThemeModule.Theme.colorText : "#666666"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    background: Rectangle {
                        radius: 10
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
            color: ThemeModule.Theme.colorTextDim
            font.pixelSize: 12
            Layout.alignment: Qt.AlignHCenter
        }
    }
    
    // ── 登录逻辑 ─────────────────────────────────────────────────────
    function startLogin() {
        if (serverUrlField.text.trim().length === 0) {
            loginErrorState.showError(1001, "请输入服务器地址")
            return
        }
        if (usernameField.text.trim().length === 0) {
            loginErrorState.showError(1002, "请输入用户名")
            return
        }
        if (passwordField.text.length === 0) {
            loginErrorState.showError(1003, "请输入密码")
            return
        }

        loginErrorState.clearError()
        isLoggingIn = true
        
        var am = loginPage.authManager
        if (am) {
            console.log("[Client][UI][LoginPage] 开始登录:", usernameField.text, "->", serverUrlField.text)
            am.login(usernameField.text, passwordField.text, serverUrlField.text)
        } else {
            isLoggingIn = false
            loginErrorState.showError(1000, "认证管理器不可用")
        }
    }
    
    // ── 信号连接 ─────────────────────────────────────────────────────
    Connections {
        target: loginPage.authManager
        ignoreUnknownSignals: true
        
        function onLoginSucceeded(token, userInfo) {
            isLoggingIn = false
            loginErrorState.clearError()
            var am = loginPage.authManager
            if (am && userInfo && userInfo.username && typeof am.addUsernameToHistory === "function") {
                am.addUsernameToHistory(userInfo.username)
            }
            console.log("[Client][UI][LoginPage] 登录成功")
        }
        
        function onLoginFailed(error) {
            isLoggingIn = false
            loginErrorState.showError(4001, error || "登录失败，请检查用户名和密码")
            console.log("[Client][UI][LoginPage] 登录失败:", error)
        }
    }
    
    // 页面动画
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
