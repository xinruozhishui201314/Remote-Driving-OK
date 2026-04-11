#include "H264WebRtcHwBridge.h"

#include "ClientVideoStreamHealth.h"
#include "VideoFrameEvidenceDiag.h"
#include "VideoInterlacedPolicy.h"
#include "VideoSwsColorHelper.h"
#include "core/configuration.h"
#include "h264decoder.h"
#include "infrastructure/media/IHardwareDecoder.h"

#if defined(ENABLE_FFMPEG)
#include "infrastructure/media/DecoderFactory.h"
#endif

#include <QDebug>
#include <QImage>

#include <atomic>

#if defined(ENABLE_FFMPEG)
extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#endif

const char* H264WebRtcHwBridge::kEnvVarName = "CLIENT_WEBRTC_HW_DECODE";

bool H264WebRtcHwBridge::hardwareDecodeRequested() {
  if (!Configuration::instance().enableHardwareDecode())
    return false;
  // 兼容：历史上用 CLIENT_WEBRTC_HW_DECODE 开关；仅「显式关闭」仍生效（空=不覆盖配置）
  const QByteArray legacy = qgetenv(kEnvVarName);
  if (!legacy.isEmpty()) {
    const QByteArray t = legacy.trimmed().toLower();
    if (t == "0" || t == "off" || t == "false" || t == "no")
      return false;
  }
  return true;
}

H264WebRtcHwBridge::H264WebRtcHwBridge(QString streamTag) : m_streamTag(std::move(streamTag)) {}

H264WebRtcHwBridge::~H264WebRtcHwBridge() {
  shutdown();
}

#if !defined(ENABLE_FFMPEG)

void H264WebRtcHwBridge::shutdown() {
  m_dec.reset();
  m_backendLabel.clear();
  m_preferDmaBufExport = false;
  m_dmabufSgEnvWanted = false;
  m_dmaBufSgBroken.store(false, std::memory_order_release);
}

void H264WebRtcHwBridge::flush() {}

bool H264WebRtcHwBridge::tryOpen(const QByteArray&, int, int) {
  qWarning() << "[Client][HW-E2E][" << m_streamTag << "][OPEN] FFmpeg disabled at build";
  return false;
}

bool H264WebRtcHwBridge::submitCompleteAnnexB(H264Decoder*, const uint8_t*, size_t, int64_t) {
  return false;
}

bool H264WebRtcHwBridge::drainAllOutput(H264Decoder*) {
  return false;
}

bool H264WebRtcHwBridge::convertCpuNv12ToRgbaAndIngest(H264Decoder*, VideoFrame&) {
  return false;
}

void H264WebRtcHwBridge::disableDmaBufExportForCpuFallback() {}

#else

namespace {
bool webRtcDmaBufSceneGraphEnabled() {
#if defined(CLIENT_HAVE_NV12_DMABUF_SG) && defined(ENABLE_EGL_DMABUF)
  const QByteArray v = qgetenv("CLIENT_WEBRTC_DMABUF_SG");
  if (v.isEmpty())
    return true;
  const QByteArray t = v.trimmed().toLower();
  if (t == "0" || t == "off" || t == "false" || t == "no")
    return false;
  return true;
#else
  return false;
#endif
}
}  // namespace

void H264WebRtcHwBridge::shutdown() {
  if (m_swsNv12ToRgba) {
    sws_freeContext(m_swsNv12ToRgba);
    m_swsNv12ToRgba = nullptr;
    m_swsW = m_swsH = 0;
  }
  if (m_dec) {
    m_dec->shutdown();
    m_dec.reset();
  }
  m_backendLabel.clear();
  m_preferDmaBufExport = false;
  m_dmabufSgEnvWanted = false;
  m_dmaBufSgBroken.store(false, std::memory_order_release);
}

void H264WebRtcHwBridge::flush() {
  if (m_dec)
    m_dec->flush();
}

bool H264WebRtcHwBridge::isHardwareAccelerated() const {
  return m_dec && m_dec->isHardwareAccelerated();
}

bool H264WebRtcHwBridge::tryOpen(const QByteArray& avccExtradata, int codedW, int codedH) {
  shutdown();
  if (avccExtradata.isEmpty()) {
    qWarning() << "[Client][HW-E2E][" << m_streamTag << "][OPEN] empty AVCC extradata";
    return false;
  }

#if !defined(ENABLE_VAAPI) && !defined(ENABLE_NVDEC)
  qWarning() << "[Client][HW-E2E][" << m_streamTag
             << "][OPEN] no ENABLE_VAAPI / ENABLE_NVDEC at build — cannot hardware-decode";
  return false;
#else
  DecoderConfig cfg;
  cfg.codec = QStringLiteral("H264");
  cfg.width = codedW > 0 ? codedW : 1920;
  cfg.height = codedH > 0 ? codedH : 1080;
  cfg.codecExtradata = avccExtradata;
  m_preferDmaBufExport = ClientVideoStreamHealth::effectivePreferDmaBufExport();
  cfg.preferDmaBufExport = m_preferDmaBufExport;
  m_dmabufSgEnvWanted = webRtcDmaBufSceneGraphEnabled();

  m_dec = DecoderFactory::create(cfg, DecoderPreference::HardwareFirst);
  if (!m_dec) {
    qWarning() << "[Client][HW-E2E][" << m_streamTag << "][OPEN] DecoderFactory returned null";
    return false;
  }
  
  // 硬解可用性检查 & 降级决策
  if (!m_dec->isHardwareAccelerated()) {
    // 硬解不可用（VA-API/NVDEC 探测失败或未编译）
    
    // 【修复1：短期】检查环境变量强制硬解的配置是否合理
    const bool requireHw = Configuration::instance().requireHardwareDecode();
    const bool hwCompiled = qEnvironmentVariableIsSet("ENABLE_VAAPI") || 
                           qEnvironmentVariableIsSet("ENABLE_NVDEC");
    
    // 【修复2：中期】记录诊断信息供运维参考
    if (requireHw && !hwCompiled) {
      qCritical() << "[Client][HW-E2E][" << m_streamTag
                  << "][OPEN] DIAGNOSTIC: requireHardwareDecode=true BUT hardware decode not compiled"
                  << " (ENABLE_VAAPI/ENABLE_NVDEC not set at build time)";
    } else if (requireHw && hwCompiled) {
      qCritical() << "[Client][HW-E2E][" << m_streamTag
                  << "][OPEN] DIAGNOSTIC: requireHardwareDecode=true BUT hardware decoder probe/init failed"
                  << " (check /dev/dri/* permissions, VA-API drivers, GPU availability)"
                  << " ★ Consider setting CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0 for this environment";
    }
    
    // 【修复3：长期】自适应降级策略
    // 只有在明确需要硬解的严格生产环境中才禁止降级；
    // 其他情况下允许 FFmpeg 软解作为 fallback，确保视频可用性
    const bool isStrictEnv = qEnvironmentVariableIsSet("CLIENT_STRICT_HW_DECODE_REQUIRED");
    
    if (requireHw && isStrictEnv) {
      // 严格模式：禁止软解降级（适用于对硬解性能有严格要求的环境）
      qWarning() << "[Client][HW-E2E][" << m_streamTag
                 << "][OPEN] hardware decoder unavailable but STRICTLY required by config"
                 << " (media.require_hardware_decode=true AND CLIENT_STRICT_HW_DECODE_REQUIRED set)"
                 << " → rejecting software fallback";
      m_dec.reset();
      return false;
    } else if (requireHw) {
      // 宽容模式（默认）：允许软解降级，但记录警告
      qWarning() << "[Client][HW-E2E][" << m_streamTag
                 << "][OPEN] hardware decoder unavailable but preferred by config;"
                 << " allowing software decoder fallback"
                 << " (to enforce strict hardware-only mode, set CLIENT_STRICT_HW_DECODE_REQUIRED=1)";
    } else {
      // 软解优先模式：正常允许
      qInfo() << "[Client][HW-E2E][" << m_streamTag
              << "][OPEN] hardware decoder unavailable, using software decoder fallback";
    }
  }

  const DecoderCapabilities caps = m_dec->queryCapabilities();
  m_backendLabel = caps.name.isEmpty() ? QStringLiteral("HW") : caps.name;

  m_dmaBufSgBroken.store(false, std::memory_order_release);
  qInfo() << "[Client][HW-E2E][" << m_streamTag << "][OPEN] ok backend=" << m_backendLabel
          << " wxh=" << cfg.width << "x" << cfg.height << " avccBytes=" << avccExtradata.size()
          << " preferDmaBufExport_effective=" << cfg.preferDmaBufExport
          << " exportDma_env="
          << (qEnvironmentVariableIntValue("CLIENT_WEBRTC_HW_EXPORT_DMABUF") != 0)
          << " CLIENT_WEBRTC_DMABUF_SG_wanted=" << m_dmabufSgEnvWanted
          << " ★ software GL 下 effective=0 见 [Client][VideoHealth][Policy]";
  return true;
#endif
}

void H264WebRtcHwBridge::disableDmaBufExportForCpuFallback() {
  m_dmaBufSgBroken.store(true, std::memory_order_release);
  if (m_dec)
    m_dec->setDmaBufExportPreferred(false);
  qWarning() << "[Client][HW-E2E][" << m_streamTag
             << "][DMABUF→CPU] Scene Graph DMA-BUF 失败，已关闭 DRM PRIME 导出；后续帧走 CPU "
                "NV12→RGBA"
             << " preferDmaWas=" << m_preferDmaBufExport << " sgEnvWanted=" << m_dmabufSgEnvWanted;
}

bool H264WebRtcHwBridge::convertCpuFrameToRgbaAndIngest(H264Decoder* dec, VideoFrame& vf) {
  if (!dec)
    return false;
  const int w = static_cast<int>(vf.width);
  const int h = static_cast<int>(vf.height);
  
  AVPixelFormat srcAvFmt = AV_PIX_FMT_NONE;
  if (vf.pixelFormat == VideoFrame::PixelFormat::NV12) {
    srcAvFmt = AV_PIX_FMT_NV12;
  } else if (vf.pixelFormat == VideoFrame::PixelFormat::YUV420P) {
    srcAvFmt = AV_PIX_FMT_YUV420P;
  }

  if (w <= 0 || h <= 0 || srcAvFmt == AV_PIX_FMT_NONE) {
    qWarning() << "[Client][HW-E2E][" << m_streamTag << "][CPU-Ingest] bad frame wxh=" << w << "x" << h
               << " fmt=" << static_cast<int>(vf.pixelFormat);
    return false;
  }
  
  if (vf.pixelFormat == VideoFrame::PixelFormat::NV12) {
    if (!vf.planes[0].data || !vf.planes[1].data) {
      qWarning() << "[Client][HW-E2E][" << m_streamTag << "][NV12] null plane ptr";
      return false;
    }
  } else if (vf.pixelFormat == VideoFrame::PixelFormat::YUV420P) {
    if (!vf.planes[0].data || !vf.planes[1].data || !vf.planes[2].data) {
      qWarning() << "[Client][HW-E2E][" << m_streamTag << "][YUV420P] null plane ptr";
      return false;
    }
  }

  if (VideoFrameEvidence::pipelineTraceEnabled()) {
    static std::atomic<int> s_cpuLog{0};
    const int n = s_cpuLog.fetch_add(1, std::memory_order_relaxed);
    if (n < 72 || (n % 100) == 0) {
      qInfo() << "[Client][VideoE2E][HW-CPU→RGBA] stream=" << m_streamTag << " sampleN=" << n
              << " wxh=" << w << "x" << h << " fmt=" << static_cast<int>(vf.pixelFormat);
    }
  }

  if (vf.pixelFormat == VideoFrame::PixelFormat::NV12) {
    VideoInterlacedPolicy::maybeApplyToNv12VideoFrame(vf, m_streamTag);
  }

  if (!m_swsNv12ToRgba || m_swsW != w || m_swsH != h || m_swsSrcFmt != srcAvFmt) {
    sws_freeContext(m_swsNv12ToRgba);
    m_swsNv12ToRgba = nullptr;
    m_swsNv12ToRgba =
        sws_getContext(w, h, srcAvFmt, w, h, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr,
                       nullptr, nullptr);
    m_swsW = w;
    m_swsH = h;
    m_swsSrcFmt = srcAvFmt;
    videoSwsConfigureYuvToRgbaColorspace(m_swsNv12ToRgba);
  }
  if (!m_swsNv12ToRgba) {
    qWarning() << "[Client][HW-E2E][" << m_streamTag << "][SWS] sws_getContext CPU→RGBA failed";
    return false;
  }

  QImage rgba(w, h, QImage::Format_RGBA8888);
  if (rgba.isNull() || !rgba.bits()) {
    qWarning() << "[Client][HW-E2E][" << m_streamTag << "][SWS] QImage alloc failed";
    return false;
  }

  const uint8_t* srcSlice[4] = {static_cast<const uint8_t*>(vf.planes[0].data),
                                static_cast<const uint8_t*>(vf.planes[1].data),
                                static_cast<const uint8_t*>(vf.planes[2].data), nullptr};
  int srcStride[4] = {static_cast<int>(vf.planes[0].stride), static_cast<int>(vf.planes[1].stride),
                      static_cast<int>(vf.planes[2].stride), 0};
  uint8_t* dstSlice[1] = {rgba.bits()};
  int dstStride[1] = {static_cast<int>(rgba.bytesPerLine())};
  const int rows = sws_scale(m_swsNv12ToRgba, srcSlice, srcStride, 0, h, dstSlice, dstStride);
  if (rows != h) {
    qWarning() << "[Client][HW-E2E][" << m_streamTag << "][SWS] sws_scale rows=" << rows << " want="
               << h;
    return false;
  }

  dec->ingestHardwareRgbaFrame(std::move(rgba), m_backendLabel);
  return true;
}

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
      // 修复：receiveFrame 失败时尝试 flush 重试一次
      errorCount++;
      if (errorCount == 1) {
        qDebug() << "[Client][HW-E2E][" << m_streamTag 
                 << "][DRAIN] receiveFrame error, flushing decoder";
        if (m_dec) {
          m_dec->flush();
        }
        continue;  // 重试一次
      } else {
        qWarning() << "[Client][HW-E2E][" << m_streamTag 
                   << "][DRAIN] receiveFrame error after flush, decode state corrupted";
        return false;
      }
    }
    
    errorCount = 0;  // 成功，重置错误计数

    if (vf.memoryType == VideoFrame::MemoryType::DMA_BUF) {
      if (vf.interlacedMetadata) {
        static std::atomic<int> s_dmaIlace{0};
        const int ilN = s_dmaIlace.fetch_add(1, std::memory_order_relaxed);
        if (ilN < 8 || (ilN % 300) == 0) {
          qWarning().noquote()
              << "[Client][HW-E2E][" << m_streamTag
              << "][Interlaced][DMABUF] libav 标记隔行：零拷贝路径不在 GPU 做去隔行；"
                 "梳齿伪影可试 CLIENT_WEBRTC_HW_EXPORT_DMABUF=0 走 CPU NV12→RGBA + "
                 "CLIENT_VIDEO_INTERLACED_POLICY";
        }
      }
      const bool useSgIngest = webRtcDmaBufSceneGraphEnabled() &&
                               !m_dmaBufSgBroken.load(std::memory_order_acquire);
      if (useSgIngest) {
        dec->ingestHardwareDmaBufFrame(std::move(vf), m_backendLabel);
        continue;
      }
      if (m_dmaBufSgBroken.load(std::memory_order_acquire)) {
        static std::atomic<int> s_transLog{0};
        if (s_transLog.fetch_add(1) < 5) {
          qInfo() << "[Client][HW-E2E][" << m_streamTag
                  << "][DMABUF→CPU] 丢弃当前 DMA-BUF 帧（切换中），下一帧起 CPU NV12";
        }
        vf.poolRef.reset();
        continue;
      }
      qWarning() << "[Client][HW-E2E][" << m_streamTag << "][DROP_DMABUF] fourcc=0x" << Qt::hex
                 << vf.dmaBuf.drmFourcc << Qt::dec << " planes=" << vf.dmaBuf.nbPlanes
                 << " fd0=" << vf.dmaBuf.fds[0] << " wxh=" << vf.width << "x" << vf.height
                 << " ★ 无 SG 消费端：构建缺 ShaderTools 或 CLIENT_WEBRTC_DMABUF_SG=0；"
                    "或设 CLIENT_WEBRTC_HW_EXPORT_DMABUF=0 走 CPU NV12";
      vf.poolRef.reset();
      continue;
    }

    if (!convertCpuFrameToRgbaAndIngest(dec, vf))
      return false;
  }
}

bool H264WebRtcHwBridge::submitCompleteAnnexB(H264Decoder* dec, const uint8_t* annexB, size_t len,
                                              int64_t pts) {
  if (!m_dec || !dec || !annexB || len == 0)
    return false;

  DecodeResult sr = m_dec->submitPacket(annexB, len, pts, AV_NOPTS_VALUE);
  if (sr == DecodeResult::NeedMore) {
    if (!drainAllOutput(dec)) {
      // 排空失败 → 解码器可能破坏 → 强制 flush 后重试
      qWarning() << "[Client][HW-E2E][" << m_streamTag 
                 << "][SUBMIT] drainAllOutput failed, flushing decoder";
      if (m_dec) {
        m_dec->flush();
      }
      sr = m_dec->submitPacket(annexB, len, pts, AV_NOPTS_VALUE);
      if (sr != DecodeResult::Ok) {
        return false;
      }
      return drainAllOutput(dec);
    }
    sr = m_dec->submitPacket(annexB, len, pts, AV_NOPTS_VALUE);
  }
  
  // 修复：Error 之前尝试一次恢复
  if (sr == DecodeResult::Error) {
    static thread_local int s_errorCount = 0;
    
    if (s_errorCount < 2) {  // 最多重试2次
      s_errorCount++;
      qWarning() << "[Client][HW-E2E][" << m_streamTag 
                 << "][SUBMIT] send_packet error (attempt " << s_errorCount << "/2), trying flush+retry";
      
      if (m_dec) {
        m_dec->flush();
      }
      sr = m_dec->submitPacket(annexB, len, pts, AV_NOPTS_VALUE);
      if (sr == DecodeResult::Ok) {
        s_errorCount = 0;  // 成功，重置
        return drainAllOutput(dec);
      }
    } else {
      s_errorCount = 0;  // 达到重试上限，重置
    }
    
    qWarning() << "[Client][HW-E2E][" << m_streamTag 
               << "][SUBMIT] send_packet error, decoder needs rebuild";
    return false;
  }
  
  if (sr == DecodeResult::NeedMore) {
    qDebug() << "[Client][HW-E2E][" << m_streamTag << "][SUBMIT] still EAGAIN after drain";
    return true;
  }
  return drainAllOutput(dec);
}

#endif
