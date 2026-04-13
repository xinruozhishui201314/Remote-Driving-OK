# 编码-解码不一致可视化对照

---

## 🔴 问题 #1: 硬解初始化失败无降级

```
┌─────────────────────────────────────────────────────┐
│           黑屏的根本原因链                           │
└─────────────────────────────────────────────────────┘

Step 1: 编码端（✅ 正常）
  CARLA BGR24 → libx264 (yuv420p) → FLV/RTMP → ZLM
  └─ 编码完全没问题

Step 2: ZLM 中继（✅ 正常）
  FLV/RTMP → WebRTC 转发 → RTP H.264
  └─ 中继完全没问题

Step 3: 客户端 RTP 接收（✅ 正常）
  RTP → NAL 解包 → Annex-B 重组
  └─ 接收完全没问题

Step 4: 解码初始化（❌ 问题在这里）
  
  requireHardwareDecode() == true
        ↓
  硬解初始化失败（Docker 无 GPU）
        ↓
  拒绝软解降级（见 h264decoder.cpp 第 213-230 行）
        ↓
  ensureDecoder() 返回 false
        ↓
  codecOpen = false
        ↓
  🖥️ 100% 黑屏

┌──────────────────────────────────────────────────────┐
│ 关键代码位置: client/src/h264decoder.cpp 第 213-230 行 │
│                                                       │
│ if (Configuration::instance().requireHardwareDecode())│
│ {                                                     │
│   if (!kClientHwDecodeCompiled) {                     │
│     qCritical() << "[HW-REQUIRED] 未编译";            │
│     return false;  // ← ❌ 无条件拒绝               │
│   }                                                   │
│                                                       │
│   if (!m_decoder) {                                   │
│     qCritical() << "[HW-REQUIRED] 不可用";            │
│     return false;  // ← ❌ 二次拒绝                 │
│   }                                                   │
│ }                                                     │
│ // 继续创建软解...  ← 永远执行不到这里              │
└──────────────────────────────────────────────────────┘

修复方案（需要改代码逻辑）:

  if (Configuration::instance().requireHardwareDecode()) {
    if (!kClientHwDecodeCompiled) {
      // 硬解未编译 → 报错（无法解决）
      return false;
    }
    // 硬解编译但不可用 → 警告但继续（自动降级）
    qWarning() << "[HW-DEGRADED] 自动降级至软解";
  }
  // 继续创建软解...  ← ✅ 现在可以执行
```

---

## 🟡 问题 #2: Python vs C++ carla-bridge 参数不同

```
┌──────────────────────────────────────────────────────────────────┐
│                   编码参数对比                                   │
└──────────────────────────────────────────────────────────────────┘

1. 码率控制
   
   Python (carla_bridge.py):
   ┌─────────────────────────────────────────────┐
   │ cmd.extend(_ffmpeg_libx264_bitrate_args())   │
   │   ↓                                          │
   │ return [                                     │
   │   "-b:v", f"{VIDEO_BITRATE_KBPS}k",        │ ← 可控
   │   "-maxrate", f"{VIDEO_BITRATE_KBPS}k",     │
   │   "-bufsize", f"{bufsize}k",                │
   │ ]                                            │
   │   ↓                                          │
   │ 默认: 2000 kbps (可配)                       │
   └─────────────────────────────────────────────┘
   
   C++ (rtmp_pusher.cpp):
   ┌─────────────────────────────────────────────┐
   │ oss << "ffmpeg ... -pix_fmt yuv420p"         │
   │       << " -f flv " << m_rtmpUrl;            │
   │   ↓                                          │
   │ ❌ 无 -b:v 参数                              │
   │ ❌ 无 -maxrate 参数                          │
   │ ❌ 无 -bufsize 参数                          │
   │   ↓                                          │
   │ 使用 x264 默认码率 (~5000+ kbps ❌)          │
   └─────────────────────────────────────────────┘
   
   后果:
     Python: 2000 kbps 可控推流 ✅
     C++:    5000+ kbps 无控推流 ❌
     → 质量完全不同！


2. 切片数控制
   
   Python (carla_bridge.py):
   ┌─────────────────────────────────────────────┐
   │ cmd.extend(_ffmpeg_libx264_slice_args())     │
   │   ↓                                          │
   │ n = int(os.environ.get("CARLA_X264_SLICES",│
   │                        "1"))                │
   │ return ["-x264-params", f"slices={n}"]      │ ← 可控
   │   ↓                                          │
   │ 默认: 1 切片 (可配)                          │
   └─────────────────────────────────────────────┘
   
   C++ (rtmp_pusher.cpp):
   ┌─────────────────────────────────────────────┐
   │ oss << "... -tune zerolatency -pix_fmt yuv420p" │
   │       << " -g " << m_fps << " ...";         │
   │   ↓                                          │
   │ ❌ 无 -x264-params slices=N 参数             │
   │   ↓                                          │
   │ 使用 x264 默认切片数 (通常 > 1 ❌)           │
   └─────────────────────────────────────────────┘
   
   后果:
     Python: 1 切片可控推流 ✅
     C++:    多切片无控推流 ❌
     → 客户端可能条纹伪影！


3. 综合对比表
   
   ┌────────────────┬──────────────────┬──────────────────┐
   │ 参数           │ Python Path      │ C++ Path         │
   ├────────────────┼──────────────────┼──────────────────┤
   │ 码率           │ 2000 kbps ✅     │ x264 默认 ❌     │
   │ 最大码率       │ 2000 kbps ✅     │ 无限制 ❌        │
   │ 缓冲区         │ 4000 kbps ✅     │ 无配置 ❌        │
   │ 切片数         │ 1 片 ✅          │ x264 默认 ❌     │
   │ GOP            │ 10 帧 ✅         │ 10 帧 ✅         │
   │ Preset         │ ultrafast ✅     │ ultrafast ✅     │
   │ Tune           │ zerolatency ✅   │ zerolatency ✅   │
   │ 色彩空间       │ yuv420p ✅       │ yuv420p ✅       │
   └────────────────┴──────────────────┴──────────────────┘
   
   C++ 路径明显参数不完整！
```

---

## 🟡 问题 #3: SDP 宣称与编码器实现不符

```
┌──────────────────────────────────────────────────────────┐
│            H.264 Profile Level ID 不一致                 │
└──────────────────────────────────────────────────────────┘

SDP 协议层面（webrtcclient.cpp 行 537-538）:

  a=fmtp:96 packetization-mode=1;profile-level-id=42e01f
  
  分解:
    42    = 0x42 = Baseline Profile (profile_idc)
    e0    = 0xe0 = constraint flags (baseline 不用 constraint set 1/2)
    1f    = 0x1f = Level 3.0 (level_idc)
  
  宣称:
    ✅ Baseline Profile
    ✅ Level 3.0
    ✅ Packetization Mode 1 (single NAL unit or aggregation)


编码器实现（carla_bridge.py 行 221-237）:

  cmd = [
    "ffmpeg", "-y",
    "-f", "rawvideo", "-pix_fmt", "bgr24", "-s", f"{width}x{height}",
    "-i", "pipe:0",
    "-c:v", "libx264",              ← libx264 编码
    "-preset", "ultrafast",
    "-tune", "zerolatency",
    "-pix_fmt", "yuv420p",
    "-g", str(fps),
    "-keyint_min", str(fps),
    "-f", "flv",
    rtmp_url,
  ]
  
  ❌ 问题: 无 -profile:v 指定
  
  结果:
    x264 使用 默认 Profile
    默认是 High Profile（支持更多特性）
    而不是 Baseline Profile


后果:

  ┌─────────────────────────────────────────┐
  │ SDP 说: "我是 Baseline"                 │
  │ SPS 中: "我是 High"                     │
  │   ↓                                    │
  │ 协议不符 ⚠️                            │
  │   ↓                                    │
  │ 严格验证器可能拒绝                     │
  │ 实际解码器都能兼容（High ⊃ Baseline）  │
  └─────────────────────────────────────────┘

修复方案（1 行代码）:

  cmd = [
    ...
    "-c:v", "libx264",
    "-profile:v", "baseline",  ← ✅ 添加这一行
    "-level", "3.0",          ← ✅ 可选但推荐
    "-preset", "ultrafast",
    ...
  ]
```

---

## 🟡 问题 #4: 多切片 + 多线程水平条纹

```
┌──────────────────────────────────────────────────────────┐
│          多切片 × 多线程的条纹伪影                       │
└──────────────────────────────────────────────────────────┘

触发条件（都满足时发生）:

  ① 编码器生成多个切片
     └─ CARLA_X264_SLICES > 1 (Python) 
        或 x264 默认多切片 (C++)
  
  ② 客户端多线程解码
     └─ CLIENT_FFMPEG_DECODE_THREADS > 1
  
  ③ 解码器没有条纹缓解
     └─ tryMitigateStripeRiskIfNeeded() 返回 false

过程:

  多切片 H.264 码流
        ↓
  RTP 传输 (FU-A 分段)
        ↓
  客户端 Annex-B 重组
        ↓
  libavcodec 接收 (多个切片的 NAL)
        ↓
  FF_THREAD_SLICE 模式激活 (多线程)
        ↓
  多个线程并行处理不同切片
        ↓
  切片间同步错误
        ↓
  🖼️ 水平条纹伪影（像素行不对齐）


示例（可视化）:

  预期输出:                    实际输出（有条纹）:
  ┌──────────────┐             ┌──────────────┐
  │ ████████████ │ ← 切片1     │ ████████████ │
  │ ████████████ │             │ ^^^^^^^^^^^^^^│ ← 条纹
  │ ████████████ │ ← 切片2     │ ████████████ │
  │ ████████████ │             │ ^^^^^^^^^^^^^^│ ← 条纹
  └──────────────┘             └──────────────┘
  (正常图像)                    (水平条纹)


缓解措施（已实现）:

  h264decoder.cpp 行 1535:
  
    if (tryMitigateStripeRiskIfNeeded(sliceCount, ...)) {
      qWarning() << "abort_decode stripe_mitigation_applied";
      return;  // ← 强制单线程解码
    }
  
  当检测到多切片 + 多线程时:
    强制 libavcodec thread_count = 1
    → FF_THREAD_SLICE 无法启用
    → 条纹伪影消除


预防（建议）:

  编码端默认:
    CARLA_X264_SLICES=1         ✅ 已是默认
  
  解码端默认:
    CLIENT_FFMPEG_DECODE_THREADS=1  ✅ 已是默认
  
  结果:
    即使缓解措施失效，默认也不会触发条纹 ✅
```

---

## ✅ 正常工作的部分

```
┌──────────────────────────────────────────────────────────┐
│              编码-解码完全一致的部分                     │
└──────────────────────────────────────────────────────────┘

1. 色彩空间（✅ 完全一致）
   
   CARLA BGRA
     ↓ (剥离 alpha)
   BGR24
     ↓ (libx264)
   YUV420P (4:2:0 Planar)
     ↓ (RTP 传输)
   H.264 码流
     ↓ (客户端解码)
   YUV420P (libavcodec)
     或 NV12 (VAAPI/NVDEC)
     ↓ (swscale 转换)
   RGBA
     ↓ (显示)
   正确的画面 ✅


2. 分辨率和 FPS（✅ 完全一致）
   
   编码端设置: CAMERA_WIDTH=640, CAMERA_HEIGHT=480, CAMERA_FPS=10
     ↓
   编码到 H.264 SPS: width=640, height=480, fps=10
     ↓
   RTP 传输
     ↓
   客户端从 SPS 解析: width=640, height=480
   从 RTP 时钟: 90000 Hz (标准)
     ↓
   正确的分辨率和帧率 ✅


3. GOP 和 IDR 间隔（✅ 完全一致）
   
   编码端:
     cmd.extend(["-g", str(fps)])         # GOP = FPS (默认 10)
     cmd.extend(["-keyint_min", str(fps)]) # 最小间隔 = FPS
     → 每 10 帧一个 IDR 帧
   
   解码端:
     for (const auto &nal : nalUnits) {
       int t = static_cast<uint8_t>(nal[0]) & 0x1f;
       if (t == 5) idrCount++;  // 检测 IDR NAL (type=5)
     }
     → 能正确检测 IDR 帧 ✅


4. RTP 时钟（✅ 完全一致）
   
   标准: RFC 3551 H.264 over RTP
     RTP 时钟频率 = 90000 Hz
   
   编码端:
     隐式（x264 编码时自动处理）
   
   客户端:
     a=rtpmap:96 H264/90000  ← SDP 中声称
     RtpTrueJitterBuffer.cpp 注释: "90000 Hz clock"
   
   验证:
     quint32 ts = rtpTimestamp(data);  // 90000 Hz 时钟
     ✅ 完全一致
```

---

## 📊 综合严重性评估

```
┌────────────────────────────────────────────────────┐
│             不一致问题严重性矩阵                  │
└────────────────────────────────────────────────────┘

优先级   问题                    影响范围    修复成本    状态
────────────────────────────────────────────────────────

🔴 P0   硬解初始化无降级        所有环境    ~10 行      ❌ 未修复
         (黑屏问题)             全部黑屏    15 分钟     ⚠️ 严重

🟡 P1   C++ 参数不完整          使用 C++    ~10 行      ❌ 未修复
         (码率/切片无控)        路径的项目  20 分钟     ⚠️ 重要

🟡 P2   SDP Profile 不符        协议验证    ~1 行       ❌ 未修复
         (Baseline vs High)     严格场景    5 分钟      📝 文档

🟡 P2   多切片 + 多线程         已缓解      已实现      ✅ 已缓解
         (条纹伪影)             风险低      监控        🟢 监控

🟢 OK   色彩空间一致            所有场景    -           ✅ 正常

🟢 OK   分辨率/FPS 一致        所有场景    -           ✅ 正常

🟢 OK   RTP 时钟一致           所有场景    -           ✅ 正常


修复优先顺序:
1. ✅ P0 (本周) → 硬解初始化无降级
2. ✅ P1 (本月) → C++ 参数不完整
3. ✅ P2 (下月) → SDP Profile 不符
4. 📊 P2 (持续) → 多线程监控
```

---

## 🎯 修复后预期效果

```
修复前状态                         修复后状态

❌ Python 正常                    ✅ Python 正常
❌ C++ 参数不完整                 ✅ C++ 参数完整
❌ Docker 无 GPU 黑屏             ✅ Docker 自动软解
❌ SDP/SPS Profile 不符           ✅ SDP/SPS 一致
⚠️ 多切片 + 多线程风险            ✅ 风险可控
✅ 色彩空间完全一致               ✅ 色彩空间完全一致

结果:
  编码-解码路径完全一致 ✅
  所有场景稳定工作 ✅
  可用于生产环境 ✅
```

---

**可视化文档版本**: v1.0  
**生成日期**: 2026-04-11
