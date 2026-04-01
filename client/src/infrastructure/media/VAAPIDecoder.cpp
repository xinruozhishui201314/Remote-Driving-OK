#include "VAAPIDecoder.h"
#include <QDebug>

#ifdef ENABLE_VAAPI
/**
 * VA-API 硬件解码器 — DRM Prime 零拷贝路径。
 *
 * 零拷贝工作流（完全避免 GPU→CPU 内存传输）：
 *   1) avcodec_receive_frame() → hwFrame（格式 AV_PIX_FMT_VAAPI，GPU 内存）
 *   2) av_hwframe_map(drmFrame, hwFrame, DIRECT) → drmFrame（格式 AV_PIX_FMT_DRM_PRIME）
 *   3) 从 AVDRMFrameDescriptor 提取 DMA-BUF fd + NV12 plane 信息
 *   4) VideoFrame.memoryType = DMA_BUF，交由 EGLDmaBufInterop 在渲染线程创建 GL 纹理
 *   5) poolRef 持有 drmFrame 生命周期，确保 DMA-BUF fd 在纹理释放前有效
 *
 * 降级路径（无 DRM Prime 支持时）：
 *   av_hwframe_transfer_data() → NV12 CPU 内存 → CpuUploadInterop
 *
 * 参考：https://trac.ffmpeg.org/wiki/Hardware/VAAPI
 */

#include <va/va.h>
#include <va/va_drm.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/hwcontext_drm.h>
}
#include <drm/drm_fourcc.h>
#include <fcntl.h>
#include <unistd.h>

struct VAAPIDecoder::Impl {
    int drmFd = -1;
    AVBufferRef* hwDeviceCtx = nullptr;
    const AVCodec* codec     = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVPacket* packet   = nullptr;
    AVFrame*  hwFrame  = nullptr;
    AVFrame*  swFrame  = nullptr;  // CPU 降级路径
    bool initialized   = false;
    bool drmPrimeReady = false;    // 运行时探测 DRM Prime 是否可用
};

VAAPIDecoder::VAAPIDecoder() : m_impl(std::make_unique<Impl>()) {}
VAAPIDecoder::~VAAPIDecoder() { shutdown(); }

bool VAAPIDecoder::initialize(const DecoderConfig& config)
{
    // 扫描可用 DRM 渲染节点
    static const char* kDrmNodes[] = {
        "/dev/dri/renderD128",
        "/dev/dri/renderD129",
        "/dev/dri/renderD130",
        nullptr
    };

    const char* drmPath = nullptr;
    for (int i = 0; kDrmNodes[i]; ++i) {
        int fd = open(kDrmNodes[i], O_RDWR);
        if (fd >= 0) {
            close(fd);
            drmPath = kDrmNodes[i];
            break;
        }
    }

    if (!drmPath) {
        qWarning() << "[Client][VAAPIDecoder] no accessible DRM render node";
        return false;
    }

    if (av_hwdevice_ctx_create(&m_impl->hwDeviceCtx, AV_HWDEVICE_TYPE_VAAPI,
                                drmPath, nullptr, 0) < 0) {
        qWarning() << "[Client][VAAPIDecoder] av_hwdevice_ctx_create failed"
                   << "node=" << drmPath;
        return false;
    }

    const AVCodecID codecId = (config.codec == "H265" || config.codec == "HEVC")
                                  ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
    m_impl->codec = avcodec_find_decoder(codecId);
    if (!m_impl->codec) {
        qWarning() << "[Client][VAAPIDecoder] codec not found:" << config.codec;
        return false;
    }

    m_impl->codecCtx = avcodec_alloc_context3(m_impl->codec);
    if (!m_impl->codecCtx) return false;

    m_impl->codecCtx->hw_device_ctx = av_buffer_ref(m_impl->hwDeviceCtx);
    m_impl->codecCtx->width  = config.width;
    m_impl->codecCtx->height = config.height;
    m_impl->codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_impl->codecCtx->thread_count = 1; // 单线程降低延迟

    if (avcodec_open2(m_impl->codecCtx, m_impl->codec, nullptr) < 0) {
        qWarning() << "[Client][VAAPIDecoder] avcodec_open2 failed";
        return false;
    }

    m_impl->packet  = av_packet_alloc();
    m_impl->hwFrame = av_frame_alloc();
    m_impl->swFrame = av_frame_alloc();
    m_impl->initialized = true;

    // 探测 DRM Prime 映射支持（后续 receiveFrame 首次时确认）
    m_impl->drmPrimeReady = true;

    qInfo() << "[Client][VAAPIDecoder] initialized codec=" << config.codec
            << "drm=" << drmPath;
    return true;
}

void VAAPIDecoder::shutdown()
{
    if (!m_impl->initialized) return;
    av_frame_free(&m_impl->swFrame);
    av_frame_free(&m_impl->hwFrame);
    av_packet_free(&m_impl->packet);
    avcodec_free_context(&m_impl->codecCtx);
    av_buffer_unref(&m_impl->hwDeviceCtx);
    if (m_impl->drmFd >= 0) { close(m_impl->drmFd); m_impl->drmFd = -1; }
    m_impl->initialized = false;
    qInfo() << "[Client][VAAPIDecoder] shutdown";
}

DecodeResult VAAPIDecoder::submitPacket(const uint8_t* data, size_t size,
                                         int64_t pts, int64_t /*dts*/)
{
    if (!m_impl->initialized) return DecodeResult::Error;
    m_impl->packet->data = const_cast<uint8_t*>(data);
    m_impl->packet->size = static_cast<int>(size);
    m_impl->packet->pts  = pts;
    int ret = avcodec_send_packet(m_impl->codecCtx, m_impl->packet);
    if (ret == AVERROR(EAGAIN)) return DecodeResult::NeedMore;
    if (ret < 0) return DecodeResult::Error;
    return DecodeResult::Ok;
}

DecodeResult VAAPIDecoder::receiveFrame(VideoFrame& frame)
{
    if (!m_impl->initialized) return DecodeResult::Error;

    int ret = avcodec_receive_frame(m_impl->codecCtx, m_impl->hwFrame);
    if (ret == AVERROR(EAGAIN)) return DecodeResult::NeedMore;
    if (ret == AVERROR_EOF)     return DecodeResult::EOF_Stream;
    if (ret < 0)                return DecodeResult::Error;

    frame.width  = static_cast<uint32_t>(m_impl->hwFrame->width);
    frame.height = static_cast<uint32_t>(m_impl->hwFrame->height);
    frame.pts    = m_impl->hwFrame->pts;

    // ── 零拷贝路径：尝试 DRM Prime 映射 ──────────────────────────────────────
    if (m_impl->drmPrimeReady) {
        AVFrame* drmFrame = av_frame_alloc();
        if (av_hwframe_map(drmFrame, m_impl->hwFrame,
                           AV_HWFRAME_MAP_DIRECT | AV_HWFRAME_MAP_READ) == 0
            && drmFrame->format == AV_PIX_FMT_DRM_PRIME)
        {
            const AVDRMFrameDescriptor* desc =
                reinterpret_cast<const AVDRMFrameDescriptor*>(drmFrame->data[0]);

            if (desc && desc->nb_objects > 0 && desc->nb_layers > 0) {
                const AVDRMLayerDescriptor& layer = desc->layers[0];

                frame.memoryType  = VideoFrame::MemoryType::DMA_BUF;
                frame.pixelFormat = VideoFrame::PixelFormat::NV12; // VAAPI 通常输出 NV12

                frame.dmaBuf.drmFourcc = layer.format;
                frame.dmaBuf.nbPlanes  = layer.nb_planes;

                for (int p = 0; p < layer.nb_planes && p < VideoFrame::DmaBufInfo::MAX_PLANES; ++p) {
                    const int objIdx = layer.planes[p].object_index;
                    frame.dmaBuf.fds[p]     = desc->objects[objIdx].fd;
                    frame.dmaBuf.offsets[p] = static_cast<uint32_t>(layer.planes[p].offset);
                    frame.dmaBuf.pitches[p] = static_cast<uint32_t>(layer.planes[p].pitch);
                    frame.dmaBuf.modifiers[p] = desc->objects[objIdx].format_modifier;
                }

                // poolRef 保持 drmFrame 生存，确保 fd 有效
                frame.poolRef = std::shared_ptr<void>(
                    drmFrame,
                    [](void* p) {
                        AVFrame* f = static_cast<AVFrame*>(p);
                        av_frame_free(&f);
                    });

                return DecodeResult::Ok;
            }
        }
        // DRM Prime 映射失败，降级到 CPU 路径（并禁止后续尝试）
        av_frame_free(&drmFrame);
        m_impl->drmPrimeReady = false;
        qWarning() << "[Client][VAAPIDecoder] DRM Prime mapping failed, falling back to CPU";
    }

    // ── 降级路径：CPU 内存 NV12 ────────────────────────────────────────────
    m_impl->swFrame->format = AV_PIX_FMT_NV12;
    if (av_hwframe_transfer_data(m_impl->swFrame, m_impl->hwFrame, 0) < 0) {
        qWarning() << "[Client][VAAPIDecoder] av_hwframe_transfer_data failed";
        return DecodeResult::Error;
    }
    av_frame_copy_props(m_impl->swFrame, m_impl->hwFrame);

    frame.memoryType  = VideoFrame::MemoryType::CPU_MEMORY;
    frame.pixelFormat = VideoFrame::PixelFormat::NV12;
    frame.pts    = m_impl->swFrame->pts;
    frame.planes[0].data   = m_impl->swFrame->data[0];
    frame.planes[0].stride = static_cast<uint32_t>(m_impl->swFrame->linesize[0]);
    frame.planes[1].data   = m_impl->swFrame->data[1];
    frame.planes[1].stride = static_cast<uint32_t>(m_impl->swFrame->linesize[1]);
    return DecodeResult::Ok;
}

void VAAPIDecoder::flush()
{
    if (m_impl->codecCtx) avcodec_flush_buffers(m_impl->codecCtx);
}

DecoderCapabilities VAAPIDecoder::queryCapabilities() const
{
    return {true, true, true, "VAAPIDecoder(DRM-Prime)"};
}

#endif // ENABLE_VAAPI
