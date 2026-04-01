# 断开连接崩溃修复文档

## 问题描述

点击"已连接"按钮后，程序发生段错误（Segmentation Fault）崩溃。

### 崩溃堆栈
```
=== Segmentation fault - backtrace ===
...
2: /opt/Qt/6.8.0/gcc_64/lib/libQt6Core.so.6(_ZN7QObject10disconnectEPKS_PKcS1_S3_+0x20e)
3: ./RemoteDrivingClient(_ZNK7QObject10disconnectEPKS_PKc+0x31)
4: ./RemoteDrivingClient(_ZN12WebRtcClient10disconnectEv+0x70)
5: ./RemoteDrivingClient(_ZN19WebRtcStreamManager13disconnectAllEv+0x2d)
```

### 问题原因

1. **竞态条件**：当连接断开时，`onStateChange` 回调被触发，启动了自动重连定时器（`QTimer::singleShot`）。在 `disconnect()` 执行过程中，定时器回调可能也在执行，导致竞态条件。

2. **对象生命周期问题**：在 `disconnect()` 中调用 `reply->disconnect(this)` 时，`reply` 可能已经被删除或正在被删除，导致段错误。

3. **无法取消的定时器**：使用 `QTimer::singleShot` 创建的定时器无法在 `disconnect()` 时取消，导致即使手动断开连接，定时器仍会触发重连。

## 修复方案

### 1. 使用成员定时器管理重连
**文件**: `client/src/webrtcclient.h`, `client/src/webrtcclient.cpp`

- 添加 `QTimer *m_reconnectTimer` 成员变量
- 在构造函数中初始化定时器并连接信号
- 使用成员定时器替代 `QTimer::singleShot`，可以在 `disconnect()` 时取消

### 2. 在 disconnect() 开始时停止定时器
**文件**: `client/src/webrtcclient.cpp`

```cpp
void WebRtcClient::disconnect()
{
    // ★ 首先停止所有定时器，防止竞态条件
    if (m_reconnectTimer && m_reconnectTimer->isActive()) {
        m_reconnectTimer->stop();
    }
    
    // ... 其他断开逻辑
}
```

### 3. 安全地断开网络回复连接
**文件**: `client/src/webrtcclient.cpp`

使用 `QPointer` 检查对象有效性：

```cpp
QNetworkReply *reply = m_currentReply;
m_currentReply = nullptr;
if (reply) {
    // 使用 QPointer 检查对象有效性，避免在对象已删除时调用 disconnect
    QPointer<QNetworkReply> safeReply(reply);
    if (safeReply) {
        safeReply->disconnect(this);
        safeReply->abort();
    }
    reply->deleteLater();
}
```

### 4. 修改自动重连逻辑
**文件**: `client/src/webrtcclient.cpp`

将 `QTimer::singleShot` 改为使用成员定时器：

```cpp
// 修改前：
QTimer::singleShot(5000, this, [this]() {
    if (!m_manualDisconnect) {
        m_offerSent = false;
        doConnect();
    }
});

// 修改后：
if (m_reconnectTimer) {
    m_reconnectTimer->start();
}
```

## 修改的文件

1. `client/src/webrtcclient.h`
   - 添加 `#include <QTimer>`
   - 添加 `QTimer *m_reconnectTimer` 成员变量

2. `client/src/webrtcclient.cpp`
   - 添加 `#include <QPointer>`
   - 在构造函数中初始化 `m_reconnectTimer` 并连接信号
   - 在 `disconnect()` 中停止定时器
   - 使用 `QPointer` 安全地断开 `reply` 连接
   - 修改自动重连逻辑使用成员定时器

## 验证方法

1. **启动系统**：
   ```bash
   bash scripts/start-full-chain.sh manual
   ```

2. **连接视频流**：
   - 在客户端UI中点击"连接车端"
   - 等待视频流连接成功（显示"已连接"）

3. **测试断开连接**：
   - 点击"已连接"按钮
   - 观察程序是否正常断开，不再崩溃
   - 检查日志确认没有自动重连尝试

4. **检查日志**：
   ```bash
   docker logs teleop-client-dev --tail 50 | grep -E "disconnect|重连|stop_stream"
   ```

**预期输出**：
```
[MQTT] requested vehicle to stop stream (stop_stream)
[QML] 已发送停止推流指令给车端
[WebRTC] 手动断开连接，不自动重连 stream= "cam_front"
[WebRTC] 手动断开连接，不自动重连 stream= "cam_rear"
...
（无崩溃，无自动重连）
```

## 技术细节

### QPointer 的作用
`QPointer` 是Qt提供的智能指针，用于跟踪 `QObject` 派生类的对象。当对象被删除时，`QPointer` 会自动变为 `nullptr`，可以安全地检查对象是否仍然存在。

### 成员定时器 vs QTimer::singleShot
- **QTimer::singleShot**：无法取消，即使对象被删除，定时器仍会触发
- **成员定时器**：可以在 `disconnect()` 时调用 `stop()` 取消，避免竞态条件

### 修复顺序的重要性
1. 首先停止定时器（防止新的重连尝试）
2. 设置 `m_manualDisconnect = true`（防止定时器回调中的重连）
3. 安全地断开网络连接
4. 清理其他资源

## 相关文件

- `client/src/webrtcclient.h` - WebRTC客户端头文件
- `client/src/webrtcclient.cpp` - WebRTC客户端实现
- `client/src/webrtcstreammanager.cpp` - WebRTC流管理器

## 注意事项

1. **定时器生命周期**：`m_reconnectTimer` 是 `WebRtcClient` 的子对象，会在析构时自动删除
2. **线程安全**：所有操作都在主线程（Qt事件循环）中执行，无需额外同步
3. **对象有效性**：使用 `QPointer` 检查对象有效性，避免访问已删除的对象
