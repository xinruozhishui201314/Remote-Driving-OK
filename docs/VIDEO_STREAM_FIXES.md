# 视频流问题修复文档

## 问题分析

根据终端日志分析，发现以下问题：

### 1. QML中QImage的size访问问题
- **现象**：日志显示 `[QML] 前方摄像头收到视频帧，size=unknown`
- **原因**：QML中访问QImage的`size`属性不正确，应该使用`width`和`height`属性
- **影响**：虽然不影响功能，但日志无法正确显示视频帧尺寸，影响调试

### 2. WebRTC连接断开后无自动重连
- **现象**：日志显示 `cam_front`、`cam_left`、`cam_right` 连接断开（Disconnected → Closed），只有`cam_rear`持续工作
- **原因**：连接断开后没有自动重连机制
- **影响**：视频流中断后无法自动恢复，需要手动重新连接

## 修复方案

### 修复1：QML中QImage的size访问
**文件**：`client/qml/DrivingInterface.qml`

**修改前**：
```qml
var imgSize = image ? (image.size ? (image.size.width + "x" + image.size.height) : "unknown") : "null"
```

**修改后**：
```qml
var imgSize = image ? ((image.width && image.height) ? (image.width + "x" + image.height) : "unknown") : "null"
```

**说明**：QImage在QML中通过`width`和`height`属性访问尺寸，而不是`size`对象。

### 修复2：WebRTC连接断开后自动重连
**文件**：
- `client/src/webrtcclient.h`
- `client/src/webrtcclient.cpp`

**新增成员变量**：
```cpp
int m_reconnectCount = 0;  // 连接断开后自动重连计数，最多重试 5 次
bool m_manualDisconnect = false;  // 是否手动断开连接（手动断开时不自动重连）
```

**修改点**：
1. **`connectToStream()`**：重置`m_manualDisconnect = false`和`m_reconnectCount = 0`
2. **`disconnect()`**：设置`m_manualDisconnect = true`，标记为手动断开
3. **`onStateChange()`**：当连接断开（Disconnected/Failed/Closed）时：
   - 检查是否为手动断开（`!m_manualDisconnect`）
   - 检查流URL是否有效（`!m_stream.isEmpty() && !m_serverUrl.isEmpty()`）
   - 如果满足条件且重连次数未达上限（`m_reconnectCount < 5`），则5秒后自动重连
   - 连接成功后重置`m_reconnectCount = 0`

**重连策略**：
- 最多重试5次
- 每次重连间隔5秒
- 手动断开时不自动重连
- 连接成功后重置重连计数

## 验证方法

### 1. 验证QImage size日志
运行客户端后，查看日志中是否显示正确的视频帧尺寸：
```bash
docker logs teleop-client-dev 2>&1 | grep "前方摄像头收到视频帧"
```

**预期输出**：
```
[QML] 前方摄像头收到视频帧，size=1600x900
```

### 2. 验证自动重连功能
1. 启动完整系统：`bash scripts/start-full-chain.sh manual`
2. 等待视频流连接成功
3. 模拟连接断开（例如停止ZLM服务或网络中断）
4. 观察日志中的自动重连信息：
```bash
docker logs teleop-client-dev 2>&1 | grep -E "连接断开|自动重连|PeerConnection state"
```

**预期输出**：
```
[WebRTC] PeerConnection state stream= "cam_front" state= Disconnected ( 3 )
[WebRTC] 连接断开，第1次自动重连（最多5次），还剩4次，5s后重连 stream= cam_front
[WebRTC] 拉流尝试 stream= cam_front（若 stream not found 将最多重试 8 次，间隔 3s）
[WebRTC] PeerConnection state stream= "cam_front" state= Connected ( 2 )
```

### 3. 验证手动断开不重连
1. 在客户端UI中手动断开连接
2. 观察日志，确认没有自动重连尝试

**预期输出**：
```
[WebRTC] PeerConnection state stream= "cam_front" state= Closed ( 5 )
（无自动重连日志）
```

## 相关文件

- `client/qml/DrivingInterface.qml` - QML视频帧处理
- `client/src/webrtcclient.h` - WebRTC客户端头文件
- `client/src/webrtcclient.cpp` - WebRTC客户端实现

## 注意事项

1. **自动重连限制**：最多重试5次，避免无限重连消耗资源
2. **手动断开优先**：手动断开连接时不会触发自动重连
3. **重连间隔**：5秒间隔，给服务端足够时间恢复
4. **连接成功重置**：连接成功后重置重连计数，确保下次断开时重新计数

## 后续优化建议

1. **可配置重连参数**：将重连次数和间隔时间配置化
2. **指数退避**：重连间隔采用指数退避策略（5s → 10s → 20s → ...）
3. **重连状态通知**：在UI中显示重连状态和剩余次数
4. **连接质量监控**：监控连接质量，在质量下降时主动重连
