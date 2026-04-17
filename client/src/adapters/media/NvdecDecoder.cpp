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
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

namespace {
static AVPixelFormat nvdec_hw_decode_get_format(AVCodecContext* ctx, const AVPixelFormat* pix_fmts) {
  for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
    if (*p == AV_PIX_FMT_CUDA)
      return AV_PIX_FMT_CUDA;
  }
  if (pix_fmts[0] != AV_PIX_FMT_NONE) {
    qWarning() << "[Client][HW-E2E][NVDEC][get_format] CUDA not listed; using first offered fmt="
               << pix_fmts[0] << " ctx=" << (void*)ctx;
    return pix_fmts[0];
  }
  qWarning() << "[Client][HW-E2E][NVDEC][get_format] empty pixel format list ctx=" << (void*)ctx;
  return AV_PIX_FMT_NONE;
}
}  // namespace

struct NvdecDecoder::Impl {
  AVBufferRef* hwDeviceCtx = nullptr;
  const AVCodec* codec = nullptr;
  AVCodecContext* codecCtx = nullptr;
  AVPacket* packet = nullptr;
  AVFrame* hwFrame = nullptr;
  AVFrame* swFrame = nullptr;
  bool initialized = false;
  int recvDiagCount = 0;
};

NvdecDecoder::NvdecDecoder() : m_impl(std::make_unique<Impl>()) {}
NvdecDecoder::~NvdecDecoder() { shutdown(); }

bool NvdecDecoder::initialize(const DecoderConfig& config) {
  // 创建 CUDA 设备（device=0 默认第一块 NVIDIA GPU）
  if (av_hwdevice_ctx_create(&m_impl->hwDeviceCtx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0) <
      0) {
    qWarning() << "[Client][NvdecDecoder] CUDA device creation failed"
               << "(NVIDIA driver or FFmpeg CUDA support missing)";
    return false;
  }

  // 验证是否为 CUDA 设备（NVDEC 挂载在 CUDA 上下文）
  AVHWDeviceContext* devCtx = reinterpret_cast<AVHWDeviceContext*>(m_impl->hwDeviceCtx->data);
  if (devCtx->type != AV_HWDEVICE_TYPE_CUDA) {
    qWarning() << "[Client][NvdecDecoder] unexpected hw device type";
    av_buffer_unref(&m_impl->hwDeviceCtx);
    return false;
  }

  const AVCodecID codecId =
      (config.codec == "H265" || config.codec == "HEVC") ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;

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
  if (!m_impl->codecCtx)
    return false;

  m_impl->codecCtx->hw_device_ctx = av_buffer_ref(m_impl->hwDeviceCtx);
  m_impl->codecCtx->width = config.width;
  m_impl->codecCtx->height = config.height;
  m_impl->codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
  m_impl->codecCtx->thread_count = 1;
  m_impl->codecCtx->get_format = nvdec_hw_decode_get_format;

  if (!config.codecExtradata.isEmpty()) {
    const int sz = config.codecExtradata.size();
    m_impl->codecCtx->extradata =
        static_cast<uint8_t*>(av_malloc(static_cast<size_t>(sz) + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!m_impl->codecCtx->extradata) {
      qCritical() << "[Client][HW-E2E][NVDEC][OPEN] extradata av_malloc failed sz=" << sz;
      avcodec_free_context(&m_impl->codecCtx);
      av_buffer_unref(&m_impl->hwDeviceCtx);
      m_impl->hwDeviceCtx = nullptr;
      return false;
    }
    memcpy(m_impl->codecCtx->extradata, config.codecExtradata.constData(),
           static_cast<size_t>(sz));
    memset(m_impl->codecCtx->extradata + sz, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    m_impl->codecCtx->extradata_size = sz;
  }

  AVDictionary* opts = nullptr;
  // 针对 NVIDIA CUVID 解码器的低延迟调优
  av_dict_set(&opts, "low_delay", "1", 0);
  av_dict_set(&opts, "delay", "0", 0);
  av_dict_set(&opts, "threads", "1", 0);

  const int openRet = avcodec_open2(m_impl->codecCtx, m_impl->codec, &opts);
  av_dict_free(&opts);

  if (openRet < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(openRet, errbuf, sizeof(errbuf));
    qWarning() << "[Client][HW-E2E][NVDEC][OPEN] avcodec_open2 failed ret=" << openRet
               << " err=" << errbuf << " decoder=" << (m_impl->codec ? m_impl->codec->name : "?")
               << " extradataSize=" << m_impl->codecCtx->extradata_size;
    avcodec_free_context(&m_impl->codecCtx);
    av_buffer_unref(&m_impl->hwDeviceCtx);
    m_impl->hwDeviceCtx = nullptr;
    return false;
  }

  m_impl->packet = av_packet_alloc();
  m_impl->hwFrame = av_frame_alloc();
  m_impl->swFrame = av_frame_alloc();
  m_impl->initialized = true;

  qInfo() << "[Client][HW-E2E][NVDEC][OPEN] ok decoder=" << m_impl->codec->name
          << " codec=" << config.codec << " wxh=" << config.width << "x" << config.height
          << " extradataSize=" << m_impl->codecCtx->extradata_size;
  return true;
}

void NvdecDecoder::shutdown() {
  if (!m_impl->initialized)
    return;
  av_frame_free(&m_impl->swFrame);
  av_frame_free(&m_impl->hwFrame);
  av_packet_free(&m_impl->packet);
  avcodec_free_context(&m_impl->codecCtx);
  av_buffer_unref(&m_impl->hwDeviceCtx);
  m_impl->initialized = false;
  qInfo() << "[Client][NvdecDecoder] shutdown";
}

bool NvdecDecoder::reconfigure(const DecoderConfig& config) {
  if (!m_impl->initialized || !m_impl->codecCtx)
    return false;

  if (m_impl->codecCtx->width == config.width && m_impl->codecCtx->height == config.height) {
    return true;
  }

  qInfo() << "[Client][HW-E2E][NVDEC][RECONF] dynamic resolution switch:"
          << m_impl->codecCtx->width << "x" << m_impl->codecCtx->height << " -> " << config.width
          << "x" << config.height;

  avcodec_flush_buffers(m_impl->codecCtx);
  m_impl->codecCtx->width = config.width;
  m_impl->codecCtx->height = config.height;

  return true;
}

DecodeResult NvdecDecoder::submitPacket(const uint8_t* data, size_t size, int64_t pts,
                                        int64_t /*dts*/) {
  if (!m_impl->initialized)
    return DecodeResult::Error;
  m_impl->packet->data = const_cast<uint8_t*>(data);
  m_impl->packet->size = static_cast<int>(size);
  m_impl->packet->pts = pts;
  int ret = avcodec_send_packet(m_impl->codecCtx, m_impl->packet);
  if (ret == AVERROR(EAGAIN))
    return DecodeResult::NeedMore;
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    qWarning() << "[Client][HW-E2E][NVDEC][send_packet] fail ret=" << ret << " err=" << errbuf
               << " size=" << size << " pts=" << pts;
    return DecodeResult::Error;
  }
  return DecodeResult::Ok;
}

DecodeResult NvdecDecoder::receiveFrame(VideoFrame& frame) {
  if (!m_impl->initialized)
    return DecodeResult::Error;

  int ret = avcodec_receive_frame(m_impl->codecCtx, m_impl->hwFrame);
  if (ret == AVERROR(EAGAIN))
    return DecodeResult::NeedMore;
  if (ret == AVERROR_EOF)
    return DecodeResult::EOF_Stream;
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    qWarning() << "[Client][HW-E2E][NVDEC][receive_frame] fail ret=" << ret << " err=" << errbuf;
    return DecodeResult::Error;
  }

  if (m_impl->recvDiagCount < 6) {
    ++m_impl->recvDiagCount;
    const char* nm = av_get_pix_fmt_name(static_cast<AVPixelFormat>(m_impl->hwFrame->format));
    qInfo() << "[Client][HW-E2E][NVDEC][RECV] hwPixFmt=" << (nm ? nm : "?") << "("
            << m_impl->hwFrame->format << ")"
            << " wxh=" << m_impl->hwFrame->width << "x" << m_impl->hwFrame->height;
  }

  // CUDA → CPU NV12（NVDEC 的 CUDA 内存 → 系统内存）
  m_impl->swFrame->format = AV_PIX_FMT_NV12;
  if (av_hwframe_transfer_data(m_impl->swFrame, m_impl->hwFrame, 0) < 0) {
    qWarning() << "[Client][HW-E2E][NVDEC][CPU] CUDA→CPU transfer failed";
    return DecodeResult::Error;
  }
  av_frame_copy_props(m_impl->swFrame, m_impl->hwFrame);

  AVFrame* cpuOwned = av_frame_clone(m_impl->swFrame);
  if (!cpuOwned) {
    qWarning() << "[Client][HW-E2E][NVDEC][CPU] av_frame_clone failed";
    return DecodeResult::Error;
  }

  frame.memoryType = VideoFrame::MemoryType::CPU_MEMORY;
  frame.pixelFormat = VideoFrame::PixelFormat::NV12;
  frame.width = static_cast<uint32_t>(cpuOwned->width);
  frame.height = static_cast<uint32_t>(cpuOwned->height);
  frame.pts = cpuOwned->pts;

  frame.planes[0].data = cpuOwned->data[0];
  frame.planes[0].stride = static_cast<uint32_t>(cpuOwned->linesize[0]);
  frame.planes[1].data = cpuOwned->data[1];
  frame.planes[1].stride = static_cast<uint32_t>(cpuOwned->linesize[1]);
  frame.planes[2].data = nullptr;
  frame.interlacedMetadata = (cpuOwned->flags & (1 << 5)) != 0;
  frame.topFieldFirst = (cpuOwned->flags & (1 << 6)) != 0;

  frame.poolRef = std::shared_ptr<void>(cpuOwned, [](void* p) {
    AVFrame* f = static_cast<AVFrame*>(p);
    av_frame_free(&f);
  });

  return DecodeResult::Ok;
}

void NvdecDecoder::flush() {
  if (m_impl->codecCtx)
    avcodec_flush_buffers(m_impl->codecCtx);
}

DecoderCapabilities NvdecDecoder::queryCapabilities() const {
  return {true, true, true,
          QString("NvdecDecoder(%1)").arg(m_impl->codec ? m_impl->codec->name : "uninitialized")};
}

#endif  // ENABLE_NVDEC
