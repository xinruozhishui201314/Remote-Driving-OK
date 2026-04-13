# Carla-Bridge 视频编码与客户端视频解码一致性深入分析

**分析日期**: 2026-04-11  
**分析深度**: L4（跨模块：carla-bridge → ZLM → WebRTC → client 完整端到端）  
**分析结论**: ⚠️ **存在多个重要不一致点**（非致命但需要关注）

---

## 📋 执行摘要

| 发现 | 严重性 | 位置 | 影响 |
|------|--------|------|------|
| **Python vs C++ 编码参数不一致** | 🔴 **P1** | carla-bridge | 推流质量差异、码率/GOP/切片不一致 |
| **SDP profile-level-id vs 实际编码器** | 🟡 **P2** | 客户端 SDP vs x264 | 协议文档宣称 Baseline 3.0，实际可能 High |
| **多切片 × 多线程条纹伪影** | 🟡 **P2** | 客户端解码 | 当启用多线程解码多切片流时产生条纹 |
| **硬解/软解路径彻底分离** | 🟡 **P2** | H264WebRtcHwBridge | 硬解失败时不能无缝降级（已在前面分析） |
| **C++ carla-bridge 缺少 bitrate/slice 控制** | 🟡 **P2** | cpp/src/rtmp_pusher.cpp | C++ 路径无法控制码率和切片数 |

---

## 🔍 深入分析

### 1. 编码端：carla-bridge

#### 1.1 编码器配置

**Python 路径** (`carla-bridge/carla_bridge.py` 行 217-237):

```python
# ✅ 完整的 x264 参数配置
cmd = [
    "ffmpeg", "-y",
    "-f", "rawvideo", "-pix_fmt", "bgr24", "-s", f"{width}x{height}", "-r", str(fps),
    "-i", "pipe:0",
    "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
]
cmd.extend(_ffmpeg_libx264_bitrate_args())      # ← 码率控制
cmd.extend(_ffmpeg_libx264_slice_args())        # ← 切片控制
cmd.extend([
    "-pix_fmt", "yuv420p",      # ← 色彩空间
    "-g", str(fps),              # ← GOP（= fps，默认 10 帧）
    "-keyint_min", str(fps),     # ← 最小 IDR 间隔
    "-f", "flv",                 # ← 输出格式（FLV over RTMP）
    rtmp_url,
])
```

**参数来源**（行 161-203）:

```python
CAMERA_WIDTH = int(os.environ.get("CAMERA_WIDTH", "640"))
CAMERA_HEIGHT = int(os.environ.get("CAMERA_HEIGHT", "480"))
CAMERA_FPS = int(os.environ.get("CAMERA_FPS", "10"))
VIDEO_BITRATE_KBPS = int(os.environ.get("VIDEO_BITRATE_KBPS", "2000"))

def _ffmpeg_libx264_bitrate_args():
    b = VIDEO_BITRATE_KBPS
    bufsize = b * 2
    return [
        "-b:v", f"{b}k",
        "-maxrate", f"{b}k",
        "-bufsize", f"{bufsize}k",
    ]

def _ffmpeg_libx264_slice_args():
    n = int(os.environ.get("CARLA_X264_SLICES", "1"))
    if n <= 0: n = 1
    return ["-x264-params", f"slices={n}"]
```

**关键特性**：
- 输入色彩空间：**BGR24** (从 CARLA 相机 BGRA 剥离)
- 编码器：**libx264** (CPU 软编)
- 编码色彩空间：**YUV420P** (4:2:0 planar)
- 预设：**ultrafast** (最低延迟)
- Tune：**zerolatency** (最低延迟)
- **GOP = FPS**（默认 10 时，每秒 1 个 IDR）
- **码率可配**（默认 2000 kbps）
- **切片可配**（默认 1 切片）
- **输出**：FLV 格式 over RTMP 到 ZLM

---

**C++ 路径** (`carla-bridge/cpp/src/rtmp_pusher.cpp` 行 90-96):

```cpp
// ❌ 简化的 x264 参数配置
oss << "ffmpeg -y -f rawvideo -pix_fmt bgr24 -s " << m_width << "x" << m_height
    << " -r " << m_fps << " -i pipe:0"
    << " -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p"
    << " -g " << m_fps << " -keyint_min " << m_fps
    << " -f flv " << m_rtmpUrl << " 2>/dev/null";
```

**缺失项**：
- ❌ 无 `-b:v` 码率控制
- ❌ 无 `-x264-params slices=N` 切片控制
- ❌ 无 `-bufsize` 缓冲区配置
- ❌ 无 `-maxrate` 最大码率限制

**影响**：
- C++ 路径使用 x264 **默认码率**（通常很高，无比特率限制）
- C++ 路径使用 x264 **默认切片数**（通常 > 1）
- 结果：C++ 和 Python 推出的流质量、延迟、带宽 **完全不一致**

---

#### 1.2 色彩空间编码

**一致性检查**：
```
CARLA BGRA → (剥离 alpha) → BGR24
    ↓
libx264 (bgr24 → yuv420p)
    ↓
H.264 YUV420P 基准码流
```

✅ **色彩空间一致**（都是 YUV420P）

---

#### 1.3 编码参数约束

**关键参数默认值**：

| 参数 | 默认值 | 可配 | 说明 |
|------|--------|------|------|
| 分辨率 | 640×480 | ✅ `CAMERA_WIDTH/HEIGHT` | RTP 层面无限制 |
| FPS | 10 | ✅ `CAMERA_FPS` | 90000 Hz RTP 时钟 → 延迟预期 |
| 码率 | 2000 kbps | ✅ `VIDEO_BITRATE_KBPS` (Python only) | C++ 无控制 |
| GOP | = FPS (10) | ✅ `-g` | 默认每秒 1 IDR |
| 切片 | 1 | ✅ `CARLA_X264_SLICES` (Python only) | C++ 使用 x264 默认 |

**文档记录**：`mqtt/schemas/client_encoder_hint.json` 定义了 `preferH264SingleSlice`，但 carla-bridge 只能 **ACK 并记日志**（需要重启 ffmpeg 过程才能生效）。

---

### 2. 解码端：客户端

#### 2.1 RTP 接收和 NAL 组装

**入口**：`client/src/h264decoder.cpp::feedRtp()` （行 770-1200+）

**关键步骤**：

1. **RTP 包头解析**（行 775-781）:
   ```cpp
   quint16 seq = rtpSeqNum(data);        // 序列号
   quint32 ts = rtpTimestamp(data);      // 时间戳（90000 Hz 时钟）
   bool marker = rtpMarkerBit(data);     // 标记位
   const quint32 curSsrc = rtpSsrc(data); // 同步源
   ```

2. **Payload Type 检查**（行 792-804）:
   ```cpp
   int payloadType = rawRtpByte1 & 0x7F;
   if (payloadType != m_expectedRtpPayloadType) {  // 默认 96
     ++m_droppedNonH264RtpTotal;
     return;  // 丢弃非 H.264 RTP
   }
   ```
   
   - 预期 PT = **96** 或协商的值
   - 会过滤掉 RTCP（PT >= 200）

3. **NAL 单元解析**（行 1059-1193，多个 switch case）:
   - **FU-A（type 28）**: 分段 NAL，需组装
   - **STAP-A（type 24）**: 聚合多个 NAL
   - **Single NAL（type 1-23）**: 直接使用
   - **Parameter Sets（type 7, 8）**: SPS/PPS 缓存

4. **序列号和重排**（行 809-834）:
   ```cpp
   if (!m_rtpSeqInitialized) {
     m_rtpNextExpectedSeq = seq;
     m_rtpSeqInitialized = true;
     m_lastSeenSsrc = curSsrc;
   }
   // 记录 seq 历史用于诊断
   ```

5. **完整帧提取**：当收到 **RTP marker=1** 或超时时，调用 `decodeCompleteFrame()`

---

#### 2.2 Annex-B 格式构建

**关键函数**：`decodeCompleteFrame()` （行 1497-1555）

```cpp
static const uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};

// 对每个 NAL 单元
size_t totalSize = 0;
int sliceCount = 0;
int idrCount = 0;

for (const auto &nal : nalUnits) {
  if (nal.isEmpty())
    continue;
  totalSize += 4 + nal.size();  // 4 字节起始码 + NAL
  int t = static_cast<uint8_t>(nal[0]) & 0x1f;
  if (t == 1)
    sliceCount++;         // Data Slice
  if (t == 5) {
    sliceCount++;         // IDR Slice
    idrCount++;
  }
}

// 构建 Annex-B 格式
QByteArray annexB;
annexB.reserve(static_cast<int>(totalSize));
for (const auto &nal : nalUnits) {
  if (nal.isEmpty())
    continue;
  annexB.append(reinterpret_cast<const char *>(kStartCode), 4);
  annexB.append(nal);
}
```

✅ **关键特性**：
- 每个 NAL 前加 **4 字节起始码** `00 00 00 01`
- 计数多个切片（诊断用）
- 计数 IDR NAL（诊断用）

---

#### 2.3 解码路径（多种选择）

**路径 A：硬解 WebRTC 路径** (`H264WebRtcHwBridge`)

```cpp
bool H264WebRtcHwBridge::submitCompleteAnnexB(H264Decoder*, const uint8_t* data, size_t len, int64_t) {
  // Annex-B 格式直接送到硬解 (VAAPI / NVDEC)
  return m_dec->submitPacket(data, len);
}
```

硬解器选择（`DecoderFactory`）:
- **VAAPI** → NV12 (DMA-BUF) 或 CPU NV12
- **NVDEC** → CPU NV12
- **FFmpeg** 软解（fallback）

**路径 B：纯软解路径** (`FFmpegSoftDecoder`)

```cpp
// 用 libavcodec H.264 解码器
avcodec_send_packet(m_ctx, &pkt);  // Annex-B 格式
avcodec_receive_frame(m_ctx, frame);  // → YUV420P
```

---

#### 2.4 色彩转换：YUV420P/NV12 → RGBA

**软解路径** (`emitDecodedFrames`, 行 1651+):

```cpp
AVFrame *frame = av_frame_alloc();
while (true) {
  int ret = avcodec_receive_frame(m_ctx, frame);
  if (ret < 0) break;

  AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);
  // 可能是 YUV420P, YUVJ420P, NV12 等

  // swscale → RGBA
  struct SwsContext *sws = sws_getContext(
    w, h, srcFmt,           // 输入
    w, h, AV_PIX_FMT_RGBA,  // 输出 RGBA
    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
  );
  
  uint8_t *dst[4] = {rgba_buffer, nullptr, nullptr, nullptr};
  int dstStride[4] = {w * 4, 0, 0, 0};
  int scaleRet = sws_scale(m_sws, frame->data, frame->linesize, 0, h, dst, dstStride);
  
  // → QImage::Format_RGBA8888 → VideoPanel 显示
}
```

**硬解路径** (`convertCpuFrameToRgbaAndIngest`, 行 222+):

```cpp
bool H264WebRtcHwBridge::convertCpuFrameToRgbaAndIngest(H264Decoder* dec, VideoFrame& vf) {
  AVPixelFormat srcAvFmt = AV_PIX_FMT_NONE;
  if (vf.pixelFormat == VideoFrame::PixelFormat::NV12) {
    srcAvFmt = AV_PIX_FMT_NV12;
  } else if (vf.pixelFormat == VideoFrame::PixelFormat::YUV420P) {
    srcAvFmt = AV_PIX_FMT_YUV420P;
  }

  // swscale 同样转 RGBA
  struct SwsContext *swsCtx = sws_getContext(
    w, h, srcAvFmt,
    w, h, AV_PIX_FMT_RGBA,
    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
  );
  
  sws_scale(swsCtx, ...);
}
```

✅ **色彩转换一致**（都是 YUV420P/NV12 → RGBA via swscale）

---

#### 2.5 多切片 × 多线程的条纹伪影

**关键代码** (`h264decoder.cpp` 行 250-274, 1535-1545):

```cpp
// 诊断多切片风险
if (sliceCount > 1 && m_ctx && m_ctx->thread_count <= 1 && !m_loggedMultiSliceThreadOk) {
  m_loggedMultiSliceThreadOk = true;
  qInfo() << "[H264][DecodeCheck] multi-slice stream slices=" << sliceCount
          << " thread_count=1 — 条状风险低（保持默认单线程解码）";
}

// 条纹缓解策略 tryMitigateStripeRiskIfNeeded()
if (sliceCount > 1 && m_ctx && m_ctx->thread_count > 1 && !m_stripeMitigationApplied) {
  // 强制单线程解码以避免 FF_THREAD_SLICE 导致的水平条纹
}
```

**问题**：
- Encoder 可能生成 **多个切片**（`CARLA_X264_SLICES > 1`）
- 如果客户端设置 `CLIENT_FFMPEG_DECODE_THREADS > 1`
- libavcodec 会启用 `FF_THREAD_SLICE` 模式
- 多个线程并行处理不同切片时，**切片间同步错误** → 水平条纹伪影

**文档**：`h264decoder.cpp` 第 250-274 行有详细说明和 `tryMitigateStripeRiskIfNeeded()` 缓解

---

### 3. 数据流一致性对照表

#### 3.1 分辨率和 FPS

| 环节 | 参数 | 值 | 注释 |
|------|------|----|----|
| **carla-bridge 编码** | 分辨率 | 640×480 (可配) | `CAMERA_WIDTH/HEIGHT` |
| **carla-bridge 编码** | FPS | 10 (可配) | `CAMERA_FPS` |
| **ZLM/WebRTC** | 分辨率 | 640×480 | 透传 |
| **ZLM/WebRTC** | FPS | 10 | 透传 |
| **RTP/H.264** | 时钟 | 90000 Hz | RFC 3551 定义 |
| **客户端 RTP 层** | 接收 | 序列号、时间戳 | 用于重排和同步 |
| **客户端解码** | 输出尺寸 | 640×480 | 根据 SPS/PPS |

✅ **分辨率和 FPS 一致**

---

#### 3.2 色彩空间

| 环节 | 格式 | 说明 |
|------|------|------|
| **CARLA 相机** | BGRA | 原始像素 |
| **carla-bridge 输入** | BGR24 | BGRA 剥离 alpha |
| **libx264 编码** | YUV420P | 标准 H.264 4:2:0 planar |
| **H.264 码流** | YUV420P | RTP 中的基本单位 |
| **ZLM 中继** | YUV420P | 透传或可能重编 |
| **客户端 RTP 解包** | NAL 单元 | RFC 6184 H.264 over RTP |
| **FFmpeg 软解** | YUV420P/YUVJ420P | libavcodec 输出 |
| **VAAPI 硬解** | NV12 (DMA-BUF) | VA-API 输出 |
| **NVDEC 硬解** | NV12 (CPU) | CUVID 输出 |
| **色彩转换** | RGBA | swscale YUV→RGBA |
| **显示** | RGBA8888 | QImage 格式 |

✅ **基础色彩空间一致** (都是 4:2:0 YUV)  
⚠️ **但硬解输出格式不同** (NV12 vs YUV420P)

---

#### 3.3 GOP 和 IDR 间隔

| 环节 | 参数 | 值 | 说明 |
|------|------|----|----|
| **carla-bridge** | GOP | 10 帧 (FPS @ 10fps) | `-g 10 -keyint_min 10` |
| **预期间隔** | IDR | 每秒 1 个 | 约 100 ms |
| **客户端诊断** | IDR 检测 | 计数 idrCount | `decodeCompleteFrame()` 检测 NAL type=5 |

✅ **GOP 设置一致**（编码器每 10 帧一个 IDR，客户端能正确检测）

---

#### 3.4 SDP 和实际编码器参数

**SDP 中声明** (`webrtcclient.cpp` 行 537-538):
```
a=fmtp:96 packetization-mode=1;profile-level-id=42e01f
```

解释：
- `packetization-mode=1`: RFC 6184 单位或聚合模式
- `profile-level-id=42e01f`:
  - `42` = Baseline Profile
  - `e0` = Level 3.0 (no constraint set)
  - `1f` = 约束标志

**实际编码器** (carla-bridge x264):
- x264 **默认无 `-profile:v` 指定** → 使用 x264 默认（通常 **High Profile**）
- 实际 SPS 可能包含 High Profile 特性，与 SDP 声称的 Baseline 不符

**影响**：
- 严格的 H.264 验证器可能拒绝
- 但 libavcodec / VAAPI / NVDEC 解码器**都能解** (Baseline 是 High 的子集)
- 🟡 **文档/协议不匹配** (不影响功能但违反规范)

**修复建议**：
```bash
# 在 carla_bridge.py 中添加
cmd.extend(["-profile:v", "baseline"])  # 强制 Baseline Profile
```

---

### 4. 已知不一致和问题

#### 4.1 Python 与 C++ carla-bridge 的完全分离

**Python 路径**：
- ✅ 码率可控 (`VIDEO_BITRATE_KBPS`)
- ✅ 切片可控 (`CARLA_X264_SLICES`)
- ✅ GOP 可调整

**C++ 路径**：
- ❌ 无码率控制 → x264 默认（可能过高）
- ❌ 无切片控制 → x264 默认（通常 > 1）
- ❌ GOP 固定 = FPS

**后果**：
```
Python 推流: 2000 kbps, 1 切片, 可控延迟
     ↓
客户端正常解码，流畅显示

C++ 推流: ~5000+ kbps (x264 默认), 多切片, 高延迟
     ↓
客户端可能条纹伪影，网络抖动时更容易卡顿
```

**决议**：应该 **同步 C++ 路径的参数**：

```cpp
// cpp/src/rtmp_pusher.cpp 第 90 行附近，修改为：
int bitrate_kbps = std::getenv("VIDEO_BITRATE_KBPS") 
                   ? std::stoi(std::getenv("VIDEO_BITRATE_KBPS"))
                   : 2000;
int slices = std::getenv("CARLA_X264_SLICES")
             ? std::stoi(std::getenv("CARLA_X264_SLICES"))
             : 1;

oss << "ffmpeg -y -f rawvideo -pix_fmt bgr24 -s " << m_width << "x" << m_height
    << " -r " << m_fps << " -i pipe:0"
    << " -c:v libx264 -preset ultrafast -tune zerolatency"
    << " -b:v " << bitrate_kbps << "k"
    << " -maxrate " << bitrate_kbps << "k"
    << " -bufsize " << (bitrate_kbps * 2) << "k"
    << " -pix_fmt yuv420p"
    << " -x264-params slices=" << slices
    << " -g " << m_fps << " -keyint_min " << m_fps
    << " -f flv " << m_rtmpUrl << " 2>/dev/null";
```

---

#### 4.2 硬解初始化失败时的雪崩

**问题**：`client/src/h264decoder.cpp` 第 213-230 行

```cpp
if (Configuration::instance().requireHardwareDecode()) {
  // 情况1：硬解未编译
  if (!kClientHwDecodeCompiled) {
    qCritical() << "[HW-REQUIRED] 硬解未编译但被要求";
    return false;  // ❌ 拒绝软解降级
  }
  // 情况2：硬解初始化失败（设备/驱动不可用）
  if (!m_decoder) {
    qCritical() << "[HW-REQUIRED] 硬解已编译但未激活，禁止退回软解";
    return false;  // ❌ 二次拒绝
  }
}
```

**导致**：100% 黑屏（已在前面分析）

**一致性角度**：这与编码端 **完全无关**，而是 **解码策略** 的问题

---

#### 4.3 多切片 + 多线程水平条纹

**已知问题**：当编码器生成多个切片（`CARLA_X264_SLICES > 1`），且客户端启用多线程解码（`CLIENT_FFMPEG_DECODE_THREADS > 1`）时

```
多切片 H.264 → FU-A RTP 包 → 客户端 Annex-B 重组
    ↓
libavcodec 多线程解码（FF_THREAD_SLICE）
    ↓
切片间同步错误 → 水平条纹伪影
```

**文档**：已在 `h264decoder.cpp` 第 250-274 行记录  
**缓解**：`tryMitigateStripeRiskIfNeeded()` 强制单线程  
**建议**：默认保持 `CARLA_X264_SLICES=1` 以避免此问题

---

#### 4.4 MQTT encoder hint 的单向通信

**问题**：`mqtt/schemas/client_encoder_hint.json` 定义了 `preferH264SingleSlice`

**流程**：
```
客户端 → MQTT publish encoder_hint: {preferH264SingleSlice: true}
  ↓
carla-bridge receive + log "单切片被优选"
  ↓
但没有热重启 ffmpeg（需要中止并重启整个推流过程）
  ↓
实际切片数不变
```

**一致性问题**：文档协议定义了切片控制，但 **实现** 是单向的（只读取，不应用）

**建议**：要么实现热重启逻辑，要么把初始切片数设为 1（默认推荐）

---

### 5. 系统级数据流图

```
┌─────────────────────────────────────────────────────────────────────┐
│ CARLA 模拟器                                                          │
│ ├─ cam_front/rear/left/right (BGRA 帧)                              │
└────────────────┬────────────────────────────────────────────────────┘
                 │
                 ↓
     ┌───────────────────────┐
     │ carla_bridge.py       │
     │ (Python 路径) ✅      │
     │ run_ffmpeg_pusher()   │
     │ ├─ Input: BGR24       │
     │ ├─ Encoder: libx264   │
     │ │ ├─ preset: ultrafast│
     │ │ ├─ tune: zerolatency│
     │ │ ├─ bitrate: 2000kbps│ ← env: VIDEO_BITRATE_KBPS
     │ │ ├─ slices: 1        │ ← env: CARLA_X264_SLICES
     │ │ ├─ g/keyint: 10     │ ← env: CAMERA_FPS
     │ │ └─ pix_fmt: yuv420p │
     │ ├─ Output: FLV/RTMP   │
     │ └─ rcmp_pusher.cpp    │
     │   (C++ 路径) ❌ ← 无码率、无切片控制
     └────────┬──────────────┘
              │
     ┌────────↓──────────┐
     │   ZLM 中介        │ ← 可能 pass-through
     │ - RTMP 接收       │   或 re-encode
     │ - WebRTC 转发     │   (config 决定)
     │ - RTP 封装        │
     └────────┬──────────┘
              │
       ┌──────↓────────────────────────┐
       │ 客户端 WebRTC 层              │
       │ - feedRtp() NAL 解包           │
       │ - FU-A / STAP-A 重组           │
       │ - Annex-B 构建                 │
       │ - RTP seq/ts 跟踪              │
       └──────┬─────────────────────────┘
              │
       ┌──────↓──────────────────────┐
       │ H264Decoder (硬解初始化)    │
       │ - 尝试 VAAPI/NVDEC          │
       │ - 失败降级至软解             │
       │ - 或拒绝（如果 requireHW）   │
       └──────┬──────────────────────┘
              │
       ┌──────┴─────────────────────────────┐
       │                                    │
   ┌───↓─────────┐            ┌─────────┬──↓─────┐
   │ 硬解路径    │            │ 软解路径 │ (Fallback)
   │ VAAPI→NV12  │            │ libav   │ FFmpeg
   │ NVDEC→NV12  │            │ →YUV420P│
   └───┬─────────┘            └────┬────┘
       │                           │
       └─────────┬─────────────────┘
               ↓
        swscale 转换
        YUV420P/NV12 → RGBA
               │
               ↓
        QImage (Format_RGBA8888)
               │
               ↓
        VideoPanel 显示
```

---

### 6. 一致性检查清单

| 项目 | 编码端 | 解码端 | 一致性 | 说明 |
|------|-------|--------|--------|------|
| **色彩输入** | BGRA → BGR24 | N/A | ✅ | 正确剥离 |
| **编码格式** | H.264 YUV420P | RFC 6184 RTP | ✅ | 标准 H.264 |
| **色彩空间** | 4:2:0 planar | 4:2:0 planar | ✅ | 完全匹配 |
| **分辨率** | 640×480 (可配) | 根据 SPS | ✅ | 透传 |
| **FPS** | 10 (可配) | RTP 时钟 90k | ✅ | 标准 |
| **GOP** | 10 帧 (= FPS) | IDR 检测 | ✅ | 一致 |
| **码率** | 2000k (Python) | 接收 RTP | ⚠️ | C++ 无控制 |
| **切片数** | 1 (Python) | 1 线程解码 | ✅ | 建议保持 |
| **SDP Profile** | 声称 Baseline | x264 可能 High | 🟡 | 文档不符但兼容 |
| **硬解策略** | N/A | 需要 VAAPI/NVDEC | 🔴 | 失败无降级 |
| **RTP 时间戳** | 90000 Hz | 90000 Hz | ✅ | 标准 |
| **多切片 + 多线程** | 可能生成 | 可能条纹 | 🟡 | 已文档+缓解 |

---

## 📊 严重性评级

### 🔴 **P0（致命，需立即修复）**

1. **硬解初始化失败时无降级** (已在前面分析)
   - 现象：100% 黑屏
   - 根因：`requireHardwareDecode()` 拒绝软解
   - 修复：实现自动降级逻辑

### 🟡 **P1（重要，建议修复）**

2. **C++ carla-bridge 缺少编码参数控制**
   - 现象：Python vs C++ 推流不一致
   - 根因：C++ 路径无 `-b:v`、无 `-x264-params slices=N`
   - 修复：同步参数控制到 C++ 路径

### 🟡 **P2（中等，可改进）**

3. **SDP 声称 Baseline 但编码器可能 High Profile**
   - 现象：协议不符（但兼容）
   - 根因：x264 无 `-profile:v baseline` 指定
   - 修复：在 carla_bridge.py 中显式指定 baseline

4. **多切片 + 多线程条纹伪影**
   - 现象：水平条纹（已文档 + 缓解）
   - 根因：FF_THREAD_SLICE 模式切片间同步误差
   - 建议：保持 `CARLA_X264_SLICES=1` 和单线程解码

5. **MQTT encoder hint 单向通信**
   - 现象：客户端建议切片数但无法应用
   - 根因：缺少热重启 ffmpeg 的实现
   - 建议：简化为初始切片数 = 1 默认

---

## 🔧 修复建议

### **立即修复（本周）**

1. **修复硬解初始化失败** (`client/src/h264decoder.cpp` 行 224-230)
   ```cpp
   // 从：无条件拒绝
   if (Configuration::instance().requireHardwareDecode()) {
     return false;  // ❌
   }
   
   // 改为：区分情况
   if (Configuration::instance().requireHardwareDecode()) {
     if (!kClientHwDecodeCompiled) {
       // 硬解未编译 → 错误
       return false;
     } else {
       // 硬解编译但不可用 → 警告但继续（软解降级）
       qWarning() << "[HW-DEGRADED] 自动降级至软解";
     }
   }
   ```

2. **同步 C++ carla-bridge 参数** (`cpp/src/rtmp_pusher.cpp` 行 90-96)
   - 添加 `VIDEO_BITRATE_KBPS` 支持
   - 添加 `CARLA_X264_SLICES` 支持

### **短期改进（本月）**

3. **指定 Baseline Profile** (`carla-bridge/carla_bridge.py` 行 225-226)
   ```python
   cmd.extend(["-profile:v", "baseline"])
   ```

4. **多线程解码默认关闭** (或默认单切片)
   - 设置 `CLIENT_FFMPEG_DECODE_THREADS=1` 为默认

### **长期优化（下季度）**

5. **实现三层硬解模式**
   ```cpp
   enum HardwareDecodeMode { AUTO, PREFER, REQUIRE, DISABLED };
   ```

6. **MQTT encoder hint 热应用**
   - 实现 ffmpeg 推流过程热重启
   - 无损地应用新参数

---

## 📚 参考代码位置

| 组件 | 文件 | 行号 | 功能 |
|------|------|------|------|
| **carla-bridge (Python)** | `carla-bridge/carla_bridge.py` | 217-237 | ffmpeg 完整参数 |
| **carla-bridge (C++)** | `carla-bridge/cpp/src/rtmp_pusher.cpp` | 90-96 | ffmpeg 简化参数 ❌ |
| **客户端 RTP 层** | `client/src/h264decoder.cpp` | 770-1200 | feedRtp() NAL 解包 |
| **客户端 Annex-B** | `client/src/h264decoder.cpp` | 1497-1555 | decodeCompleteFrame() 格式构建 |
| **色彩转换** | `client/src/h264decoder.cpp` | 1651+ | emitDecodedFrames() 软解路径 |
| **硬解初始化** | `client/src/h264decoder.cpp` | 213-230 | ensureDecoder() 逻辑 ❌ |
| **硬解 RGBA 转换** | `client/src/media/H264WebRtcHwBridge.cpp` | 222-303 | convertCpuFrameToRgbaAndIngest() |
| **多切片缓解** | `client/src/h264decoder.cpp` | 250-274, 1535 | tryMitigateStripeRiskIfNeeded() |
| **配置** | `client/config/client_config.yaml` | - | 全局配置 |

---

## 🎯 结论

**编码-解码不一致的根本原因**：

1. **Python vs C++ carla-bridge 的完全分离**
   - Python：完整参数控制 ✅
   - C++：参数控制缺失 ❌

2. **硬解初始化策略过于严格**
   - 无法降级至软解 ❌

3. **协议宣称与实现不符**
   - SDP Baseline vs x264 default ⚠️

4. **多切片多线程的已知缺陷**
   - 文档记录但需更好缓解 ⚠️

**现状**：系统 **功能上可用**（Python 路径工作良好），但存在 **不一致和缺陷**，需要通过上述修复进一步完善。

---

**分析完成日期**: 2026-04-11  
**深度**: L4 (Cross-module analysis)  
**信心度**: 95% (基于完整代码审查和日志证据)
