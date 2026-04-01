#include "NvdecDecoder.h"
#include <QDebug>

#ifdef ENABLE_NVDEC
/**
 * NVDEC 通过 FFmpeg CUDA hwaccel 实现。
 * 不依赖 CUDA SDK（仅需 FFmpeg 以 --enable-cuda-nvcc 编译）。
 * 解码在 NVDEC 单元完成，帧在 CUDA 内存中，通过 av_hwframe_transfer_data
 * 转为 CPU NV12，再由渲染层异步上传。
 */

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
}

struct NvdecDecoder::Impl {
    AVBufferRef*    hwDeviceCtx = nullptr;
    const AVCodec*  codec       = nullptr;
    AVCodecContext* codecCtx    = nullptr;
    AVPacket*       packet      = nullptr;
    AVFrame*        hwFrame     = nullptr;
    AVFrame*        swFrame     = nullptr;
    bool initialized = false;
};

NvdecDecoder::NvdecDecoder() : m_impl(std::make_unique<Impl>()) {}
NvdecDecoder::~NvdecDecoder() { shutdown(); }

bool NvdecDecoder::initialize(const DecoderConfig& config)
{
    // 创建 CUDA 设备（device=0 默认第一块 NVIDIA GPU）
    if (av_hwdevice_ctx_create(&m_impl->hwDeviceCtx, AV_HWDEVICE_TYPE_CUDA,
                                nullptr, nullptr, 0) < 0) {
        qWarning() << "[Client][NvdecDecoder] CUDA device creation failed"
                   << "(NVIDIA driver or FFmpeg CUDA support missing)";
        return false;
    }

    // 验证是否为 CUDA 设备（NVDEC 挂载在 CUDA 上下文）
    AVHWDeviceContext* devCtx =
        reinterpret_cast<AVHWDeviceContext*>(m_impl->hwDeviceCtx->data);
    if (devCtx->type != AV_HWDEVICE_TYPE_CUDA) {
        qWarning() << "[Client][NvdecDecoder] unexpected hw device type";
        av_buffer_unref(&m_impl->hwDeviceCtx);
        return false;
    }

    const AVCodecID codecId = (config.codec == "H265" || config.codec == "HEVC")
                                  ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;

    // 优先使用 CUVID（NVDEC）解码器
    const char* cuviName = (codecId == AV_CODEC_ID_HEVC) ? "hevc_cuvid" : "h264_cuvid";
    m_impl->codec = avcodec_find_decoder_by_name(cuviName);
    if (!m_impl->codec) {
        // 回退到通用解码器配合 CUDA hwdevice
        m_impl->codec = avcodec_find_decoder(codecId);
    }
    if (!m_impl->codec) {
        qWarning() << "[Client][NvdecDecoder] no suitable decoder for" << config.codec;
        return false;
    }

    m_impl->codecCtx = avcodec_alloc_context3(m_impl->codec);
    if (!m_impl->codecCtx) return false;

    m_impl->codecCtx->hw_device_ctx = av_buffer_ref(m_impl->hwDeviceCtx);
    m_impl->codecCtx->width         = config.width;
    m_impl->codecCtx->height        = config.height;
    m_impl->codecCtx->flags        |= AV_CODEC_FLAG_LOW_DELAY;
    m_impl->codecCtx->thread_count  = 1;

    if (avcodec_open2(m_impl->codecCtx, m_impl->codec, nullptr) < 0) {
        qWarning() << "[Client][NvdecDecoder] avcodec_open2 failed";
        return false;
    }

    m_impl->packet  = av_packet_alloc();
    m_impl->hwFrame = av_frame_alloc();
    m_impl->swFrame = av_frame_alloc();
    m_impl->initialized = true;

    qInfo() << "[Client][NvdecDecoder] initialized decoder=" << m_impl->codec->name
            << "codec=" << config.codec;
    return true;
}

void NvdecDecoder::shutdown()
{
    if (!m_impl->initialized) return;
    av_frame_free(&m_impl->swFrame);
    av_frame_free(&m_impl->hwFrame);
    av_packet_free(&m_impl->packet);
    avcodec_free_context(&m_impl->codecCtx);
    av_buffer_unref(&m_impl->hwDeviceCtx);
    m_impl->initialized = false;
    qInfo() << "[Client][NvdecDecoder] shutdown";
}

DecodeResult NvdecDecoder::submitPacket(const uint8_t* data, size_t size,
                                         int64_t pts, int64_t /*dts*/)
{
    if (!m_impl->initialized) return DecodeResult::Error;
    m_impl->packet->data = const_cast<uint8_t*>(data);
    m_impl->packet->size = static_cast<int>(size);
    m_impl->packet->pts  = pts;
    int ret = avcodec_send_packet(m_impl->codecCtx, m_impl->packet);
    if (ret == AVERROR(EAGAIN)) return DecodeResult::NeedMore;
    if (ret < 0)                return DecodeResult::Error;
    return DecodeResult::Ok;
}

DecodeResult NvdecDecoder::receiveFrame(VideoFrame& frame)
{
    if (!m_impl->initialized) return DecodeResult::Error;

    int ret = avcodec_receive_frame(m_impl->codecCtx, m_impl->hwFrame);
    if (ret == AVERROR(EAGAIN)) return DecodeResult::NeedMore;
    if (ret == AVERROR_EOF)     return DecodeResult::EOF_Stream;
    if (ret < 0)                return DecodeResult::Error;

    // CUDA → CPU NV12（NVDEC 的 CUDA 内存 → 系统内存）
    m_impl->swFrame->format = AV_PIX_FMT_NV12;
    if (av_hwframe_transfer_data(m_impl->swFrame, m_impl->hwFrame, 0) < 0) {
        qWarning() << "[Client][NvdecDecoder] CUDA→CPU transfer failed";
        return DecodeResult::Error;
    }
    av_frame_copy_props(m_impl->swFrame, m_impl->hwFrame);

    frame.memoryType  = VideoFrame::MemoryType::CPU_MEMORY;
    frame.pixelFormat = VideoFrame::PixelFormat::NV12;
    frame.width  = static_cast<uint32_t>(m_impl->swFrame->width);
    frame.height = static_cast<uint32_t>(m_impl->swFrame->height);
    frame.pts    = m_impl->swFrame->pts;

    frame.planes[0].data   = m_impl->swFrame->data[0];
    frame.planes[0].stride = static_cast<uint32_t>(m_impl->swFrame->linesize[0]);
    frame.planes[1].data   = m_impl->swFrame->data[1]; // UV 交织
    frame.planes[1].stride = static_cast<uint32_t>(m_impl->swFrame->linesize[1]);
    frame.planes[2].data   = nullptr; // NV12：无独立 V 平面

    return DecodeResult::Ok;
}

void NvdecDecoder::flush()
{
    if (m_impl->codecCtx) avcodec_flush_buffers(m_impl->codecCtx);
}

DecoderCapabilities NvdecDecoder::queryCapabilities() const
{
    return {true, true, true, QString("NvdecDecoder(%1)").arg(
        m_impl->codec ? m_impl->codec->name : "uninitialized")};
}

#endif // ENABLE_NVDEC
