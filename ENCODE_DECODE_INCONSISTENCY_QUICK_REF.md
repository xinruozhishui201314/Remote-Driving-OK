# 编码-解码不一致问题速查表

**快速参考**: 本文档总结了 carla-bridge 编码和客户端解码之间的所有重要不一致

---

## 🚨 最严重的不一致（必须修复）

### 1. **硬解初始化失败时完全无法降级** 

**问题位置**: `client/src/h264decoder.cpp` 行 213-230

**当前行为**（❌ 错误）:
```cpp
if (Configuration::instance().requireHardwareDecode()) {
  // 硬解被要求但不可用 → 直接返回 false
  return false;
}
```

**结果**: 解码器无法初始化 → **100% 黑屏**

**正确行为**（✅）:
```cpp
if (Configuration::instance().requireHardwareDecode()) {
  if (!kClientHwDecodeCompiled) {
    // 只有当硬解未编译时才报错
    qCritical() << "[HW-REQUIRED] 未编译";
    return false;
  }
  // 硬解编译但不可用 → 自动降级至软解（仅警告）
  qWarning() << "[HW-DEGRADED] 自动降级至软解";
}
// 继续创建软解...
```

**修复成本**: ~10 行代码  
**影响范围**: Docker 环境无 GPU 的所有部署  
**优先级**: 🔴 **P0 - 立即修复**

---

### 2. **C++ carla-bridge 缺少编码参数控制**

**问题位置**: `carla-bridge/cpp/src/rtmp_pusher.cpp` 行 90-96

**当前情况** (❌ 不一致):

```
Python carla_bridge.py:
  - 码率: 2000 kbps (可控)
  - 切片: 1 (可控)
  - GOP: 10 (可控)
  ✅ 完整参数支持

C++ rtmp_pusher.cpp:
  - 码率: x264 默认 (~5000+ kbps)
  - 切片: x264 默认 (通常 > 1)
  - GOP: 10 (无法控制)
  ❌ 参数不完整
```

**根本原因**:

```cpp
// C++ 路径（行 90-96）：
oss << "ffmpeg -y -f rawvideo -pix_fmt bgr24 -s " << m_width << "x" << m_height
    << " -r " << m_fps << " -i pipe:0"
    << " -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p"
    << " -g " << m_fps << " -keyint_min " << m_fps
    << " -f flv " << m_rtmpUrl << " 2>/dev/null";

// ❌ 缺失的参数：
//   -b:v 2000k
//   -maxrate 2000k
//   -bufsize 4000k
//   -x264-params slices=1
```

**后果**:
- C++ 推流质量与 Python 不一致
- C++ 推出的码流码率更高、可能产生多切片
- 客户端解码可能条纹伪影（多切片 + 多线程）

**修复**:
```cpp
// 读取环境变量
int bitrate = getenv("VIDEO_BITRATE_KBPS") ? stoi(getenv("VIDEO_BITRATE_KBPS")) : 2000;
int slices = getenv("CARLA_X264_SLICES") ? stoi(getenv("CARLA_X264_SLICES")) : 1;

// 添加参数
oss << " -b:v " << bitrate << "k"
    << " -maxrate " << bitrate << "k"
    << " -bufsize " << (bitrate * 2) << "k"
    << " -x264-params slices=" << slices;
```

**修复成本**: ~10 行代码  
**影响范围**: 所有使用 C++ carla-bridge 的部署  
**优先级**: 🟡 **P1 - 本周修复**

---

## 🔍 中等不一致（推荐改进）

### 3. **SDP 声称 Baseline Profile，但编码器可能 High**

**问题位置**:
- SDP 宣称: `webrtcclient.cpp` 行 537-538
- 编码器: `carla-bridge/carla_bridge.py` 行 225

**当前情况** (⚠️ 协议不符):

```
SDP 中声称:
  a=fmtp:96 packetization-mode=1;profile-level-id=42e01f
  └─ 42e01f = Baseline Profile, Level 3.0

实际编码器 (carla_bridge.py):
  "-c:v", "libx264",    # ← 无 -profile:v 指定
  "-preset", "ultrafast",
  └─ x264 默认使用 High Profile

结果:
  ✅ 解码兼容（High 包含 Baseline）
  ❌ 协议不符（违反 H.264 规范声明）
  ⚠️ 严格验证器可能拒绝
```

**修复** (1 行):
```python
# carla-bridge/carla_bridge.py 行 225-226 附近添加：
cmd.extend(["-profile:v", "baseline", "-level", "3.0"])
```

**修复成本**: ~1 行代码  
**影响范围**: 规范兼容性  
**优先级**: 🟡 **P2 - 本月改进**

---

### 4. **多切片 + 多线程条纹伪影**

**问题位置**:
- 编码端: `carla-bridge/carla_bridge.py` 行 228 (`CARLA_X264_SLICES`)
- 解码端: `client/src/h264decoder.cpp` 行 250-274

**当前情况** (⚠️ 已文档但仍有风险):

```
当以下条件都满足时：
  ① 编码器生成多个切片: CARLA_X264_SLICES > 1
  ② 客户端多线程解码: CLIENT_FFMPEG_DECODE_THREADS > 1

结果:
  libavcodec 启用 FF_THREAD_SLICE 模式
  → 多个线程并行处理不同切片
  → 切片间同步错误
  → 水平条纹伪影
```

**文档**: `h264decoder.cpp` 行 250-274 有详细说明

**缓解** (已实现):
```cpp
// tryMitigateStripeRiskIfNeeded() 会强制单线程解码
if (sliceCount > 1 && m_ctx && m_ctx->thread_count > 1) {
  // 强制 thread_count = 1
}
```

**建议** (预防):
1. **编码端默认**: `CARLA_X264_SLICES=1` ✅ 已是默认
2. **解码端默认**: `CLIENT_FFMPEG_DECODE_THREADS=1` ✅ 已是默认

**修复成本**: 已实现，无需改动  
**优先级**: 🟡 **P2 - 监控（已缓解）**

---

### 5. **RTP Payload Type 协商与实现**

**问题位置**:
- 客户端 SDP 声称: `webrtcclient.cpp` 行 527
- 客户端期望: `h264decoder.cpp` 行 792-804

**当前情况** (✅ 实际一致):

```
SDP 中声称:
  a=rtpmap:96 H264/90000   # ← PT = 96

客户端接收:
  m_expectedRtpPayloadType = 96
  if (payloadType != 96) drop;  # ← 检查一致
```

**备注**: ZLM 的 `config.ini` 中的 `h264_pt=98` 是针对 RTP Proxy（不同场景），与标准 WebRTC 无关

**结论**: ✅ 一致  
**优先级**: 🟢 **OK - 无需改动**

---

## 📊 色彩空间一致性检查

**完整链路**:

```
CARLA 相机 (BGRA)
    ↓
carla-bridge 剥离 alpha (BGR24)
    ↓
libx264 编码 (BGR24 → YUV420P)
    ↓
H.264 码流 (YUV420P 4:2:0)
    ↓
RTP 传输 (NAL 单元)
    ↓
客户端 RTP 解包 (RFC 6184)
    ↓
libavcodec 解码 (Annex-B → YUV420P)
    或
VAAPI 解码 (Annex-B → NV12)
    或
NVDEC 解码 (Annex-B → NV12)
    ↓
swscale 转换 (YUV420P/NV12 → RGBA)
    ↓
QImage (RGBA8888)
    ↓
显示
```

**一致性评估**: ✅ **完全一致**

所有环节都正确处理 4:2:0 YUV，色彩空间转换链完整。

---

## 📋 一致性汇总表

| # | 项目 | 编码端 | 解码端 | 一致性 | 严重性 | 修复 |
|----|------|--------|--------|--------|--------|------|
| 1 | 硬解初始化 | N/A | requireHW 拒绝软解 | ❌ 无降级 | 🔴 P0 | 实现降级逻辑 |
| 2 | 编码参数 | Python ✅ / C++ ❌ | 接收 RTP | ❌ 参数不对等 | 🟡 P1 | 同步 C++ 参数 |
| 3 | SDP Profile | 声称 Baseline | x264 可能 High | ⚠️ 不符 | 🟡 P2 | 添加 `-profile:v baseline` |
| 4 | 多切片 + 多线程 | 可能生成 | 可能条纹 | ⚠️ 已缓解 | 🟡 P2 | 保持默认 + 监控 |
| 5 | 色彩空间 | YUV420P | YUV420P/NV12 | ✅ 一致 | 🟢 - | - |
| 6 | 分辨率 | 640×480 (可配) | 根据 SPS | ✅ 透传 | 🟢 - | - |
| 7 | FPS | 10 (可配) | RTP 90k Hz | ✅ 标准 | 🟢 - | - |
| 8 | GOP | 10 帧 = FPS | IDR 检测 | ✅ 一致 | 🟢 - | - |
| 9 | RTP 时钟 | N/A | 90000 Hz | ✅ 标准 | 🟢 - | - |
| 10 | Payload Type | N/A | PT=96 协商 | ✅ 一致 | 🟢 - | - |

---

## 🔧 修复优先级和计划

### **本周完成 (周五前)**

- [ ] **修复 P0**: 硬解初始化无降级 (`h264decoder.cpp` 行 213-230)
  - 改 `requireHardwareDecode()` 逻辑
  - 预期修复时间: **15 分钟**
  - 测试: Docker 环境无 GPU 验证黑屏消除

### **本月完成 (4月底前)**

- [ ] **修复 P1**: C++ carla-bridge 参数同步 (`cpp/src/rtmp_pusher.cpp` 行 90-96)
  - 添加 `-b:v`, `-maxrate`, `-bufsize`, `-x264-params slices=N`
  - 读取 `VIDEO_BITRATE_KBPS` 和 `CARLA_X264_SLICES` 环境变量
  - 预期修复时间: **20 分钟**
  - 测试: 使用 C++ 推流验证参数一致

- [ ] **改进 P2**: SDP Profile 一致性 (`carla-bridge/carla_bridge.py` 行 225)
  - 添加 `-profile:v baseline -level 3.0`
  - 预期修复时间: **5 分钟**
  - 测试: 验证编码 SPS profile-level 字段

### **下季度优化 (Q2)**

- [ ] 实现三层硬解模式 (`AUTO` | `PREFER` | `REQUIRE` | `DISABLED`)
- [ ] MQTT encoder hint 热应用（ffmpeg 推流重启）
- [ ] 多线程解码性能对标（CPU vs GPU）

---

## 📝 修复验证检查清单

修复后验证以下项：

```
[ ] 修复硬解初始化
  [ ] Docker 环境无 GPU 仍能显示视频
  [ ] 日志中 FFmpegSoftDecoder 正常初始化
  [ ] 不再有 [HW-REQUIRED] CRITICAL 错误

[ ] 同步 C++ carla-bridge 参数
  [ ] C++ 推流时码率受控（日志显示 -b:v）
  [ ] C++ 推流时切片数受控（日志显示 slices=）
  [ ] 客户端解码质量与 Python 相同

[ ] 改进 SDP Profile
  [ ] 编码 SPS 中 profile-level 字段正确
  [ ] SDP 与 SPS 声明一致

[ ] 多切片 + 多线程
  [ ] 保持 CARLA_X264_SLICES=1
  [ ] 保持 CLIENT_FFMPEG_DECODE_THREADS=1
  [ ] 观察 7 天无条纹伪影报告
```

---

## 🎯 总体结论

**编码-解码一致性状态**: ⚠️ **部分不一致**

**工作正常的部分** (✅):
- 色彩空间一致 (YUV420P 4:2:0)
- 分辨率和 FPS 透传
- GOP 和 IDR 间隔一致
- RTP 时钟标准 (90000 Hz)

**需要修复的部分** (❌🟡):
1. 硬解初始化无降级 (**P0 - 立即修**)
2. C++ 参数控制缺失 (**P1 - 本月修**)
3. SDP/SPS Profile 不符 (**P2 - 下月改**)
4. 多切片多线程风险 (**P2 - 已缓解**)

**修复后预期**: 编码-解码路径完全一致，所有场景稳定工作

---

**文档版本**: v1.0  
**生成日期**: 2026-04-11  
**维护者**: Development Team
