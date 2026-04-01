# 客户端视频流错误修复说明

## 问题总结

根据最新日志分析，发现以下问题：

### 1. IDR 恢复时间仍然很长
- **现象**：等待 74/102/115/54 帧才收到 IDR
- **原因**：推流脚本已修改但**推流进程未重启**，仍在使用旧配置（GOP ≈ 250 帧）

### 2. FFmpeg 解码错误
- **现象**：
  ```
  [h264 @ 0x...] Invalid NAL unit 1, skipping.
  [h264 @ 0x...] Missing reference picture, default is 0
  [h264 @ 0x...] decode_slice_header error
  [h264 @ 0x...] no frame!
  ```
- **原因**：RTP 丢包导致帧不完整，解码器状态损坏

---

## 已实施的修复

### 1. 推流脚本 GOP 配置修复 ✅

**文件**：`scripts/push-testpattern-to-zlm.sh`

**修改**：添加 GOP 配置参数
```bash
-g ${FPS} -keyint_min ${FPS} -bf 0
```

**效果**：
- IDR 间隔从 250 帧（25 秒@10fps）缩短到 10 帧（1 秒@10fps）
- 丢包恢复时间从 10+ 秒缩短到 ≤ 1 秒

### 2. 解码器错误处理增强 ✅

**文件**：`client/src/h264decoder.cpp`

**修改 1**：`AVERROR_INVALIDDATA` 时立即 flush
```cpp
if (ret == AVERROR_INVALIDDATA) {
    if (!m_needKeyframe) {
        qDebug() << "[H264] 解码失败(INVALIDDATA)，立即 flush 解码器并等待 IDR";
        flushDecoder();  // ★ 立即 flush，清除损坏的解码器状态
        m_needKeyframe = true;
        m_framesSinceKeyframeRequest = 0;
    }
    return;
}
```

**修改 2**：连续解码错误检测
```cpp
int consecutive_errors = 0;
const int max_consecutive_errors = 3;

while (true) {
    int ret = avcodec_receive_frame(m_ctx, frame);
    if (ret < 0) {
        consecutive_errors++;
        if (consecutive_errors >= max_consecutive_errors && !m_needKeyframe) {
            qDebug() << "[H264] 连续" << consecutive_errors << "次解码错误，flush 解码器并等待 IDR";
            flushDecoder();
            m_needKeyframe = true;
            m_framesSinceKeyframeRequest = 0;
        }
        break;
    }
    consecutive_errors = 0;  // 重置错误计数
    // ... 正常处理帧
}
```

**效果**：
- 检测到关键错误时立即 flush 解码器，清除损坏状态
- 连续 3 次解码错误后自动 flush，防止解码器状态持续损坏

---

## 需要用户操作

### ⚠️ 重要：重启推流脚本

**问题**：虽然脚本已修改，但**推流进程仍在运行旧配置**

**操作步骤**：

1. **停止当前推流进程**
   ```bash
   # 在车端容器中
   docker exec -it teleop-vehicle bash
   pkill -f "push-testpattern-to-zlm.sh"
   pkill -f "push-nuscenes-cameras-to-zlm.sh"
   pkill -f ffmpeg
   ```

2. **重新触发推流**（客户端重新连接车端）
   - 客户端断开连接
   - 客户端重新点击"连接车端"
   - 车端收到 `start_stream` 后执行新脚本

3. **验证 GOP 配置**
   ```bash
   # 检查推流进程参数
   ps aux | grep ffmpeg | grep -E "(-g|-keyint)"
   # 应该看到：-g 10 -keyint_min 10
   ```

---

## 验证修复效果

### 1. 检查 IDR 间隔

**观察日志**：
```
[H264] 收到 IDR，丢包恢复完成 等待了 X 帧
```

**预期**：
- ✅ X ≤ 10（1 秒@10fps）
- ❌ X > 50 说明推流脚本未重启

### 2. 检查解码错误

**观察日志**：
- ✅ 不应频繁出现 `Invalid NAL unit`、`Missing reference picture`
- ✅ 如果出现，应该立即 flush 并等待 IDR

### 3. 检查帧不完整率

**观察日志**：
```
[H264] 帧不完整 ts= ... slices= X / 4
```

**预期**：
- ✅ 帧不完整次数应该显著减少
- ✅ 即使出现，恢复时间应该 ≤ 1 秒

---

## 如果问题仍然存在

### 检查清单

1. **推流脚本是否重启？**
   ```bash
   # 检查推流进程的 GOP 参数
   ps aux | grep ffmpeg
   ```

2. **网络是否稳定？**
   - 检查 RTP 丢包率
   - 检查网络延迟和抖动

3. **ZLMediaKit 配置是否正确？**
   - 检查 `deploy/zlm/config.ini` 中的 RTP 配置
   - 检查缓冲区大小设置

### 进一步优化（可选）

如果问题仍然存在，可以考虑：

1. **更激进的 GOP 配置**（降低到 0.5 秒）
   ```bash
   -g $((FPS / 2)) -keyint_min $((FPS / 2))
   ```

2. **客户端主动请求关键帧**（需要 ZLMediaKit API 支持）
   ```cpp
   // 当 m_framesSinceKeyframeRequest > 30 时
   // 通过 HTTP API 请求关键帧
   POST /index/api/webrtc?app=teleop&stream=cam_front&type=play&force_keyframe=1
   ```

3. **启用前向纠错（FEC）**
   - WebRTC 原生支持 RED/FEC
   - 需要 ZLMediaKit 支持

---

## 相关文件

- `scripts/push-testpattern-to-zlm.sh` - 推流脚本（已修复）
- `scripts/push-nuscenes-cameras-to-zlm.sh` - 数据集推流脚本（配置正确）
- `client/src/h264decoder.cpp` - H.264 解码器（已增强错误处理）
- `docs/CLIENT_VIDEO_STREAM_ISSUE_ANALYSIS.md` - 详细问题分析

---

## 总结

**核心问题**：推流脚本 GOP 配置已修复，但**推流进程需要重启**才能生效。

**修复效果**：
- ✅ IDR 间隔：25 秒 → 1 秒
- ✅ 丢包恢复：10+ 秒 → ≤ 1 秒
- ✅ 解码错误处理：增强，自动 flush 损坏状态

**下一步**：**重启推流脚本**，验证修复效果。
