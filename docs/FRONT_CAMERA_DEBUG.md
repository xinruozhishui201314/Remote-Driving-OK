# 前方摄像头无画面问题诊断与修复

## 问题现象

客户端连接后，前方摄像头（cam_front）没有显示画面，但日志显示：
- WebRTC 连接成功
- H264 解码器正常工作
- 视频帧已解码（日志显示 "出帧 #1, #2, #3..."）

## 根本原因分析

从代码分析来看，视频帧传递链路如下：

1. **H264Decoder** → `emit frameReady(QImage)` (h264decoder.cpp:614)
2. **WebRtcClient** → `onVideoFrameFromDecoder()` → `emit videoFrameReady(QImage)` (webrtcclient.cpp)
3. **QML Connections** → `onVideoFrameReady(image)` → `frontVideoRenderer.setFrame(image)` (DrivingInterface.qml:512)

可能的问题点：
- QML 信号连接可能失败（target 为 null）
- 视频帧参数传递问题（QML 中参数名不匹配）
- VideoRenderer 渲染问题

## 已实施的修复

### 1. 添加调试日志

在以下位置添加了详细的调试日志：

**webrtcclient.cpp**:
```cpp
void WebRtcClient::onVideoFrameFromDecoder(const QImage &image)
{
    if (image.isNull()) {
        qWarning() << "[WebRTC] 收到空视频帧 stream=" << m_stream;
        return;
    }
    qDebug() << "[WebRTC] 转发视频帧 stream=" << m_stream 
             << "size=" << image.size() << "format=" << image.format();
    emit videoFrameReady(image);
}
```

**src/presentation/renderers/VideoRenderer.cpp**:
```cpp
void VideoRenderer::setFrame(const QImage& image)
{
    // 新实现：qImageToYuv420Frame → 三缓冲 deliverFrame → updatePaintNode(GPU)
    // 详见 client/src/presentation/renderers/VideoRenderer.cpp
}
```

**DrivingInterface.qml**:
```qml
function onVideoFrameReady(image) {
    console.log("[QML] 前方摄像头收到视频帧，size=" + 
                (image ? (image.width + "x" + image.height) : "null"))
    if (image && frontVideoRenderer) {
        frontVideoRenderer.setFrame(image)
        console.log("[QML] 前方摄像头视频帧已设置到渲染器")
    } else {
        console.warn("[QML] 前方摄像头视频帧无效或渲染器不存在", 
                     image, frontVideoRenderer)
    }
}
```

### 2. 修复信号连接

将 `H264Decoder::frameReady` 直接连接到 `WebRtcClient::videoFrameReady` 改为：
- 先连接到 `onVideoFrameFromDecoder` 槽函数
- 在槽函数中添加验证和日志
- 再 emit `videoFrameReady` 信号

这样可以：
- 确保在主线程中处理（Qt::QueuedConnection）
- 添加空帧检查
- 记录详细的调试信息

## 验证步骤

1. **重新编译客户端**：
   ```bash
   cd client
   mkdir -p build && cd build
   cmake .. && make
   ```

2. **运行客户端并查看日志**：
   ```bash
   ./RemoteDrivingClient 2>&1 | grep -E "\[WebRTC\]|\[VideoRenderer\]|\[QML\]|前方摄像头"
   ```

3. **检查日志输出**：
   - 应该看到 `[WebRTC] 转发视频帧 stream=cam_front`
   - 应该看到 `[VideoRenderer] 设置视频帧`
   - 应该看到 `[QML] 前方摄像头收到视频帧`

4. **如果仍然没有画面**：
   - 检查 `frontVideoRenderer` 是否为 null
   - 检查 `webrtcStreamManager.frontClient` 是否为 null
   - 检查视频帧尺寸和格式是否正确

## 常见问题排查

### 问题 1: QML Connections target 为 null

**现象**：日志中没有 `[QML] 前方摄像头收到视频帧`

**原因**：`webrtcStreamManager.frontClient` 可能为 null

**解决方法**：
- 确保 `webrtcStreamManager` 已正确初始化
- 确保 `connectFourStreams()` 已调用
- 检查 `DrivingInterface.qml` 中 `webrtcStreamManager` 的绑定

### 问题 2: 视频帧格式不正确

**现象**：日志显示收到视频帧，但画面为黑色

**原因**：QImage 格式可能不被支持

**解决方法**：
- 检查 `image.format()` 是否为 `QImage::Format_RGB888`
- 如果格式不正确，在 `VideoRenderer::setFrame()` 中转换格式

### 问题 3: VideoRenderer 未正确渲染

**现象**：日志显示 `setFrame()` 已调用，但画面不更新

**原因**：`update()` 可能未触发重绘

**解决方法**：
- 确保 `VideoRenderer` 的 `visible` 属性为 true
- 确保 `VideoRenderer` 的尺寸不为 0
- 检查 `paint()` 方法是否被调用

## 后续优化建议

1. **添加视频帧统计**：
   - 记录每秒接收的帧数（FPS）
   - 记录丢帧数量
   - 记录解码延迟

2. **优化渲染性能**：
   - 使用硬件加速（如果可用）
   - 减少不必要的图像拷贝
   - 使用双缓冲减少闪烁

3. **添加错误恢复机制**：
   - 检测长时间无帧的情况
   - 自动重连 WebRTC
   - 显示错误提示

## 相关文件

- `client/src/webrtcclient.cpp` - WebRTC 客户端实现
- `client/src/webrtcclient.h` - WebRTC 客户端头文件
- `client/src/presentation/renderers/VideoRenderer.cpp` - GPU 加速视频渲染器实现
- `client/src/h264decoder.cpp` - H264 解码器实现
- `client/qml/DrivingInterface.qml` - 主驾驶界面 QML
- `client/qml/VideoView.qml` - 视频显示组件 QML
