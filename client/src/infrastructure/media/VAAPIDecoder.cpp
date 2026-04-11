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
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}
#include <drm/drm_fourcc.h>
#include <fcntl.h>
#include <unistd.h>

#include <atomic>

namespace {
static AVPixelFormat vaapi_hw_decode_get_format(AVCodecContext* ctx, const AVPixelFormat* pix_fmts) {
  for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
    if (*p == AV_PIX_FMT_VAAPI)
      return AV_PIX_FMT_VAAPI;
  }
  qWarning() << "[Client][HW-E2E][VAAPI][get_format] no AV_PIX_FMT_VAAPI in offered list ctx="
             << (void*)ctx;
  return AV_PIX_FMT_NONE;
}
}  // namespace

struct VAAPIDecoder::Impl {
  int drmFd = -1;
  AVBufferRef* hwDeviceCtx = nullptr;
  const AVCodec* codec = nullptr;
  AVCodecContext* codecCtx = nullptr;
  AVPacket* packet = nullptr;
  AVFrame* hwFrame = nullptr;
  AVFrame* swFrame = nullptr;  // CPU 降级路径临时缓冲（transfer target）
  bool initialized = false;
  std::atomic<bool> drmPrimeReady{false};   // preferDmaBufExport 且运行时映射仍可用
  std::atomic<bool> preferDmaBufExport{true};
  int recvDiagCount = 0;
};

VAAPIDecoder::VAAPIDecoder() : m_impl(std::make_unique<Impl>()) {}
VAAPIDecoder::~VAAPIDecoder() { shutdown(); }

bool VAAPIDecoder::initialize(const DecoderConfig& config) {
  // 扫描可用 DRM 渲染节点
  static const char* kDrmNodes[] = {"/dev/dri/renderD128", "/dev/dri/renderD129",
                                    "/dev/dri/renderD130", nullptr};

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

  if (av_hwdevice_ctx_create(&m_impl->hwDeviceCtx, AV_HWDEVICE_TYPE_VAAPI, drmPath, nullptr, 0) <
      0) {
    qWarning() << "[Client][VAAPIDecoder] av_hwdevice_ctx_create failed"
               << "node=" << drmPath;
    return false;
  }

  const AVCodecID codecId =
      (config.codec == "H265" || config.codec == "HEVC") ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
  m_impl->codec = avcodec_find_decoder(codecId);
  if (!m_impl->codec) {
    qWarning() << "[Client][VAAPIDecoder] codec not found:" << config.codec;
    return false;
  }

  m_impl->codecCtx = avcodec_alloc_context3(m_impl->codec);
  if (!m_impl->codecCtx)
    return false;

  m_impl->preferDmaBufExport.store(config.preferDmaBufExport);
  m_impl->drmPrimeReady.store(config.preferDmaBufExport);

  m_impl->codecCtx->hw_device_ctx = av_buffer_ref(m_impl->hwDeviceCtx);
  m_impl->codecCtx->width = config.width;
  m_impl->codecCtx->height = config.height;
  m_impl->codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
  m_impl->codecCtx->thread_count = 1;  // 单线程降低延迟
  m_impl->codecCtx->get_format = vaapi_hw_decode_get_format;

  if (!config.codecExtradata.isEmpty()) {
    const int sz = config.codecExtradata.size();
    m_impl->codecCtx->extradata =
        static_cast<uint8_t*>(av_malloc(static_cast<size_t>(sz) + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!m_impl->codecCtx->extradata) {
      qCritical() << "[Client][HW-E2E][VAAPI][OPEN] extradata av_malloc failed sz=" << sz;
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

  const int openRet = avcodec_open2(m_impl->codecCtx, m_impl->codec, nullptr);
  if (openRet < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(openRet, errbuf, sizeof(errbuf));
    qWarning() << "[Client][HW-E2E][VAAPI][OPEN] avcodec_open2 failed ret=" << openRet
               << " err=" << errbuf << " codec=" << config.codec << " wxh=" << config.width << "x"
               << config.height << " extradata=" << (m_impl->codecCtx->extradata_size)
               << " preferDmaBuf=" << m_impl->preferDmaBufExport.load();
    avcodec_free_context(&m_impl->codecCtx);
    av_buffer_unref(&m_impl->hwDeviceCtx);
    m_impl->hwDeviceCtx = nullptr;
    return false;
  }

  m_impl->packet = av_packet_alloc();
  m_impl->hwFrame = av_frame_alloc();
  m_impl->swFrame = av_frame_alloc();
  m_impl->initialized = true;

  qInfo() << "[Client][HW-E2E][VAAPI][OPEN] ok codec=" << config.codec << " drm=" << drmPath
          << " wxh=" << config.width << "x" << config.height
          << " extradataSize=" << m_impl->codecCtx->extradata_size
          << " preferDmaBufExport=" << m_impl->preferDmaBufExport.load()
          << " drmPrimeWillTry=" << m_impl->drmPrimeReady.load();
  return true;
}

void VAAPIDecoder::shutdown() {
  if (!m_impl->initialized)
    return;
  av_frame_free(&m_impl->swFrame);
  av_frame_free(&m_impl->hwFrame);
  av_packet_free(&m_impl->packet);
  avcodec_free_context(&m_impl->codecCtx);
  av_buffer_unref(&m_impl->hwDeviceCtx);
  if (m_impl->drmFd >= 0) {
    close(m_impl->drmFd);
    m_impl->drmFd = -1;
  }
  m_impl->initialized = false;
  qInfo() << "[Client][VAAPIDecoder] shutdown";
}

DecodeResult VAAPIDecoder::submitPacket(const uint8_t* data, size_t size, int64_t pts,
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
    qWarning() << "[Client][HW-E2E][VAAPI][send_packet] fail ret=" << ret << " err=" << errbuf
               << " size=" << size << " pts=" << pts;
    return DecodeResult::Error;
  }
  return DecodeResult::Ok;
}

DecodeResult VAAPIDecoder::receiveFrame(VideoFrame& frame) {
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
    qWarning() << "[Client][HW-E2E][VAAPI][receive_frame] fail ret=" << ret << " err=" << errbuf;
    return DecodeResult::Error;
  }

  frame.width = static_cast<uint32_t>(m_impl->hwFrame->width);
  frame.height = static_cast<uint32_t>(m_impl->hwFrame->height);
  frame.pts = m_impl->hwFrame->pts;

  if (m_impl->recvDiagCount < 6) {
    ++m_impl->recvDiagCount;
    const char* nm = av_get_pix_fmt_name(static_cast<AVPixelFormat>(m_impl->hwFrame->format));
    qInfo() << "[Client][HW-E2E][VAAPI][RECV] frame hwPixFmt=" << (nm ? nm : "?") << "("
            << m_impl->hwFrame->format << ")"
            << " wxh=" << frame.width << "x" << frame.height << " pts=" << frame.pts;
  }

  // ── 零拷贝路径：尝试 DRM Prime 映射 ──────────────────────────────────────
  if (m_impl->preferDmaBufExport.load(std::memory_order_acquire) &&
      m_impl->drmPrimeReady.load(std::memory_order_acquire)) {
    AVFrame* drmFrame = av_frame_alloc();
    if (av_hwframe_map(drmFrame, m_impl->hwFrame, AV_HWFRAME_MAP_DIRECT | AV_HWFRAME_MAP_READ) ==
            0 &&
        drmFrame->format == AV_PIX_FMT_DRM_PRIME) {
      const AVDRMFrameDescriptor* desc =
          reinterpret_cast<const AVDRMFrameDescriptor*>(drmFrame->data[0]);

      if (desc && desc->nb_objects > 0 && desc->nb_layers > 0) {
        const AVDRMLayerDescriptor& layer = desc->layers[0];

        frame.memoryType = VideoFrame::MemoryType::DMA_BUF;
        frame.pixelFormat = VideoFrame::PixelFormat::NV12;  // VAAPI 通常输出 NV12

        frame.dmaBuf.drmFourcc = layer.format;
        frame.dmaBuf.nbPlanes = layer.nb_planes;

        for (int p = 0; p < layer.nb_planes && p < VideoFrame::DmaBufInfo::MAX_PLANES; ++p) {
          const int objIdx = layer.planes[p].object_index;
          frame.dmaBuf.fds[p] = desc->objects[objIdx].fd;
          frame.dmaBuf.offsets[p] = static_cast<uint32_t>(layer.planes[p].offset);
          frame.dmaBuf.pitches[p] = static_cast<uint32_t>(layer.planes[p].pitch);
          frame.dmaBuf.modifiers[p] = desc->objects[objIdx].format_modifier;
        }

        // poolRef 保持 drmFrame 生存，确保 fd 有效
        frame.poolRef = std::shared_ptr<void>(drmFrame, [](void* p) {
          AVFrame* f = static_cast<AVFrame*>(p);
          av_frame_free(&f);
        });

        qInfo() << "[Client][HW-E2E][VAAPI][DRM_PRIME] ok fourcc=0x" << Qt::hex
                << frame.dmaBuf.drmFourcc << Qt::dec << " nbPlanes=" << frame.dmaBuf.nbPlanes
                << " fd0=" << frame.dmaBuf.fds[0] << " wxh=" << frame.width << "x" << frame.height;

        frame.interlacedMetadata = (m_impl->hwFrame->flags & (1 << 5)) != 0;
        frame.topFieldFirst = (m_impl->hwFrame->flags & (1 << 6)) != 0;

        return DecodeResult::Ok;
      }
    }
    // DRM Prime 映射失败，降级到 CPU 路径（并禁止后续尝试）
    av_frame_free(&drmFrame);
    m_impl->drmPrimeReady.store(false, std::memory_order_release);
    qWarning() << "[Client][HW-E2E][VAAPI][DRM_PRIME] map failed → CPU NV12 transfer path"
               << " (后续不再尝试 DRM Prime)";
  }

  // ── 降级路径：CPU 内存 NV12（独立 AVFrame clone，避免指向 swFrame 内部缓冲被下帧覆盖）──
  m_impl->swFrame->format = AV_PIX_FMT_NV12;
  if (av_hwframe_transfer_data(m_impl->swFrame, m_impl->hwFrame, 0) < 0) {
    qWarning() << "[Client][HW-E2E][VAAPI][CPU] av_hwframe_transfer_data failed";
    return DecodeResult::Error;
  }
  av_frame_copy_props(m_impl->swFrame, m_impl->hwFrame);

  AVFrame* cpuOwned = av_frame_clone(m_impl->swFrame);
  if (!cpuOwned) {
    qWarning() << "[Client][HW-E2E][VAAPI][CPU] av_frame_clone failed";
    return DecodeResult::Error;
  }

  frame.memoryType = VideoFrame::MemoryType::CPU_MEMORY;
  frame.pixelFormat = VideoFrame::PixelFormat::NV12;
  frame.pts = cpuOwned->pts;
  frame.planes[0].data = cpuOwned->data[0];
  frame.planes[0].stride = static_cast<uint32_t>(cpuOwned->linesize[0]);
  frame.planes[1].data = cpuOwned->data[1];
  frame.planes[1].stride = static_cast<uint32_t>(cpuOwned->linesize[1]);
  frame.interlacedMetadata = (cpuOwned->flags & (1 << 5)) != 0;
  frame.topFieldFirst = (cpuOwned->flags & (1 << 6)) != 0;
  frame.poolRef = std::shared_ptr<void>(cpuOwned, [](void* p) {
    AVFrame* f = static_cast<AVFrame*>(p);
    av_frame_free(&f);
  });
  return DecodeResult::Ok;
}

void VAAPIDecoder::flush() {
  if (m_impl->codecCtx)
    avcodec_flush_buffers(m_impl->codecCtx);
}

DecoderCapabilities VAAPIDecoder::queryCapabilities() const {
  return {true, true, true, "VAAPIDecoder(DRM-Prime)"};
}

void VAAPIDecoder::setDmaBufExportPreferred(bool enable) {
  m_impl->preferDmaBufExport.store(enable, std::memory_order_release);
  if (!enable)
    m_impl->drmPrimeReady.store(false, std::memory_order_release);
  qInfo() << "[Client][HW-E2E][VAAPI][EXPORT] setDmaBufExportPreferred enable=" << enable
          << " (SceneGraph 回退或运维切换)";
}

#endif  // ENABLE_VAAPI
