#pragma once
#include <QByteArray>
#include <QSize>
#include <QString>

#include <cstdint>
#include <memory>

/**
 * 视频帧零拷贝表示（《客户端架构设计》§3.1.2）。
 *
 * 兼容多种 GPU 路径：
 *   CPU_MEMORY    : software decode / hw decode + CPU transfer（通用）
 *   DMA_BUF       : Linux VAAPI/V4L2 → EGL DMA-BUF → GL texture（Intel/AMD 零拷贝）
 *   GPU_TEXTURE_GL: 已在 GPU 的 GL 纹理（CUDA/NVDEC interop 路径）
 *   VAAPI_SURFACE : VA-API surface handle（备用路径）
 */
struct VideoFrame {
  // ── 内存类型 ─────────────────────────────────────────────────────────────
  enum class MemoryType {
    CPU_MEMORY,
    GPU_TEXTURE_GL,
    GPU_TEXTURE_VULKAN,
    DMA_BUF,
    D3D11_TEXTURE,
    VAAPI_SURFACE,
  };

  // ── 像素格式 ─────────────────────────────────────────────────────────────
  enum class PixelFormat {
    UNKNOWN = 0,
    YUV420P,  // 3 planes: Y, U, V（软件解码默认）
    NV12,     // 2 planes: Y + UV 交织（VAAPI/NVDEC 常见输出）
    NV21,     // 2 planes: Y + VU 交织
    P010,     // 10-bit NV12（HDR）
    RGBA32,   // 单平面 RGBA
  };

  // ── CPU 内存平面 ──────────────────────────────────────────────────────────
  struct PlaneInfo {
    PlaneInfo() : data(nullptr), stride(0), size(0) {}
    PlaneInfo(void* d, uint32_t str, uint32_t s) : data(d), stride(str), size(s) {}
    void* data = nullptr;
    uint32_t stride = 0;
    uint32_t size = 0;
  };

  // ── DMA-BUF 描述符（Linux 零拷贝路径） ──────────────────────────────────
  struct DmaBufInfo {
    DmaBufInfo() : fds{-1, -1, -1, -1}, offsets{}, pitches{}, drmFourcc(0), modifiers{}, nbPlanes(0) {}
    static constexpr int MAX_PLANES = 4;
    int fds[MAX_PLANES] = {-1, -1, -1, -1};  // fd per plane（-1 = 共享 fds[0]）
    uint32_t offsets[MAX_PLANES] = {};       // byte offset in buffer
    uint32_t pitches[MAX_PLANES] = {};       // stride in bytes
    uint32_t drmFourcc = 0;                  // DRM_FORMAT_NV12 etc.
    uint64_t modifiers[MAX_PLANES] = {};     // DRM format modifiers
    int nbPlanes = 0;
  };

  MemoryType memoryType = MemoryType::CPU_MEMORY;
  PixelFormat pixelFormat = PixelFormat::YUV420P;
  /** libav AV_FRAME_FLAG_INTERLACED；用于 NV12 CPU 去隔行 / DMA-BUF 路径告警 */
  bool interlacedMetadata = false;
  /** AV_FRAME_FLAG_TOP_FIELD_FIRST */
  bool topFieldFirst = true;
  uint32_t width = 0;
  uint32_t height = 0;
  int64_t pts = 0;
  int64_t captureTimestamp = 0;  // 原始采集时间戳（用于 E2E 延迟计算）
  uint32_t cameraId = 0;
  quint64 frameId = 0;  // 端到端帧序列号（解码 → WebRtcClient → QVideoSink / QML）
  quint64 lifecycleId = 0;

  PlaneInfo planes[3] = {};  // Y, U, V 或 RGB（CPU_MEMORY 路径）
  DmaBufInfo dmaBuf;         // DMA_BUF 路径的描述符

  union GpuHandle {
    GpuHandle() : glTextureId(0) {}
    uint32_t glTextureId = 0;
    uintptr_t vaapiSurface;
    void* d3d11Texture;
  } gpuHandle{};

  // 持有底层 AVFrame/surface 的生命周期引用
  // 确保 DMA-BUF fd 在 GL 纹理创建并使用完毕之前不被关闭
  std::shared_ptr<void> poolRef;

  VideoFrame()
      : memoryType(MemoryType::CPU_MEMORY),
        pixelFormat(PixelFormat::YUV420P),
        interlacedMetadata(false),
        topFieldFirst(true),
        width(0),
        height(0),
        pts(0),
        captureTimestamp(0),
        cameraId(0),
        frameId(0),
        lifecycleId(0),
        planes{},
        dmaBuf(),
        gpuHandle(),
        poolRef() {}

  // ★★★ v3 新增：生命周期 ID 生成器 ★★★
  // 在 webrtcclient.cpp 的 RTP 包到达时递增并分配给 frame
  static std::atomic<quint64>& globalLifecycleIdCounter() {
    static std::atomic<quint64> counter{0};
    return counter;
  }
  static quint64 nextLifecycleId() { return ++globalLifecycleIdCounter(); }

  void reset() {
    pts = 0;
    captureTimestamp = 0;
    pixelFormat = PixelFormat::YUV420P;
    interlacedMetadata = false;
    topFieldFirst = true;
    for (auto& p : planes) {
      p.data = nullptr;
      p.stride = 0;
      p.size = 0;
    }
    for (int i = 0; i < DmaBufInfo::MAX_PLANES; ++i) {
      dmaBuf.fds[i] = -1;
      dmaBuf.offsets[i] = 0;
      dmaBuf.pitches[i] = 0;
    }
    dmaBuf.drmFourcc = 0;
    dmaBuf.nbPlanes = 0;
    poolRef.reset();
  }
};

/**
 * 解码器配置。
 */
struct DecoderConfig {
  int width = 1920;
  int height = 1080;
  QString codec = "H264";  // H264 / H265 / AV1
  /** AVCDecoderConfigurationRecord（与 H264Decoder::openDecoderWithExtradata 一致），硬解 open 前写入
   * avcodec extradata */
  QByteArray codecExtradata;
  /**
   * true：VAAPI 优先尝试 av_hwframe_map → DRM_PRIME / DMA-BUF（零拷贝导出，需下游 RHI/EGL 消费）。
   * false：直接从 hw surface transfer 到 CPU NV12（WebRTC→QImage 稳定路径；仍保留硬解 CABAC）。
   */
  bool preferDmaBufExport = true;
};

/**
 * 解码器能力。
 */
struct DecoderCapabilities {
  bool supportsH264 = false;
  bool supportsH265 = false;
  bool supportsHW = false;
  QString name;
};

enum class DecodeResult { Ok, NeedMore, Error, EOF_Stream };

/**
 * 硬件解码器纯虚接口（《客户端架构设计》§3.1.2）。
 */
class IHardwareDecoder {
 public:
  virtual ~IHardwareDecoder() = default;

  virtual bool initialize(const DecoderConfig& config) = 0;
  virtual void shutdown() = 0;

  virtual DecodeResult submitPacket(const uint8_t* data, size_t size, int64_t pts, int64_t dts) = 0;
  virtual DecodeResult receiveFrame(VideoFrame& frame) = 0;
  virtual void flush() = 0;
  virtual DecoderCapabilities queryCapabilities() const = 0;
  virtual bool isHardwareAccelerated() const = 0;

  /**
   * 运行时关闭 DRM PRIME / DMA-BUF 导出，使后续 receiveFrame 走 CPU NV12（如 VAAPI
   * av_hwframe_transfer_data）。默认空操作；硬解实现按需覆盖。供 Scene Graph DMA-BUF 呈现失败时回退。
   */
  virtual void setDmaBufExportPreferred(bool enable) { (void)enable; }
};
