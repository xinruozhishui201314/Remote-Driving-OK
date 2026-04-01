#pragma once
#include <memory>
#include <cstdint>
#include <QSize>
#include <QString>

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
        UNKNOWN  = 0,
        YUV420P,     // 3 planes: Y, U, V（软件解码默认）
        NV12,        // 2 planes: Y + UV 交织（VAAPI/NVDEC 常见输出）
        NV21,        // 2 planes: Y + VU 交织
        P010,        // 10-bit NV12（HDR）
        RGBA32,      // 单平面 RGBA
    };

    // ── CPU 内存平面 ──────────────────────────────────────────────────────────
    struct PlaneInfo {
        void*    data   = nullptr;
        uint32_t stride = 0;
        uint32_t size   = 0;
    };

    // ── DMA-BUF 描述符（Linux 零拷贝路径） ──────────────────────────────────
    struct DmaBufInfo {
        static constexpr int MAX_PLANES = 4;
        int      fds[MAX_PLANES]     = {-1, -1, -1, -1}; // fd per plane（-1 = 共享 fds[0]）
        uint32_t offsets[MAX_PLANES] = {};                // byte offset in buffer
        uint32_t pitches[MAX_PLANES] = {};                // stride in bytes
        uint32_t drmFourcc           = 0;                 // DRM_FORMAT_NV12 etc.
        uint64_t modifiers[MAX_PLANES] = {};              // DRM format modifiers
        int      nbPlanes            = 0;
    };

    MemoryType  memoryType  = MemoryType::CPU_MEMORY;
    PixelFormat pixelFormat = PixelFormat::YUV420P;
    uint32_t width          = 0;
    uint32_t height         = 0;
    int64_t  pts            = 0;
    int64_t  captureTimestamp = 0; // 原始采集时间戳（用于 E2E 延迟计算）
    uint32_t cameraId       = 0;

    PlaneInfo   planes[3]; // Y, U, V 或 RGB（CPU_MEMORY 路径）
    DmaBufInfo  dmaBuf;    // DMA_BUF 路径的描述符

    union {
        uint32_t  glTextureId = 0;
        uintptr_t vaapiSurface;
        void*     d3d11Texture;
    } gpuHandle{};

    // 持有底层 AVFrame/surface 的生命周期引用
    // 确保 DMA-BUF fd 在 GL 纹理创建并使用完毕之前不被关闭
    std::shared_ptr<void> poolRef;

    void reset() {
        pts = 0;
        captureTimestamp = 0;
        pixelFormat = PixelFormat::YUV420P;
        for (auto& p : planes) { p.data = nullptr; p.stride = 0; p.size = 0; }
        for (int i = 0; i < DmaBufInfo::MAX_PLANES; ++i) {
            dmaBuf.fds[i]     = -1;
            dmaBuf.offsets[i] = 0;
            dmaBuf.pitches[i] = 0;
        }
        dmaBuf.drmFourcc = 0;
        dmaBuf.nbPlanes  = 0;
        poolRef.reset();
    }
};

/**
 * 解码器配置。
 */
struct DecoderConfig {
    int width  = 1920;
    int height = 1080;
    QString codec = "H264"; // H264 / H265 / AV1
};

/**
 * 解码器能力。
 */
struct DecoderCapabilities {
    bool supportsH264  = false;
    bool supportsH265  = false;
    bool supportsHW    = false;
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

    virtual DecodeResult submitPacket(const uint8_t* data, size_t size,
                                       int64_t pts, int64_t dts) = 0;
    virtual DecodeResult receiveFrame(VideoFrame& frame) = 0;
    virtual void flush() = 0;
    virtual DecoderCapabilities queryCapabilities() const = 0;
    virtual bool isHardwareAccelerated() const = 0;
};
