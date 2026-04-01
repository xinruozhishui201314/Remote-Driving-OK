# 登录流程修改说明

## Executive Summary

已修改登录流程：
1. ✅ 移除默认用户名和密码（字段为空）
2. ✅ 添加测试模式：用户名和密码都是 "123" 时模拟登录成功
3. ✅ 登录成功后直接显示主页面（不打开车辆选择对话框）

---

## 1. 修改内容

### ✅ 移除默认值

**LoginDialog.qml**:
- 用户名字段：移除 `text: "123"`
- 密码字段：移除 `text: "123"`
- 现在两个字段都为空，需要用户手动输入

### ✅ 添加测试模式

**authmanager.cpp**:
- 当用户名和密码都是 "123" 时，直接模拟登录成功
- 不需要调用服务器 API
- 生成测试 token 和用户信息
- 立即触发 `loginSucceeded` 信号

### ✅ 修改登录成功流程

**LoginDialog.qml**:
- 登录成功后只关闭登录对话框
- 不自动打开车辆选择对话框
- 主页面（ApplicationWindow）自动显示

---

## 2. 登录流程

### 用户操作流程

1. **打开登录对话框**
   - 用户名字段：空
   - 密码字段：空

2. **输入用户名和密码**
   - 用户输入用户名：`123`
   - 用户输入密码：`123`

3. **点击登录按钮**
   - 触发 `authManager.login("123", "123", serverUrl)`

4. **测试模式检测**
   - 检测到用户名和密码都是 "123"
   - 模拟登录成功
   - 生成测试 token
   - 更新登录状态

5. **显示主页面**
   - 关闭登录对话框
   - 主页面（ApplicationWindow）显示
   - 用户可以看到：
     - 视频显示区域（左侧）
     - 控制面板（右侧）
     - 状态栏（顶部）
     - 菜单栏

---

## 3. 代码变更详情

### LoginDialog.qml

```qml
TextField {
    id: usernameField
    placeholderText: "请输入用户名"
    // 移除了 text: "123"
}

TextField {
    id: passwordField
    placeholderText: "请输入密码"
    // 移除了 text: "123"
}

function onLoginSucceeded(token, userInfo) {
    // 只关闭登录对话框，不打开车辆选择对话框
    loginDialog.close()
}
```

### authmanager.cpp

```cpp
void AuthManager::login(const QString &username, const QString &password, const QString &serverUrl)
{
    // 测试模式：如果用户名和密码都是 "123"，直接模拟登录成功
    if (username == "123" && password == "123") {
        // 模拟登录成功
        QString testToken = "test_token_" + QString::number(QDateTime::currentMSecsSinceEpoch());
        QJsonObject testUserInfo;
        testUserInfo["username"] = username;
        
        updateLoginStatus(true, username, testToken);
        saveCredentials();
        
        emit loginSucceeded(testToken, testUserInfo);
        return;
    }
    
    // 正常模式：调用服务器 API
    // ...
}
```

---

## 4. 主页面组件

登录成功后显示的主页面包含：

1. **视频显示区域**（左侧，70% 宽度）
   - VideoView 组件
   - 显示视频流或等待连接提示

2. **控制面板**（右侧，30% 宽度）
   - ControlPanel 组件
   - 车辆控制控件（方向盘、油门、刹车等）
   - 车辆状态信息

3. **状态栏**（顶部）
   - StatusBar 组件
   - 显示连接状态、视频状态、MQTT 状态等

4. **菜单栏**
   - 文件菜单：选择车辆、连接、断开连接、退出登录、退出
   - 设置菜单：视频设置、控制设置
   - 帮助菜单：关于

---

## 5. 验证方法

### 运行程序

```bash
cd /workspaces/Remote-Driving/client
bash run.sh
```

### 验证步骤

1. ✅ 打开登录对话框
2. ✅ 检查用户名字段是否为空
3. ✅ 检查密码字段是否为空
4. ✅ 输入用户名：`123`
5. ✅ 输入密码：`123`
6. ✅ 点击登录按钮
7. ✅ 登录对话框关闭
8. ✅ 主页面显示（视频区域、控制面板、状态栏）

---

## 6. 修改的文件

1. **client/qml/LoginDialog.qml**
   - 移除默认用户名和密码
   - 修改登录成功后的流程

2. **client/src/authmanager.cpp**
   - 添加测试模式支持
   - 添加 QTimer 和 QDateTime 头文件

---

## 7. 总结

✅ **默认值已移除** - 用户名和密码字段为空
✅ **测试模式已添加** - 用户名和密码都是 "123" 时模拟登录成功
✅ **登录流程已修改** - 登录成功后直接显示主页面

**所有修改已保存并编译完成！** 🎉

---

## 8. 注意事项

1. **测试模式**：用户名和密码都是 "123" 时，不需要服务器即可登录
2. **正常模式**：其他用户名/密码组合会调用服务器 API
3. **车辆选择**：用户可以通过菜单"文件 -> 选择车辆..."来打开车辆选择对话框
4. **主页面**：ApplicationWindow 的 `visible: true` 确保主页面始终可见
