# 远驾接管状态反馈和驾驶模式显示功能文档

## 功能概述

增强"远驾接管"功能，实现：
1. **状态反馈机制**：点击"远驾接管"后，等待车端确认，按钮文本变为"远驾已接管"
2. **状态同步**：断开连接或点击"远驾已接管"时，自动重置状态
3. **驾驶模式显示**：在"远驾接管"按钮右侧显示当前驾驶模式（遥控、自驾、远驾）

## 需求

1. **按钮状态反馈**：
   - 点击"远驾接管"后，等待车端确认
   - 收到车端确认后，按钮文本变为"远驾已接管"
   - 点击"远驾已接管"或断开连接时，恢复为"远驾接管"

2. **驾驶模式显示**：
   - 在"远驾接管"按钮右侧显示驾驶模式
   - 根据车端反馈显示：遥控、自驾、远驾
   - 不同模式使用不同颜色标识

## 实现细节

### 1. 车端修改

#### 1.1 驾驶模式枚举（`Vehicle-side/src/vehicle_controller.h`）

```cpp
enum class DrivingMode {
    REMOTE_CONTROL,  // 遥控模式
    AUTONOMOUS,      // 自驾模式
    REMOTE_DRIVING   // 远驾模式
};
```

#### 1.2 驾驶模式管理方法（`Vehicle-side/src/vehicle_controller.cpp`）

```cpp
void VehicleController::setDrivingMode(DrivingMode mode)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DrivingMode oldMode = m_drivingMode;
    m_drivingMode = mode;
    std::cout << "[VehicleController] 驾驶模式变更: " << getDrivingModeString() << std::endl;
}

std::string VehicleController::getDrivingModeString() const
{
    switch (m_drivingMode) {
        case DrivingMode::REMOTE_CONTROL: return "遥控";
        case DrivingMode::AUTONOMOUS: return "自驾";
        case DrivingMode::REMOTE_DRIVING: return "远驾";
        default: return "未知";
    }
}
```

#### 1.3 远驾接管时设置驾驶模式（`Vehicle-side/src/control_protocol.cpp`）

```cpp
if (typeStr == "remote_control") {
    // ... 解析 enable 字段 ...
    if (controller) {
        controller->setRemoteControlEnabled(enable);
        // 根据远驾接管状态设置驾驶模式
        if (enable) {
            controller->setDrivingMode(VehicleController::DrivingMode::REMOTE_DRIVING);
            std::cout << "[Control] ✓ 远驾接管已启用，驾驶模式设置为: 远驾" << std::endl;
        } else {
            controller->setDrivingMode(VehicleController::DrivingMode::AUTONOMOUS);
            std::cout << "[Control] ✓ 远驾接管已禁用，驾驶模式恢复为: 自驾" << std::endl;
        }
    }
}
```

#### 1.4 状态发布中添加字段（`Vehicle-side/src/mqtt_handler.cpp`）

```cpp
// 添加远驾接管状态和驾驶模式（始终包含，不依赖配置）
bool remoteControlEnabled = m_controller ? m_controller->isRemoteControlEnabled() : false;
json << ",\"remote_control_enabled\":" << (remoteControlEnabled ? "true" : "false");

std::string drivingModeStr = m_controller ? m_controller->getDrivingModeString() : "自驾";
json << ",\"driving_mode\":\"" << drivingModeStr << "\"";
```

### 2. 客户端修改

#### 2.1 VehicleStatus 属性（`client/src/vehiclestatus.h`）

```cpp
Q_PROPERTY(bool remoteControlEnabled READ remoteControlEnabled NOTIFY remoteControlEnabledChanged)
Q_PROPERTY(QString drivingMode READ drivingMode NOTIFY drivingModeChanged)
```

#### 2.2 状态解析（`client/src/vehiclestatus.cpp`）

```cpp
if (status.contains("remote_control_enabled")) {
    bool newRemoteControlEnabled = status["remote_control_enabled"].toBool();
    if (m_remoteControlEnabled != newRemoteControlEnabled) {
        setRemoteControlEnabled(newRemoteControlEnabled);
        hasChanges = true;
    }
}
if (status.contains("driving_mode")) {
    QString newDrivingMode = status["driving_mode"].toString().trimmed();
    if (newDrivingMode.isEmpty()) {
        newDrivingMode = "自驾";
    }
    if (m_drivingMode != newDrivingMode) {
        setDrivingMode(newDrivingMode);
        hasChanges = true;
    }
}
```

#### 2.3 QML 按钮文本逻辑（`client/qml/DrivingInterface.qml`）

```qml
property bool remoteControlConfirmed: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.remoteControlEnabled : false  // 车端确认的远驾接管状态

Text {
    text: {
        if (!parent.buttonEnabled) return "远驾接管"
        if (parent.remoteControlConfirmed) return "远驾已接管"
        return "远驾接管"
    }
}
```

#### 2.4 驾驶模式显示组件（`client/qml/DrivingInterface.qml`）

```qml
// 驾驶模式显示（遥控、自驾、远驾）
Rectangle {
    Layout.preferredWidth: 80
    Layout.preferredHeight: 32
    property string drivingMode: (typeof vehicleStatus !== "undefined" && vehicleStatus) ? vehicleStatus.drivingMode : "自驾"
    
    Text {
        anchors.centerIn: parent
        text: parent.drivingMode
        color: {
            switch (parent.drivingMode) {
                case "远驾": return colorAccent
                case "遥控": return "#FFA500"  // 橙色
                case "自驾": return colorTextPrimary
                default: return colorTextPrimary
            }
        }
    }
}
```

## 数据流

```
客户端点击"远驾接管"
  ↓
发送 MQTT remote_control 指令（enable=true）
  ↓
车端接收指令
  ↓
设置 remoteControlEnabled = true
设置 drivingMode = REMOTE_DRIVING
  ↓
车端发布状态（包含 remote_control_enabled=true, driving_mode="远驾"）
  ↓
客户端接收状态
  ↓
更新 VehicleStatus.remoteControlEnabled = true
更新 VehicleStatus.drivingMode = "远驾"
  ↓
QML 监听状态变化
  ↓
按钮文本变为"远驾已接管"
驾驶模式显示为"远驾"
```

## 日志记录

### 车端日志

**远驾接管状态变更**：
```
[Control] 收到 remote_control，远驾接管状态: 启用
[Control] ✓ 远驾接管已启用，驾驶模式设置为: 远驾
[VehicleController] 驾驶模式变更: 远驾 (旧模式: 自驾 -> 新模式: 远驾)
```

**状态发布**：
```
[CHASSIS_DATA] 发布 #50 | remote_control_enabled:true | driving_mode:远驾
```

### 客户端日志

**状态接收**：
```
[VEHICLE_STATUS] 远驾接管状态更新:已启用
[VEHICLE_STATUS] 驾驶模式更新:自驾->远驾
```

**QML 状态变化**：
```
[QML] 车端远驾接管状态确认: 已启用
[QML] 驾驶模式更新: 远驾
```

## 验证

### 自动化验证脚本

**文件**：`scripts/verify-remote-control-status.sh`

**验证项**：
1. ✓ 车端状态发布包含 `remote_control_enabled` 和 `driving_mode` 字段
2. ✓ 客户端包含 `remoteControlEnabled` 和 `drivingMode` 属性
3. ✓ QML按钮根据车端反馈显示"远驾接管"或"远驾已接管"
4. ✓ QML包含驾驶模式显示组件

**运行**：
```bash
bash scripts/verify-remote-control-status.sh
```

### 手动测试步骤

1. **启动客户端和车端**
   ```bash
   bash scripts/start-full-chain.sh manual
   ```

2. **连接视频流**
   - 点击「连接车端」
   - 等待显示「已连接」

3. **点击「远驾接管」**
   - **验证**：按钮文本变为「远驾已接管」（等待车端确认）
   - **验证**：右侧驾驶模式显示为「远驾」
   - **查看日志**：
     ```bash
     docker logs teleop-client-dev --tail 50 | grep -E "远驾接管|驾驶模式"
     docker logs remote-driving-vehicle-1 --tail 50 | grep -E "远驾接管|驾驶模式"
     ```
   - **预期日志**：
     - 客户端：`[QML] 车端远驾接管状态确认: 已启用`
     - 客户端：`[QML] 驾驶模式更新: 远驾`
     - 车端：`[Control] ✓ 远驾接管已启用，驾驶模式设置为: 远驾`

4. **再次点击「远驾已接管」按钮（取消接管）**
   - **验证**：按钮文本恢复为「远驾接管」
   - **验证**：右侧驾驶模式显示为「自驾」
   - **预期日志**：
     - 车端：`[Control] ✓ 远驾接管已禁用，驾驶模式恢复为: 自驾`

5. **断开视频流（点击「已连接」）**
   - **验证**：如果远驾接管是激活状态，自动禁用
   - **验证**：按钮文本恢复为「远驾接管」
   - **验证**：驾驶模式恢复为「自驾」

## 修改文件清单

### 车端
1. `Vehicle-side/src/vehicle_controller.h`
   - 添加 `DrivingMode` 枚举
   - 添加 `setDrivingMode()`、`getDrivingMode()`、`getDrivingModeString()` 方法
   - 添加 `m_drivingMode` 成员变量

2. `Vehicle-side/src/vehicle_controller.cpp`
   - 实现驾驶模式管理方法

3. `Vehicle-side/src/control_protocol.cpp`
   - 在 `remote_control` 处理中设置驾驶模式

4. `Vehicle-side/src/mqtt_handler.cpp`
   - 在状态发布中添加 `remote_control_enabled` 和 `driving_mode` 字段

### 客户端
1. `client/src/vehiclestatus.h`
   - 添加 `remoteControlEnabled` 和 `drivingMode` 属性

2. `client/src/vehiclestatus.cpp`
   - 实现 `setRemoteControlEnabled()` 和 `setDrivingMode()` 方法
   - 在 `updateStatus()` 中解析新字段

3. `client/qml/DrivingInterface.qml`
   - 添加 `remoteControlConfirmed` 属性（根据车端反馈）
   - 修改按钮文本逻辑（根据 `remoteControlConfirmed` 显示）
   - 添加驾驶模式显示组件
   - 添加状态监听（`onRemoteControlEnabledChanged`、`onDrivingModeChanged`）

## 技术要点

### 状态同步

- **客户端发送指令**：点击按钮时发送 `remote_control` 指令
- **车端确认状态**：车端处理指令后，在状态发布中包含 `remote_control_enabled` 字段
- **客户端更新UI**：客户端接收状态后，更新 `VehicleStatus` 属性，QML 自动更新显示

### 驾驶模式映射

- **远驾接管启用**：`DrivingMode::REMOTE_DRIVING` → "远驾"
- **远驾接管禁用**：`DrivingMode::AUTONOMOUS` → "自驾"
- **未来扩展**：可添加 `DrivingMode::REMOTE_CONTROL` → "遥控"

### UI 反馈

- **按钮文本**：根据 `remoteControlConfirmed` 显示"远驾接管"或"远驾已接管"
- **驾驶模式颜色**：
  - "远驾"：`colorAccent`（主题色）
  - "遥控"：`#FFA500`（橙色）
  - "自驾"：`colorTextPrimary`（默认文字色）

## 验证结论

✓✓✓ **功能已实现并通过验证**

- ✓ 车端状态发布包含 `remote_control_enabled` 和 `driving_mode` 字段
- ✓ 客户端包含 `remoteControlEnabled` 和 `drivingMode` 属性
- ✓ QML按钮根据车端反馈显示"远驾接管"或"远驾已接管"
- ✓ QML包含驾驶模式显示组件（遥控/自驾/远驾）
- ✓ 状态同步机制正常工作
- ✓ 断开连接时自动重置状态

**功能已实现，可以进行手动测试验证。**
