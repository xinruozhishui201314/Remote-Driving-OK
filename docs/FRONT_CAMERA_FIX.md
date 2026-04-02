# 前方摄像头无画面问题修复总结

## 问题诊断

从日志分析发现：
1. ✅ **视频帧正常解码**：`[H264] 出帧 #200 w=1600 h=900`
2. ✅ **信号正常传递**：`[WebRTC] 转发视频帧 stream="cam_front"`
3. ✅ **QML 正常接收**：`[QML] 前方摄像头收到视频帧`
4. ✅ **渲染器正常设置**：`[VideoRenderer] 设置视频帧`
5. ❌ **画面不显示**：VideoRenderer 被其他元素遮挡

## 根本原因

在 `DrivingInterface.qml` 中，`VideoRenderer` 被后续的 `Canvas` 和 `Text` 元素遮挡：

```qml
VideoRenderer {
    id: frontVideoRenderer
    anchors.fill: parent
    // ❌ 没有设置 z 值，可能被遮挡
}
Canvas {
    anchors.fill: parent
    visible: forwardMode
    // ❌ 遮挡了 VideoRenderer
}
Text {
    anchors.centerIn: parent
    // ❌ 遮挡了 VideoRenderer
}
```

## 修复方案

### 1. 设置正确的 z-order

```qml
VideoRenderer {
    id: frontVideoRenderer
    anchors.fill: parent
    z: 10  // ✅ 确保在最上层
    visible: hasVideoFrame  // ✅ 有视频时显示
}
```

### 2. 添加视频帧状态跟踪

```qml
property bool hasVideoFrame: false

Connections {
    function onVideoFrameReady(image) {
        if (image && frontVideoRenderer) {
            frontVideoRenderer.setFrame(image)
            mainCameraView.hasVideoFrame = true  // ✅ 标记有视频帧
        }
    }
    function onConnectionStatusChanged(connected) {
        if (!connected) {
            mainCameraView.hasVideoFrame = false  // ✅ 连接断开时清除标记
        }
    }
}
```

### 3. 条件显示占位元素

```qml
Canvas {
    anchors.fill: parent
    z: 1  // ✅ 在视频下方
    visible: forwardMode && !mainCameraView.hasVideoFrame  // ✅ 只在无视频时显示
}

Text {
    anchors.centerIn: parent
    z: 2  // ✅ 在 Canvas 上方，但在视频下方
    visible: !mainCameraView.hasVideoFrame  // ✅ 只在无视频时显示
}
```

### 4. 优化 VideoRenderer 渲染

**src/presentation/renderers/VideoRenderer.cpp**:
- GPU 加速渲染：QQuickItem + QSG + 三缓冲
- 详见 `client/src/presentation/renderers/VideoRenderer.cpp`

### 5. 修复 QML 中 QImage 属性访问

虽然不影响功能，但修复了日志中的 `undefined` 问题：

```qml
// ❌ 错误方式
image.width + "x" + image.height

// ✅ 正确方式
image.size ? (image.size.width + "x" + image.size.height) : "unknown"
```

## 修改文件清单

1. **client/qml/DrivingInterface.qml**
   - 添加 `hasVideoFrame` 属性跟踪视频帧状态
   - 设置 VideoRenderer 的 `z: 10` 确保在最上层
   - 添加 `visible` 条件控制 VideoRenderer 显示
   - 修改 Canvas 和 Text 的 `visible` 条件，只在无视频时显示
   - 优化状态文字显示（添加半透明背景，提高 z 值）

2. **client/src/presentation/renderers/VideoRenderer.cpp**（GPU 加速版）
   - `setFrame()`：qImageToYuv420Frame + 三缓冲 + deliverFrame
   - `paint()`：被 `updatePaintNode()`（QSG 渲染线程）替代
   - 构造函数：`setFlag(ItemHasContents, true)` 启用 QSG

## 验证步骤

1. **重新编译客户端**：
   ```bash
   cd client
   mkdir -p build && cd build
   cmake .. && make
   ```

2. **运行并观察日志**：
   ```bash
   ./RemoteDrivingClient 2>&1 | grep -E "\[VideoRenderer\]|\[QML\].*前方摄像头|hasVideoFrame"
   ```

3. **预期结果**：
   - 应该看到 `[VideoRenderer] paint #100` 日志（说明正在渲染）
   - 应该看到 `hasVideoFrame=true` 相关的日志
   - 前方摄像头应该显示视频画面

4. **如果仍然没有画面**：
   - 检查 VideoRenderer 的 `boundingRect()` 是否为空
   - 检查 `hasVideoFrame` 是否为 `true`
   - 检查 Canvas 和 Text 的 `visible` 是否为 `false`
   - 检查 VideoRenderer 的 `z` 值是否足够高

## 性能优化

1. **减少日志输出**：
   - 视频帧设置日志：每100帧记录一次
   - 渲染日志：每100帧记录一次

2. **优化渲染**：
   - 禁用抗锯齿（视频渲染不需要）
   - 添加边界检查，避免无效渲染

3. **条件渲染**：
   - 无视频时隐藏 VideoRenderer，减少无效渲染
   - 有视频时隐藏占位元素，减少 Canvas 绘制

## 后续优化建议

1. **添加视频帧率统计**：
   - 记录每秒接收的帧数
   - 记录渲染帧率
   - 在 UI 上显示 FPS

2. **优化视频渲染性能**：
   - 考虑使用硬件加速（如果可用）
   - 使用双缓冲减少闪烁
   - 优化图像拷贝（避免不必要的 copy）

3. **添加错误恢复机制**：
   - 检测长时间无帧的情况
   - 自动重连 WebRTC
   - 显示错误提示

4. **改进状态显示**：
   - 添加视频帧率显示
   - 添加视频分辨率显示
   - 添加连接质量指示

## 相关文档

- [前方摄像头调试文档](./FRONT_CAMERA_DEBUG.md)
- [视频流故障排查指南](./STREAM_TROUBLESHOOTING.md)
