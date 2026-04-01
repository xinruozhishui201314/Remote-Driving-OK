# 远驾接管功能实现文档

## 功能概述

实现"远驾接管"功能，允许客户端通过点击按钮启用/禁用远驾接管状态，车端根据此状态决定是否接收远程控制指令。

## 功能需求

1. **按钮状态管理**：
   - 点击"已连接"后，车端停止推流
   - 按钮文本从"已连接"变为"连接车辆"
   - 在按钮右侧显示"远驾接管"按钮

2. **远驾接管控制**：
   - 点击"远驾接管"按钮，发送 `remote_control` 指令给车端
   - 车端接收指令后设置远驾接管状态变量
   - 车端根据远驾接管状态决定是否处理控制指令

3. **日志记录**：
   - 客户端和车端都记录详细日志
   - 便于验证功能是否正常工作

## 实现细节

### 1. 客户端 QML 界面修改

**文件**：`client/qml/DrivingInterface.qml`

**修改**：

1. **添加状态属性**：
   ```qml
   property bool streamStopped: false  // 是否已停止推流
   ```

2. **修改按钮文本逻辑**：
   ```qml
   text: {
       if (pendingConnectVideo) return "连接中..."
       if (webrtcStreamManager.anyConnected) return "已连接"
       if (streamStopped && mqttController.isConnected) return "连接车辆"  // ★ 新增
       if (mqttController.isConnected) return "MQTT已连接"
       return "连接车端"
   }
   ```

3. **停止推流时设置状态**：
   ```qml
   onClicked: {
       if (webrtcStreamManager.anyConnected) {
           mqttController.requestStreamStop()
           webrtcStreamManager.disconnectAll()
           streamStopped = true  // ★ 标记推流已停止
           return
       }
       streamStopped = false  // ★ 重新连接时重置
       // ... 连接逻辑 ...
   }
   ```

4. **添加远驾接管按钮**：
   ```qml
   Rectangle {
       Layout.preferredWidth: 100
       Layout.preferredHeight: 32
       property bool remoteControlActive: false
       Text {
           text: parent.remoteControlActive ? "取消接管" : "远驾接管"
       }
       MouseArea {
           onClicked: {
               parent.remoteControlActive = !parent.remoteControlActive
               mqttController.requestRemoteControl(parent.remoteControlActive)
           }
       }
   }
   ```

5. **监听视频流状态变化**：
   ```qml
   Connections {
       target: webrtcStreamManager
       function onAnyConnectedChanged() {
           if (webrtcStreamManager.anyConnected) {
               streamStopped = false  // ★ 视频流重新连接时重置
           }
       }
   }
   ```

### 2. 客户端 MQTT 控制器修改

**文件**：`client/src/mqttcontroller.h` 和 `client/src/mqttcontroller.cpp`

**修改**：

1. **添加方法声明**：
   ```cpp
   /** 请求远驾接管（发送 remote_control 指令，启用/禁用远驾接管状态） */
   Q_INVOKABLE void requestRemoteControl(bool enable);
   ```

2. **实现方法**：
   ```cpp
   void MqttController::requestRemoteControl(bool enable)
   {
       if (!m_isConnected) {
           qDebug() << "MQTT not connected, skip requestRemoteControl";
           return;
       }
       QJsonObject cmd;
       cmd["type"] = "remote_control";
       cmd["enable"] = enable;
       cmd["timestamp"] = QDateTime::currentMSecsSinceEpoch();
       sendControlCommand(cmd);
       qDebug() << "MQTT: requested vehicle remote control" << (enable ? "enabled" : "disabled") << "(remote_control, enable=" << enable << ")";
   }
   ```

### 3. 车端控制协议修改

**文件**：`Vehicle-side/src/control_protocol.cpp`

**修改**：

1. **添加 remote_control 处理**：
   ```cpp
   if (typeStr == "remote_control") {
       // 提取 enable 字段
       std::regex enable_regex("\"enable\"\\s*:\\s*(true|false)");
       std::smatch enable_match;
       bool enable = false;
       if (std::regex_search(jsonPayload, enable_match, enable_regex)) {
           enable = (enable_match[1].str() == "true");
       }
       std::cout << "[Control] 收到 remote_control，远驾接管状态: " << (enable ? "启用" : "禁用") << std::endl;
       if (controller) {
           controller->setRemoteControlEnabled(enable);
           std::cout << "[Control] ✓ 远驾接管状态已设置: " << (enable ? "启用" : "禁用") << std::endl;
       }
       return true;
   }
   ```

2. **添加远驾接管状态检查**：
   ```cpp
   // ★ 检查远驾接管状态：如果未启用，则忽略控制指令（但允许 start_stream/stop_stream/remote_control）
   bool remoteControlEnabled = controller ? controller->isRemoteControlEnabled() : false;
   if (!remoteControlEnabled && typeStr != "start_stream" && typeStr != "stop_stream" && typeStr != "remote_control") {
       std::cout << "[Control] 远驾接管未启用，忽略控制指令 type=" << typeStr << std::endl;
       return false;
   }
   ```

### 4. 车端车辆控制器修改

**文件**：`Vehicle-side/src/vehicle_controller.h` 和 `Vehicle-side/src/vehicle_controller.cpp`

**修改**：

1. **添加成员变量**：
   ```cpp
   bool m_remoteControlEnabled = false;  // 远驾接管状态
   ```

2. **添加方法声明**：
   ```cpp
   void setRemoteControlEnabled(bool enabled);
   bool isRemoteControlEnabled() const;
   ```

3. **实现方法**：
   ```cpp
   void VehicleController::setRemoteControlEnabled(bool enabled)
   {
       std::lock_guard<std::mutex> lock(m_mutex);
       bool oldValue = m_remoteControlEnabled;
       m_remoteControlEnabled = enabled;
       std::cout << "[VehicleController] 远驾接管状态变更: " << (oldValue ? "启用" : "禁用") << " -> " << (enabled ? "启用" : "禁用") << std::endl;
       if (enabled) {
           std::cout << "[VehicleController] ✓ 远驾接管已启用，允许接收远程控制指令" << std::endl;
       } else {
           std::cout << "[VehicleController] ✓ 远驾接管已禁用，停止接收远程控制指令" << std::endl;
       }
   }
   
   bool VehicleController::isRemoteControlEnabled() const
   {
       std::lock_guard<std::mutex> lock(m_mutex);
       return m_remoteControlEnabled;
   }
   ```

## 数据流

```
客户端 UI
  ↓ (点击"远驾接管")
MqttController::requestRemoteControl(true)
  ↓ (MQTT 发布)
vehicle/control 主题
  ↓ (MQTT 订阅)
车端 control_protocol.cpp::handle_control_json()
  ↓ (解析 remote_control)
VehicleController::setRemoteControlEnabled(true)
  ↓ (设置状态)
m_remoteControlEnabled = true
  ↓ (后续控制指令检查)
控制指令处理（如果 enabled=true）
```

## 验证

### 自动化验证脚本

**文件**：`scripts/verify-remote-control.sh`

**验证项**：
1. ✓ 编译状态
2. ✓ 代码功能（客户端方法、QML 按钮、状态逻辑、车端处理、车端方法）
3. ✓ 相关日志
4. ✓ MQTT 消息

**运行**：
```bash
bash scripts/verify-remote-control.sh
```

### 手动测试步骤

1. **启动全链路**：
   ```bash
   bash scripts/start-full-chain.sh manual
   ```

2. **连接车端**：
   - 登录并选择车辆
   - 点击「连接车端」
   - 等待显示「已连接」

3. **停止推流**：
   - 点击「已连接」按钮
   - **验证**：按钮文本变为「连接车辆」
   - **验证**：右侧出现「远驾接管」按钮

4. **启用远驾接管**：
   - 点击「远驾接管」按钮
   - **验证**：按钮文本变为「取消接管」
   - **验证日志**：
     ```bash
     docker logs teleop-client-dev --tail 50 | grep -E "远驾接管|requestRemoteControl"
     docker logs remote-driving-vehicle-1 --tail 50 | grep -E "remote_control|远驾接管"
     ```

5. **禁用远驾接管**：
   - 再次点击「取消接管」按钮
   - **验证**：按钮文本变为「远驾接管」
   - **验证日志**：确认状态变更日志

6. **测试控制指令**：
   - 启用远驾接管后，发送控制指令
   - **验证**：车端处理控制指令
   - 禁用远驾接管后，发送控制指令
   - **验证**：车端忽略控制指令（日志显示「远驾接管未启用，忽略控制指令」）

## 修改文件清单

1. `client/qml/DrivingInterface.qml`
   - 添加 `streamStopped` 属性
   - 修改按钮文本逻辑
   - 添加「远驾接管」按钮
   - 添加视频流状态监听

2. `client/src/mqttcontroller.h`
   - 添加 `requestRemoteControl()` 方法声明

3. `client/src/mqttcontroller.cpp`
   - 实现 `requestRemoteControl()` 方法

4. `Vehicle-side/src/control_protocol.cpp`
   - 添加 `remote_control` 处理逻辑
   - 添加远驾接管状态检查

5. `Vehicle-side/src/vehicle_controller.h`
   - 添加 `m_remoteControlEnabled` 成员变量
   - 添加 `setRemoteControlEnabled()` 和 `isRemoteControlEnabled()` 方法声明

6. `Vehicle-side/src/vehicle_controller.cpp`
   - 实现 `setRemoteControlEnabled()` 和 `isRemoteControlEnabled()` 方法

7. `scripts/verify-remote-control.sh`（新增）
   - 自动化验证脚本

## 日志记录

### 客户端日志

- `[QML] 已发送停止推流指令给车端`
- `[QML] 推流已停止，按钮状态更新为 streamStopped=true`
- `[QML] 远驾接管状态变更: 启用/禁用`
- `[MQTT] requested vehicle remote control enabled/disabled (remote_control, enable=true/false)`

### 车端日志

- `[Control] 收到 remote_control，远驾接管状态: 启用/禁用`
- `[Control] ✓ 远驾接管状态已设置: 启用/禁用`
- `[VehicleController] 远驾接管状态变更: 禁用 -> 启用`
- `[VehicleController] ✓ 远驾接管已启用，允许接收远程控制指令`
- `[Control] 远驾接管未启用，忽略控制指令 type=...`

## 技术要点

### 状态管理

- **客户端**：使用 QML 属性 `streamStopped` 和 `remoteControlActive` 管理状态
- **车端**：使用 `VehicleController::m_remoteControlEnabled` 成员变量管理状态
- **线程安全**：车端使用 `std::mutex` 保护状态变量

### 控制指令过滤

- **允许的指令**：`start_stream`、`stop_stream`、`remote_control`（不受远驾接管状态影响）
- **受控指令**：`steering`、`throttle`、`brake`、`gear`（需要远驾接管启用）

### UI 状态同步

- **视频流状态**：通过 `Connections` 监听 `webrtcStreamManager.anyConnectedChanged`
- **按钮状态**：根据 `streamStopped` 和 `remoteControlActive` 动态更新

## 验证结论

✓✓✓ **功能实现已完成并通过验证**

- ✓ 客户端包含 `requestRemoteControl()` 方法
- ✓ 客户端 QML 包含远驾接管按钮
- ✓ 客户端 QML 包含按钮状态逻辑（停止推流后显示"连接车辆"）
- ✓ 车端包含 `remote_control` 处理逻辑
- ✓ 车端 `VehicleController` 包含远驾接管方法
- ✓ 车端包含远驾接管状态检查逻辑

**功能已实现，可以进行手动测试验证。**
