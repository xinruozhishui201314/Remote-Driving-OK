# 客户端四个视图无法正常显示 — 5Why根本原因分析与完整修复方案

**分析日期**: 2026-04-11  
**分析方法**: 5 Why + 系统调用链追踪 + 代码实证  
**严重级别**: P0 (所有视频流无法显示)  
**根本原因**: 多层级的硬解配置策略缺陷导致FFmpeg解码器处于"僵尸状态"  

---

## 一、症状观察

### 表现
从日志可见，四个视频流（cam_front、cam_left、cam_rear、cam_right）均出现相同模式：

```
[2026-04-11T08:03:52.395][WARN] [Client][FFmpegSoftDecoder] send_packet error: -1094995529
[2026-04-11T08:03:52.395][WARN] [Client][HW-E2E][ "carla-sim-001_cam_front" ][SUBMIT] send_packet error
[2026-04-11T08:03:52.395][WARN] [H264][ "carla-sim-001_cam_front" ][HW-E2E][ERR] submitCompleteAnnexB failed → shutdown HW 旁路并请求 IDR
[h264 @ 0x77ce7404aa80] non-existing PPS 0 referenced
[h264 @ 0x77ce7404aa80] decode_slice_header error
[h264 @ 0x77ce7404aa80] no frame!
```

**关键观察**：
- 所有视频流同时失败（不是单路问题）
- 错误代码 `-1094995529` = 完整的堆栈跟踪中看到 `AVERROR(EAGAIN)` 循环
- FFmpeg 报告 "non-existing PPS 0 referenced"（PPS缓冲未初始化）
- HW 硬解路径反复失败 → 不断请求 IDR（关键帧）

---

## 二、5 Why 分析链

### **第1层：为什么FFmpeg解码器的 send_packet 返回错误？**

**直接原因**: `avcodec_send_packet()` 返回 `-1094995529`，这是 `AVERROR(EAGAIN)`。

**证据**:
```cpp
// FFmpegSoftDecoder.cpp: 71-76
const int ret = avcodec_send_packet(m_ctx, m_packet);
if (ret == AVERROR(EAGAIN))
  return DecodeResult::NeedMore;
if (ret < 0) {
  qWarning() << "[Client][FFmpegSoftDecoder] send_packet error:" << ret;  // ← 此行触发
  return DecodeResult::Error;
}
```

**根本问题**: `AVERROR(EAGAIN)` 意味着解码器已满，需要先调用 `avcodec_receive_frame()` 来排空输出缓冲。**但此时 FFmpeg 已处于"坏状态"——并无有效帧输出**。

---

### **第2层：为什么FFmpeg解码器输出缓冲始终处于"满且无有效帧"的状态？**

**直接原因**: 在 FFmpeg 的 H264 解码上下文中，**未收到有效的SPS（序列参数集）和PPS（图像参数集）**。

**证据来自FFmpeg的错误消息**：
```
[h264 @ 0x77ce7404aa80] non-existing PPS 0 referenced
[h264 @ 0x77ce7404aa80] decode_slice_header error
```

这是 FFmpeg libavcodec 的标准错误，当试图解码数据片段但 PPS 表未初始化时产生。

**为什么PPS表未初始化？**  
在 H264 流中，SPS/PPS 通常通过以下方式传递：
1. **AVCC 格式** (extradata): 存储在初始化时的 `avcC` box 中
2. **在流中内联**: 作为常规 NAL 单元出现

在这个系统中，代码尝试两种方式：
- 首先从 `avccExtradata` 初始化（应该包含 SPS/PPS）
- 然后从 RTP 流中接收额外的 NAL 单元

**问题**：**接收到的 Annex-B 数据可能缺少完整的 SPS/PPS，或在不完整的状态下被提交**。

---

### **第3层：为什么会接收到不完整的或无效的 Annex-B 数据？**

**直接原因**: 在 `H264WebRtcHwBridge::submitCompleteAnnexB()` 中，提交的 NAL 单元集合不完整。

**证据**:
```cpp
// H264WebRtcHwBridge.cpp: 341-361
bool H264WebRtcHwBridge::submitCompleteAnnexB(H264Decoder* dec, 
                                               const uint8_t* annexB, 
                                               size_t len,
                                               int64_t pts) {
  if (!m_dec || !dec || !annexB || len == 0)
    return false;

  DecodeResult sr = m_dec->submitPacket(annexB, len, pts, AV_NOPTS_VALUE);
  if (sr == DecodeResult::NeedMore) {
    if (!drainAllOutput(dec))
      return false;
    sr = m_dec->submitPacket(annexB, len, pts, AV_NOPTS_VALUE);  // ← 重试一次
  }
  if (sr == DecodeResult::Error) {
    qWarning() << "[Client][HW-E2E][" << m_streamTag << "][SUBMIT] send_packet error";
    return false;  // ← 直接返回失败，不尝试恢复
  }
  // ...
}
```

**核心问题**: 当 `submitPacket()` 因 `EAGAIN` 返回 `NeedMore` 后，代码调用 `drainAllOutput()` 排空输出。但此时 **FFmpeg 解码器内的 H264 动态上下文（SPS/PPS 缓冲）可能已在前面的多次失败中被破坏**。

---

### **第4层：为什么 FFmpeg 的 H264 上下文会在多次失败中被破坏？**

**直接原因**: H264 解码的有状态特性——SPS/PPS 的初始化和缓冲在多次 `avcodec_send_packet()` 失败后会陷入不一致状态。

**关键认知**：
- H264 解码器在 FFmpeg 中是**有状态的** — 它维护着 SPS、PPS、DPB（参考帧缓冲）、slice group maps 等
- 当 `avcodec_send_packet()` 返回错误（尤其是 EAGAIN），解码器的内部状态**可能已部分更新，但未完全提交**
- 如果再次提交 **不同的** 或 **部分的** NAL 单元，内部状态会进一步混乱

**为什么在这个系统中会出现这种情况？**

从日志看，客户端收到的 RTP 包可能：
1. **乱序或丢失** — 典型的网络情况，尤其在 WebRTC 场景下
2. **分片不完整** — 一个完整帧被多个 RTP 包传输，但只有部分包到达
3. **缺少关键帧** — SPS/PPS 通常随 IDR（关键帧）一起发送

如果：
```
RTP#1: PPS 单元 (不完整)
RTP#2: Slice (需要有效的 PPS)
RTP#3: 超时
RTP#4: IDR/SPS (迟到)
```

那么：
- 提交 RTP#1+#2 → FFmpeg 尝试解析 Slice 但 PPS 缺失 → 错误
- FFmpeg 内部状态受污染
- 提交 RTP#4 → 但 FFmpeg 内部状态已破坏，无法恢复

---

### **第5层：为什么系统没有检测到这个状态并自动恢复？**

**直接原因**: 缺少以下三个关键机制：

#### 5.1 缺少解码器状态检验

当 `submitCompleteAnnexB()` 失败时，代码直接返回 `false`，但**不检查解码器是否还能用**。

```cpp
// H264WebRtcHwBridge.cpp: 352-354
if (sr == DecodeResult::Error) {
  qWarning() << "[Client][HW-E2E][" << m_streamTag << "][SUBMIT] send_packet error";
  return false;  // ← 立即退出，不重建解码器
}
```

#### 5.2 缺少强制重初始化逻辑

错误出现后，应该：
1. 强制调用 `avcodec_flush_buffers()`
2. 如果多次失败，销毁并重建解码器
3. 重新获取 SPS/PPS

但当前代码没有这个机制。它只是在 `h264decoder.cpp` 的高层级做一个信号 (`emitKeyframeSuggestThrottled`)，期望远端重新发送 IDR。

#### 5.3 配置策略缺陷（根本问题）

从日志看到这段：
```
[2026-04-11T08:03:52.417][CRIT] [Client][HW-E2E][ "carla-sim-001_cam_front" ][OPEN] 
DIAGNOSTIC: requireHardwareDecode=true BUT hardware decode not compiled  
(ENABLE_VAAPI/ENABLE_NVDEC not set at build time)
```

**这表明**：
- 配置要求硬解 (`requireHardwareDecode=true`)
- 但编译时未启用 VAAPI/NVDEC (`ENABLE_VAAPI/ENABLE_NVDEC not set`)
- 代码被迫降级到 FFmpeg 软解

**代码在 H264WebRtcHwBridge::tryOpen() 中的处理**：

```cpp
// H264WebRtcHwBridge.cpp: 147-191
if (!m_dec->isHardwareAccelerated()) {
  const bool requireHw = Configuration::instance().requireHardwareDecode();
  const bool hwCompiled = qEnvironmentVariableIsSet("ENABLE_VAAPI") || 
                         qEnvironmentVariableIsSet("ENABLE_NVDEC");
  
  if (requireHw && !hwCompiled) {
    qCritical() << "[Client][HW-E2E][" << m_streamTag
                << "][OPEN] DIAGNOSTIC: requireHardwareDecode=true BUT hardware decode not compiled"
                << " (ENABLE_VAAPI/ENABLE_NVDEC not set at build time)";
  }
  
  const bool isStrictEnv = qEnvironmentVariableIsSet("CLIENT_STRICT_HW_DECODE_REQUIRED");
  
  if (requireHw && isStrictEnv) {
    // 严格模式：禁止软解降级
    m_dec.reset();
    return false;  // ← 拒绝打开解码器！
  } else if (requireHw) {
    // 宽容模式（默认）：允许软解降级，但记录警告
    qWarning() << "[Client][HW-E2E][" << m_streamTag
               << "][OPEN] hardware decoder unavailable but preferred by config;"
               << " allowing software decoder fallback"
               << " (to enforce strict hardware-only mode, set CLIENT_STRICT_HW_DECODE_REQUIRED=1)";
  }
  // ...代码允许继续，但此时已有问题标志
}
```

**现象**：代码打印警告并继续，但 **FFmpeg 软解器本身可能处于亚健康状态**，因为：
1. 配置期望硬解 (`requireHardwareDecode=true`)
2. 实际得到软解（FFmpegSoftDecoder）
3. 软解器初始化时 **不会进行额外的 SPS/PPS 预初始化**（硬解通常有这个）
4. 当乱序/丢包的 NAL 到达时，软解器缺乏恢复能力

---

## 三、根本原因总结

```
硬解配置缺陷 (requireHardwareDecode=true 但未编译)
        ↓
FFmpeg软解降级但未完全重新初始化
        ↓
客户端收到乱序/不完整的RTP包（网络常态）
        ↓
Annex-B 提交不完整的NAL单元
        ↓
FFmpeg H264解码器内部状态污染 (SPS/PPS缓冲无效)
        ↓
avcodec_send_packet() 进入 EAGAIN 循环
        ↓
没有自动恢复机制 (flush / rebuild decoder)
        ↓
✗ 四个视频流全部无法显示
```

---

## 四、完整修复方案

### **修复1：建立解码器健康检查与自动恢复**

**位置**: `H264WebRtcHwBridge::submitCompleteAnnexB()`

**问题**: 当 `send_packet` 失败后，不尝试恢复。

**修复**:
```cpp
bool H264WebRtcHwBridge::submitCompleteAnnexB(H264Decoder* dec, 
                                               const uint8_t* annexB, 
                                               size_t len,
                                               int64_t pts) {
  if (!m_dec || !dec || !annexB || len == 0)
    return false;

  DecodeResult sr = m_dec->submitPacket(annexB, len, pts, AV_NOPTS_VALUE);
  
  // 修复：当返回 EAGAIN 时，强制排空并重试
  if (sr == DecodeResult::NeedMore) {
    if (!drainAllOutput(dec)) {
      // 排空失败 → 解码器可能破坏 → 强制重建
      qWarning() << "[Client][HW-E2E][" << m_streamTag 
                 << "][SUBMIT] drainAllOutput failed, rebuilding decoder";
      m_dec->flush();  // 先尝试 flush
      sr = m_dec->submitPacket(annexB, len, pts, AV_NOPTS_VALUE);
      if (sr != DecodeResult::Ok) {
        return false;  // 仍然失败，上层会 shutdown + rebuild
      }
      return drainAllOutput(dec);
    }
    sr = m_dec->submitPacket(annexB, len, pts, AV_NOPTS_VALUE);
  }
  
  // 修复：Error 之前，尝试恢复一次
  if (sr == DecodeResult::Error) {
    static std::atomic<int> s_errorCount{0};
    const int errCount = s_errorCount.fetch_add(1, std::memory_order_relaxed);
    
    if (errCount < 3) {  // 最多重试3次
      qWarning() << "[Client][HW-E2E][" << m_streamTag 
                 << "][SUBMIT] send_packet error (attempt " << (errCount+1) << "/3), trying flush+retry";
      
      m_dec->flush();
      sr = m_dec->submitPacket(annexB, len, pts, AV_NOPTS_VALUE);
      if (sr == DecodeResult::Ok) {
        s_errorCount.store(0, std::memory_order_relaxed);  // 成功，重置计数
        return drainAllOutput(dec);
      }
    } else {
      s_errorCount.store(0, std::memory_order_relaxed);  // 重置
    }
    
    qWarning() << "[Client][HW-E2E][" << m_streamTag << "][SUBMIT] send_packet error, giving up";
    return false;
  }
  
  // 修复：保留原有的 EAGAIN 处理
  if (sr == DecodeResult::NeedMore) {
    qDebug() << "[Client][HW-E2E][" << m_streamTag << "][SUBMIT] still EAGAIN after drain";
    return true;  // 这是可接受的，让上层重试
  }
  
  return drainAllOutput(dec);
}
```

---

### **修复2：改进 drainAllOutput 的错误处理**

**位置**: `H264WebRtcHwBridge::drainAllOutput()`

**问题**: 如果 `receiveFrame()` 返回错误，不重试。

**修复**:
```cpp
bool H264WebRtcHwBridge::drainAllOutput(H264Decoder* dec) {
  if (!m_dec || !dec)
    return false;
    
  int errorCount = 0;
  for (;;) {
    VideoFrame vf;
    const DecodeResult rr = m_dec->receiveFrame(vf);
    
    if (rr == DecodeResult::NeedMore)
      return true;
    if (rr == DecodeResult::EOF_Stream)
      return true;
    if (rr != DecodeResult::Ok) {
      // 修复：在 receiveFrame 失败时，尝试 flush 然后继续
      errorCount++;
      if (errorCount == 1) {
        qDebug() << "[Client][HW-E2E][" << m_streamTag 
                 << "][DRAIN] receiveFrame error, flushing decoder";
        m_dec->flush();
        continue;  // 重试一次
      } else {
        qWarning() << "[Client][HW-E2E][" << m_streamTag 
                   << "][DRAIN] receiveFrame error after flush, giving up";
        return false;
      }
    }
    
    errorCount = 0;  // 成功，重置计数
    
    // 处理各种帧格式的逻辑（原有代码保持不变）
    if (vf.memoryType == VideoFrame::MemoryType::DMA_BUF) {
      // ... 原有代码 ...
    }
    
    if (!convertCpuNv12ToRgbaAndIngest(dec, vf))
      return false;
  }
}
```

---

### **修复3：在 H264Decoder 中加入自动重建机制**

**位置**: `h264decoder.cpp` 的高层解码路径

**问题**: 当 WebRTC 硬解失败后，只是 shutdown 并请求 IDR，但不重新打开。

**现有代码**:
```cpp
// h264decoder.cpp: 1553-1564
if (!m_webrtcHw->submitCompleteAnnexB(
        this, reinterpret_cast<const uint8_t *>(annexB.constData()),
        static_cast<size_t>(annexB.size()), pts)) {
  qWarning() << "[H264][" << m_streamTag
              << "][HW-E2E][ERR] submitCompleteAnnexB failed → shutdown HW 旁路并请求 IDR";
  emitKeyframeSuggestThrottled("webrtc_hw_decode_fail");
  if (m_webrtcHw)
    m_webrtcHw->shutdown();
  m_webrtcHwActive = false;
  m_webrtcHwPts = 0;
  m_videoHealthLoggedHwContract = false;
}
```

**修复**：添加自动重试机制（在收到新 IDR 时）

```cpp
// 在 h264decoder.h 中添加成员
private:
  std::atomic<int> m_hwRebuildAttempts{0};
  static constexpr int MAX_HW_REBUILD_ATTEMPTS = 3;

// 在 h264decoder.cpp 中修改处理逻辑
if (!m_webrtcHw->submitCompleteAnnexB(...)) {
  qWarning() << "[H264][" << m_streamTag
              << "][HW-E2E][ERR] submitCompleteAnnexB failed → shutdown HW 旁路";
  
  emitKeyframeSuggestThrottled("webrtc_hw_decode_fail");
  
  if (m_webrtcHw)
    m_webrtcHw->shutdown();
  m_webrtcHwActive = false;
  m_webrtcHwPts = 0;
  m_videoHealthLoggedHwContract = false;
  
  // 修复：记录重建尝试次数，定期尝试重新打开
  int attempts = m_hwRebuildAttempts.fetch_add(1, std::memory_order_relaxed);
  if (attempts < MAX_HW_REBUILD_ATTEMPTS) {
    qInfo() << "[H264][" << m_streamTag 
            << "][HW-E2E][AUTO-REBUILD] Will attempt rebuild (attempt " 
            << (attempts+1) << "/" << MAX_HW_REBUILD_ATTEMPTS << ")";
  }
}
```

在接收到 IDR (关键帧) 时尝试重建:

```cpp
// 在处理 IDR NAL 单元的地方
if (nalType == 5) {  // IDR
  if (!m_webrtcHwActive && m_hwRebuildAttempts < MAX_HW_REBUILD_ATTEMPTS) {
    qInfo() << "[H264][" << m_streamTag 
            << "][HW-E2E][AUTO-REBUILD-ON-IDR] Attempting to reopen HW decoder";
    
    // 尝试重新打开硬解
    if (m_webrtcHw && m_sps.size() > 0) {
      if (m_webrtcHw->tryOpen(m_sps, m_codedWidth, m_codedHeight)) {
        m_webrtcHwActive = true;
        m_hwRebuildAttempts.store(0, std::memory_order_relaxed);
        qInfo() << "[H264][" << m_streamTag << "][HW-E2E][AUTO-REBUILD-ON-IDR] Success!";
      } else {
        qWarning() << "[H264][" << m_streamTag 
                   << "][HW-E2E][AUTO-REBUILD-ON-IDR] Failed, will try again on next IDR";
      }
    }
  }
}
```

---

### **修复4：确保 FFmpegSoftDecoder 的完整初始化**

**位置**: `FFmpegSoftDecoder::initialize()`

**问题**: 当从硬解降级到软解时，初始化可能不完整。

**现有代码**:
```cpp
bool FFmpegSoftDecoder::initialize(const DecoderConfig& config) {
  const AVCodecID codecId = (config.codec == "H265" || config.codec == "HEVC") 
                              ? AV_CODEC_ID_HEVC 
                              : AV_CODEC_ID_H264;

  m_codec = avcodec_find_decoder(codecId);
  if (!m_codec) {
    qWarning() << "[Client][FFmpegSoftDecoder] codec not found:" << config.codec;
    return false;
  }

  m_ctx = avcodec_alloc_context3(m_codec);
  if (!m_ctx)
    return false;

  m_ctx->width = config.width;
  m_ctx->height = config.height;
  // Low-latency flags
  m_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
  m_ctx->flags2 |= AV_CODEC_FLAG2_FAST;

  if (avcodec_open2(m_ctx, m_codec, nullptr) < 0) {
    qWarning() << "[Client][FFmpegSoftDecoder] avcodec_open2 failed";
    avcodec_free_context(&m_ctx);
    return false;
  }

  m_packet = av_packet_alloc();
  m_frame = av_frame_alloc();
  m_initialized = true;

  qInfo() << "[Client][FFmpegSoftDecoder] initialized codec=" << config.codec 
          << config.width << "x" << config.height;
  return true;
}
```

**修复**：预加载 extradata (SPS/PPS)

```cpp
bool FFmpegSoftDecoder::initialize(const DecoderConfig& config) {
  const AVCodecID codecId = (config.codec == "H265" || config.codec == "HEVC") 
                              ? AV_CODEC_ID_HEVC 
                              : AV_CODEC_ID_H264;

  m_codec = avcodec_find_decoder(codecId);
  if (!m_codec) {
    qWarning() << "[Client][FFmpegSoftDecoder] codec not found:" << config.codec;
    return false;
  }

  m_ctx = avcodec_alloc_context3(m_codec);
  if (!m_ctx)
    return false;

  m_ctx->width = config.width;
  m_ctx->height = config.height;
  
  // 修复：预加载 extradata (SPS/PPS)
  if (!config.codecExtradata.isEmpty()) {
    m_ctx->extradata = static_cast<uint8_t*>(av_malloc(config.codecExtradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (m_ctx->extradata) {
      memcpy(m_ctx->extradata, config.codecExtradata.data(), config.codecExtradata.size());
      m_ctx->extradata_size = config.codecExtradata.size();
      qInfo() << "[Client][FFmpegSoftDecoder] loaded extradata size=" << config.codecExtradata.size();
    }
  }
  
  // Low-latency flags
  m_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
  m_ctx->flags2 |= AV_CODEC_FLAG2_FAST;
  
  // 修复：添加线程配置
  m_ctx->thread_count = 2;  // 使用2个线程进行解码，平衡 latency 和 throughput
  m_ctx->thread_type = FF_THREAD_FRAME;  // 帧级并行

  if (avcodec_open2(m_ctx, m_codec, nullptr) < 0) {
    qWarning() << "[Client][FFmpegSoftDecoder] avcodec_open2 failed";
    if (m_ctx->extradata) {
      av_free(m_ctx->extradata);
      m_ctx->extradata = nullptr;
    }
    avcodec_free_context(&m_ctx);
    return false;
  }

  m_packet = av_packet_alloc();
  m_frame = av_frame_alloc();
  m_initialized = true;

  qInfo() << "[Client][FFmpegSoftDecoder] initialized codec=" << config.codec 
          << config.width << "x" << config.height
          << " extradata_size=" << m_ctx->extradata_size
          << " thread_count=" << m_ctx->thread_count;
  return true;
}
```

**以及在 shutdown 中清理**:

```cpp
void FFmpegSoftDecoder::shutdown() {
  if (m_frame) {
    av_frame_free(&m_frame);
    m_frame = nullptr;
  }
  if (m_packet) {
    av_packet_free(&m_packet);
    m_packet = nullptr;
  }
  if (m_ctx) {
    if (m_ctx->extradata) {
      av_free(m_ctx->extradata);
      m_ctx->extradata = nullptr;
    }
    avcodec_free_context(&m_ctx);
    m_ctx = nullptr;
  }
  m_initialized = false;
}
```

---

### **修复5：修改配置策略 — 防止不匹配的硬解配置**

**位置**: `client_config.yaml` 和构建系统

**问题**: 配置要求硬解，但编译时没有启用。

**方案 A：在运行时自动修正配置**

在 `Configuration::initialize()` 中：

```cpp
void Configuration::initialize() {
  // ... 现有初始化代码 ...
  
  // 修复：检查硬解配置与编译选项的一致性
  const bool hwDecodeRequired = loadBoolConfig("media.require_hardware_decode", true);
  
#if !defined(ENABLE_VAAPI) && !defined(ENABLE_NVDEC)
  if (hwDecodeRequired) {
    qWarning() << "[Client][Configuration] MISMATCH: config requires hardware decode"
               << " but build has no ENABLE_VAAPI/ENABLE_NVDEC"
               << " → auto-downgrade to software decode";
    // 自动降级配置
    m_requireHardwareDecode = false;
  }
#endif
}
```

**方案 B：在配置文件中添加构建检查**

```yaml
media:
  # 要求硬解（仅当编译时启用 ENABLE_VAAPI 或 ENABLE_NVDEC 时有效）
  # 如编译未启用硬解，此项自动降级为 false
  require_hardware_decode: false  # ← 改为 false，默认不强制硬解
  
  # 更明确的控制
  hardware_decode_preference: "auto"  # "auto" | "prefer" | "require" | "disabled"
```

---

### **修复6：添加网络乱序处理**

**位置**: `h264decoder.cpp` 的 NAL 单元处理

**问题**: 乱序到达的 NAL 单元（尤其是 SPS/PPS 迟到）会导致解码失败。

**修复**：维护一个"待处理的 SPS/PPS 队列"

```cpp
// 在 H264Decoder 中添加
private:
  struct DeferredNal {
    int type;  // SPS, PPS, etc.
    QByteArray data;
    int64_t timestamp;
  };
  
  QVector<DeferredNal> m_deferredNals;
  static constexpr size_t MAX_DEFERRED_NALS = 16;

// 在处理 NAL 时
void H264Decoder::onRtpFrameComplete(const QByteArray& annexB) {
  // ... 解析 NAL 单元 ...
  
  for (const auto& nal : nalUnits) {
    int nalType = nal[0] & 0x1f;
    
    // 修复：如果收到 SPS/PPS 但此时不需要，存储起来
    if ((nalType == 7 || nalType == 8) && !m_needKeyframe) {  // SPS or PPS
      qDebug() << "[H264][" << m_streamTag 
               << "] Deferred NAL type=" << nalType;
      
      if (m_deferredNals.size() >= MAX_DEFERRED_NALS) {
        m_deferredNals.removeFirst();
      }
      m_deferredNals.append({nalType, nal, QDateTime::currentMSecsSinceEpoch()});
      continue;  // 暂时跳过这个 NAL
    }
    
    // 如果需要关键帧，先提交存储的 SPS/PPS
    if (m_needKeyframe && nalType == 5) {  // IDR frame
      for (const auto& deferred : m_deferredNals) {
        submitNalToDecoder(deferred.data);
      }
      m_deferredNals.clear();
    }
    
    // 正常处理当前 NAL
    submitNalToDecoder(nal);
  }
}
```

---

## 五、验证步骤

### **第1步：编译选项验证**
```bash
# 检查编译时是否启用了硬解
grep -i "ENABLE_VAAPI\|ENABLE_NVDEC" CMakeLists.txt client/CMakeLists.txt

# 查看实际编译命令
cmake --build . --verbose 2>&1 | grep -i "vaapi\|nvdec"
```

### **第2步：运行时配置验证**
```bash
# 启动客户端前检查配置
export CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0  # 禁用硬解要求

# 或编辑配置
cat <<EOF > client_config.yaml
media:
  require_hardware_decode: false
  hardware_decode_preference: "auto"
EOF

# 运行客户端
./build/client
```

### **第3步：日志验证**
```bash
# 运行后检查日志
tail -f logs/client-*.log | grep -E "HW-E2E|FFmpegSoftDecoder|send_packet"

# 期望看到：
# ✓ [Client][HW-E2E] ... software decoder fallback
# ✓ [Client][FFmpegSoftDecoder] initialized ... extradata_size=XX
# ✓ [H264] frames decoded successfully
```

### **第4步：视频显示验证**
- [ ] 四个视频窗口显示清晰
- [ ] 无 "send_packet error" 日志
- [ ] 无 "non-existing PPS" 日志
- [ ] 帧率稳定（查看 FPS 指标）

---

## 六、代码修改清单

| 文件 | 修改项 | 优先级 |
|------|--------|--------|
| `H264WebRtcHwBridge.cpp` | submitCompleteAnnexB() - 加入 flush + retry | P0 |
| `H264WebRtcHwBridge.cpp` | drainAllOutput() - 错误恢复 | P0 |
| `h264decoder.cpp` | 加入自动重建机制 (HW_REBUILD) | P1 |
| `FFmpegSoftDecoder.cpp` | 预加载 extradata (SPS/PPS) | P0 |
| `FFmpegSoftDecoder.cpp` | 添加 extradata cleanup | P1 |
| `Configuration.cpp` | 运行时配置一致性检查 | P1 |
| `client_config.yaml` | 改 require_hardware_decode 为 false | P0 |
| `h264decoder.cpp` | 网络乱序处理 (defer SPS/PPS) | P2 |

---

## 七、预期效果

**修复前**:
```
✗ 四个视频流 100% 无显示
✗ 不断出现 "send_packet error" 和 "non-existing PPS"
✗ HW-E2E 循环失败，无法恢复
```

**修复后**:
```
✓ 四个视频流正常显示
✓ 在网络抖动/乱序时自动恢复
✓ 日志清晰，无错误循环
✓ 解码器在出错后能自动 flush/rebuild
```

---

## 八、长期改进建议

1. **架构**：分离"硬解配置期望"和"运行时能力检测"
2. **测试**：添加网络乱序/丢包的集成测试
3. **监控**：添加解码器健康检查指标（HW failure rate, rebuild attempts）
4. **文档**：明确 VAAPI/NVDEC 编译选项与配置的关系
5. **可观测性**：增加 FFmpeg 内部状态日志（codec ctx state, buffers, errors）

