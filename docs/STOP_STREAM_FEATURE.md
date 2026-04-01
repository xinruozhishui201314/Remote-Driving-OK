# 停止推流功能文档

## 功能概述

在客户端主驾驶页面上，当视频流连接成功后，"连接车端"按钮会显示为"已连接"。点击"已连接"按钮后，系统会：
1. 向车端发送 `stop_stream` 指令
2. 车端接收指令后停止推流进程
3. 客户端断开视频流连接

## 实现细节

### 客户端实现

#### 1. MqttController 新增方法
**文件**: `client/src/mqttcontroller.h`, `client/src/mqttcontroller.cpp`

新增 `requestStreamStop()` 方法，用于发送停止推流指令：

```cpp
Q_INVOKABLE void requestStreamStop();
```

实现：
```cpp
void MqttController::requestStreamStop()
{
    if (!m_isConnected) {
        qDebug() << "MQTT not connected, skip requestStreamStop";
        return;
    }
    QJsonObject cmd;
    cmd["type"] = "stop_stream";
    cmd["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    sendControlCommand(cmd);
    qDebug() << "MQTT: requested vehicle to stop stream (stop_stream)";
}
```

#### 2. DrivingInterface.qml 修改
**文件**: `client/qml/DrivingInterface.qml`

修改"已连接"按钮的点击事件处理：

```qml
onClicked: {
    if (typeof webrtcStreamManager !== "undefined" && webrtcStreamManager && webrtcStreamManager.anyConnected) {
        // 先发送停止推流指令给车端
        if (typeof mqttController !== "undefined" && mqttController && mqttController.isConnected) {
            mqttController.requestStreamStop()
            console.log("[QML] 已发送停止推流指令给车端")
        }
        // 然后断开视频流连接
        webrtcStreamManager.disconnectAll()
        return
    }
    // ... 其他连接逻辑
}
```

### 车端实现

#### 1. control_protocol.cpp 新增停止推流处理
**文件**: `Vehicle-side/src/control_protocol.cpp`

在 `handle_control_json()` 函数中添加 `stop_stream` 指令处理：

```cpp
if (typeStr == "stop_stream") {
    std::cout << "[Control] 收到 stop_stream，停止推流进程" << std::endl;
    // ★ 标记推流应该停止
    g_streaming_should_be_running = false;
    // ★ 停止推流进程
    stop_streaming_processes();
    return true;
}
```

#### 2. 新增 stop_streaming_processes() 函数
**文件**: `Vehicle-side/src/control_protocol.cpp`

实现停止推流进程的函数：

```cpp
static void stop_streaming_processes()
{
    // 1. 读取PID文件，获取推流脚本进程ID
    // 2. 发送 SIGTERM 信号给推流脚本进程
    // 3. 停止所有相关的 ffmpeg 进程
    // 4. 清理PID文件
    // 5. 验证推流进程已停止
}
```

**停止流程**：
1. 检查并停止 NuScenes 推流脚本（通过 `/tmp/push-nuscenes-cameras.pid`）
2. 检查并停止测试图案推流脚本（通过 `/tmp/push-testpattern.pid`）
3. 停止所有相关的 ffmpeg 进程（通过进程名匹配）
4. 等待500ms确认进程已停止
5. 输出日志确认停止结果

## 使用流程

1. **启动推流**：
   - 客户端点击"连接车端"按钮
   - 系统发送 `start_stream` 指令
   - 车端启动推流进程

2. **停止推流**：
   - 客户端点击"已连接"按钮
   - 系统发送 `stop_stream` 指令
   - 车端停止推流进程
   - 客户端断开视频流连接

## 验证方法

### 自动化验证脚本
运行验证脚本：
```bash
bash scripts/verify-stop-stream.sh
```

脚本会：
1. 检查推流状态
2. 发送停止推流指令
3. 等待推流停止
4. 检查PID文件清理
5. 检查车端日志
6. 输出验证结果

### 手动验证步骤

1. **启动系统**：
   ```bash
   bash scripts/start-full-chain.sh manual
   ```

2. **启动推流**：
   - 在客户端UI中点击"连接车端"
   - 等待视频流连接成功（显示"已连接"）

3. **检查推流状态**：
   ```bash
   docker exec <vehicle-container> ps aux | grep ffmpeg
   ```

4. **停止推流**：
   - 在客户端UI中点击"已连接"按钮
   - 观察车端日志：
     ```bash
     docker logs <vehicle-container> --tail 20 | grep stop_stream
     ```

5. **验证推流已停止**：
   ```bash
   docker exec <vehicle-container> ps aux | grep ffmpeg
   # 应该没有输出或只有grep进程本身
   ```

## 日志输出

### 客户端日志
```
[MQTT] requested vehicle to stop stream (stop_stream)
[QML] 已发送停止推流指令给车端
```

### 车端日志
```
[Control] 收到 stop_stream，停止推流进程
[Control] 停止推流脚本进程 PID=12345
[Control] ✓ 已发送停止信号给推流脚本 (PID=12345)
[Control] ✓ 已尝试停止所有相关 ffmpeg 进程
[Control] ✓ 推流进程已成功停止
```

## 注意事项

1. **优雅停止**：使用 `SIGTERM` 信号停止进程，允许进程清理资源
2. **PID文件清理**：停止推流后会自动清理PID文件
3. **进程验证**：停止后会等待500ms并验证进程已停止
4. **MQTT连接检查**：只有在MQTT连接时才会发送停止指令
5. **推流状态标记**：停止推流后设置 `g_streaming_should_be_running = false`，防止自动恢复推流

## 相关文件

- `client/src/mqttcontroller.h` - MQTT控制器头文件
- `client/src/mqttcontroller.cpp` - MQTT控制器实现
- `client/qml/DrivingInterface.qml` - 主驾驶界面
- `Vehicle-side/src/control_protocol.cpp` - 车端控制协议处理
- `scripts/verify-stop-stream.sh` - 验证脚本

## 故障排查

### 推流未停止
1. 检查MQTT连接是否正常
2. 检查车端日志是否收到 `stop_stream` 指令
3. 手动检查推流进程：
   ```bash
   docker exec <vehicle-container> ps aux | grep ffmpeg
   ```
4. 手动停止推流进程：
   ```bash
   docker exec <vehicle-container> kill -TERM <pid>
   ```

### PID文件未清理
1. 检查PID文件是否存在：
   ```bash
   docker exec <vehicle-container> ls -l /tmp/push-*.pid
   ```
2. 手动清理PID文件：
   ```bash
   docker exec <vehicle-container> rm -f /tmp/push-*.pid
   ```
