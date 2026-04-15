// h264decoder.cpp
#include "h264decoder.h"

#include "core/configuration.h"
#include "media/H264WebRtcHwBridge.h"
#include "media/ClientVideoDiagCache.h"
#include "media/ClientVideoStreamHealth.h"
#include "media/H264ClientDiag.h"
#include "media/RtpIngressTypes.h"
#include "media/RtpPacketSpscQueue.h"
#include "media/RtpStreamClockContext.h"
#include "media/VideoInterlacedPolicy.h"
#include "media/VideoFrameEvidenceDiag.h"
#include "media/VideoFrameFingerprintCache.h"
#include "media/VideoSwsColorHelper.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QTimer>
#include <QtGlobal>

#include <algorithm>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace {
static void quiet_av_log_callback(void *avcl, int level, const char *fmt, va_list vl) {
  // ★ 条状排障：不再过滤 concealing，这通常意味着码流损坏触发了 FFmpeg 错误隐藏
  // if (fmt && strstr(fmt, "concealing"))
  //   return;
  va_list vl2;
  va_copy(vl2, vl);
  av_log_default_callback(avcl, level, fmt, vl2);
  va_end(vl2);
}

/** 非 0：记录 sws 重建原因、源像素格式名、linesize、目标 bytesPerLine（证伪「格式不变却未重建
 * m_sws」类问题）。 */
static bool h264SwsDiagEnabled() {
  return qEnvironmentVariableIntValue("CLIENT_H264_SWS_DIAG") != 0;
}

/** CLIENT_H264_STRIPE_DIAG=1：每帧打印 tryMitigate 入口快照（高噪声）。 */
static bool h264StripeDiagVerbose() {
  return qEnvironmentVariableIntValue("CLIENT_H264_STRIPE_DIAG") != 0;
}

/** 与 ClientVideoGlobalPolicySnapshot::decodeFrameSummaryEveryEffective 一致（默认 60；0=关）。 */
static int h264DecodeFrameSummaryEveryN() {
  return ClientVideoStreamHealth::globalPolicy().decodeFrameSummaryEveryEffective;
}

/** 0=默认仅前若干帧；1=每帧 [H264][DecodePath]。 */
static int h264DecodePathDiagMode() {
  return qEnvironmentVariableIntValue("CLIENT_H264_DECODE_PATH_DIAG");
}

constexpr uint32_t kStripeSkipLoggedSliceLe1 = 1u << 0;
constexpr uint32_t kStripeSkipLoggedNoCtx = 1u << 1;
constexpr uint32_t kStripeSkipLoggedThrLe1 = 1u << 2;
constexpr uint32_t kStripeSkipLoggedMitigated = 1u << 3;
}  // namespace

static quint16 rtpSeqNum(const uint8_t *d) { return (d[2] << 8) | d[3]; }
static quint32 rtpTimestamp(const uint8_t *d) {
  return (d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7];
}
static bool rtpMarkerBit(const uint8_t *d) { return (d[1] & 0x80) != 0; }
static quint32 rtpSsrc(const uint8_t *d) {
  return (static_cast<quint32>(d[8]) << 24) | (static_cast<quint32>(d[9]) << 16) |
         (static_cast<quint32>(d[10]) << 8) | static_cast<quint32>(d[11]);
}

/** RFC 3550：payload 起始 = 12 + CSRC×4 + extension；失败返回 false。 */
static bool rtpComputePayloadOffset(const uint8_t *d, size_t len, size_t *outPayloadOff,
                                    QString *err) {
  if (len < 12) {
    *err = QStringLiteral("len<12");
    return false;
  }
  const unsigned cc = static_cast<unsigned>(d[0]) & 0x0FU;
  size_t off = 12u + static_cast<size_t>(cc) * 4u;
  if (off > len) {
    *err = QStringLiteral("cc_trunc");
    return false;
  }
  if ((static_cast<unsigned>(d[0]) & 0x10U) == 0) {
    *outPayloadOff = off;
    return true;
  }
  if (len < off + 4u) {
    *err = QStringLiteral("ext_short");
    return false;
  }
  const unsigned extLen =
      (static_cast<unsigned>(d[off + 2]) << 8) | static_cast<unsigned>(d[off + 3]);
  off += 4u + static_cast<size_t>(extLen) * 4u;
  if (off > len) {
    *err = QStringLiteral("ext_overflow");
    return false;
  }
  *outPayloadOff = off;
  return true;
}
static constexpr uint8_t kFuAStart = 0x80;
static constexpr uint8_t kFuAEnd = 0x40;

H264Decoder::H264Decoder(const QString &streamTag, QObject *parent)
    : QObject(parent),
      m_ingressQueue(nullptr),
      m_trueJitter(),
      m_playoutWakeTimer(nullptr),
      m_playoutEnvLogged(false),
      m_rtpBuffer(),
      m_rtpNextExpectedSeq(0),
      m_rtpSeqInitialized(false),
      m_frameIdCounter(0),
      m_fuBuffer(),
      m_fuStarted(false),
      m_fuNalType(0),
      m_pendingFrame(),
      m_pendingNacks(),
      m_lastNackSentMs(0),
      m_lastRtpPacketTime(0),
      m_sps(),
      m_pps(),
      m_codec(nullptr),
      m_ctx(nullptr),
      m_codecOpen(false),
      m_haveDecodedKeyframe(false),
      m_needKeyframe(false),
      m_framesSinceKeyframeRequest(0),
      m_expectedSliceCount(0),
      m_forcedDecodeThreadCount(-1),
      m_stripeMitigationApplied(false),
      m_loggedMultiSliceThreadOk(false),
      m_decodePathDiagFramesRemaining(16),
      m_stripeDiagSkipLoggedMask(0),
      m_sws(nullptr),
      m_swsSrcFmt(AV_PIX_FMT_NONE),
      m_width(0),
      m_height(0),
      m_framePool(),
      m_framePoolIdx(0),
      m_slotAudit(),
      m_lastSkipFrame(-1),
      m_lastSkipLoopFilter(-1),
      m_streamTag(streamTag),
      m_framesEmitted(0),
      m_droppedPFrameCount(0),
      m_rtpPacketsProcessed(0),
      m_lastRtpSeq(0),
      m_lastSeenSsrc(0),
      m_logSendCount(0),
      m_statsWindowStart(0),
      m_statsFramesInWindow(0),
      m_statsPacketsInWindow(0),
      m_statsDroppedInWindow(0),
      m_lastStatSeqNum(0),
      m_lastFeedRtpTime(0),
      m_currentLifecycleId(0),
      m_diagFrameReadyEmitAccum(0),
      m_diagFrameDumpCount(0),
      m_lastFrameReadyEmitWallMs(0),
      m_lastKeyframeSuggestMs(0),
      m_expectedRtpPayloadType(96),
      m_droppedNonH264RtpTotal(0),
      m_droppedNonH264RtpInStatsWindow(0),
      m_webrtcHw(nullptr),
      m_webrtcHwActive(false),
      m_webrtcHwPts(0),
      m_videoHealthLoggedHwContract(false),
      m_videoHealthLoggedSwContract(false),
      m_hwBridgeTryOpenFailed(false),
      m_feedTooShortInWindow(0),
      m_feedBadRtpVersionInWindow(0),
      m_dupRtpIgnoredInWindow(0),
      m_reorderEnqueueInWindow(0),
      m_seqJumpResyncInWindow(0),
      m_rtpHdrParseFailInWindow(0),
      m_rtpNonMinimalHdrInWindow(0),
      m_statsAvSendFailInWindow(0),
      m_statsAvRecvFailInWindow(0),
      m_statsEnsureDecoderFailInWindow(0),
      m_statsQualityDropInWindow(0),
      m_statsEagainSendInWindow(0),
      m_statsIncompleteFrameDropInWindow(0),
      m_statsIncompleteFrameEmitInWindow(0),
      m_statsIncompleteHoleTotalInWindow(0),
      m_rtpSeqHist(),
      m_rtpSeqHistIdx(0),
      m_rtpSeqHistCount(0) {
  static bool s_cb_set = false;
  if (!s_cb_set) {
    av_log_set_callback(quiet_av_log_callback);
    s_cb_set = true;
  }
  const QByteArray ptEnv = qgetenv("CLIENT_H264_RTP_PAYLOAD_TYPE");
  if (!ptEnv.isEmpty()) {
    bool ok = false;
    const int v = QString::fromLatin1(ptEnv).toInt(&ok);
    if (ok && v >= 0 && v <= 127)
      m_expectedRtpPayloadType = v;
  }

  m_playoutWakeTimer = new QTimer(this);
  m_playoutWakeTimer->setSingleShot(true);
  connect(m_playoutWakeTimer, &QTimer::timeout, this, &H264Decoder::drainRtpIngressQueue);
}

H264Decoder::~H264Decoder() {
  reset();
  if (m_sws) {
    sws_freeContext(m_sws);
    m_sws = nullptr;
  }
}

void H264Decoder::closeDecoder() {
  m_videoHealthLoggedHwContract = false;
  m_videoHealthLoggedSwContract = false;
  m_hwBridgeTryOpenFailed = false;
  if (m_webrtcHw && m_webrtcHwActive) {
    m_webrtcHw->shutdown();
    m_webrtcHwActive = false;
    m_webrtcHwPts = 0;
    qDebug() << "[H264] closeDecoder: 已关闭 WebRTC 硬解旁路 stream=" << m_streamTag;
  }
  if (m_ctx) {
    qDebug() << "[H264] closeDecoder: 正在关闭解码器 m_codecOpen=" << m_codecOpen;
    avcodec_free_context(&m_ctx);
    m_ctx = nullptr;
  } else {
    qDebug() << "[H264] closeDecoder: m_ctx 已是 nullptr，无操作";
  }
  m_codecOpen = false;
}

bool H264Decoder::ensureDecoder() {
  if (!m_codec) {
    m_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!m_codec) {
      qCritical() << "[H264] ensureDecoder: 未找到 H264 解码器!";
      return false;
    }
    qDebug() << "[H264] ensureDecoder: 找到 H264 解码器";
  }

  if (H264WebRtcHwBridge::hardwareDecodeRequested()) {
    if (!m_webrtcHw)
      m_webrtcHw = std::make_unique<H264WebRtcHwBridge>(m_streamTag);
    if (!m_webrtcHwActive && !m_hwBridgeTryOpenFailed && !m_sps.isEmpty() && !m_pps.isEmpty()) {
      const QByteArray avcc = buildAvccExtradata();
      if (!avcc.isEmpty()) {
        if (m_webrtcHw->tryOpen(avcc, m_width, m_height) && m_webrtcHw->isHardwareAccelerated()) {
          m_webrtcHwActive = true;
          m_webrtcHwPts = 0;
          qInfo() << "[H264][" << m_streamTag
                  << "][HW-E2E] active backend path (IHardwareDecoder) policy=media.hardware_decode "
                     "legacyOffEnv=" << H264WebRtcHwBridge::kEnvVarName;
          if (!m_videoHealthLoggedHwContract) {
            m_videoHealthLoggedHwContract = true;
            logVideoStreamHealthContract(QStringLiteral("path_hw"));
          }
        } else {
          // 如果 bridge 成功打开但不是硬解，则不激活 webrtcHwActive，从而走 native SW 路径
          m_webrtcHwActive = false;
          m_hwBridgeTryOpenFailed = true; // ★ 避免每帧尝试
          if (m_webrtcHw->isActive()) {
            qInfo() << "[H264][" << m_streamTag
                    << "][HW-E2E] bridge opened but not hardware accelerated, using native software path instead";
            m_webrtcHw->shutdown();
          }
        }
      }
    }
    if (m_webrtcHwActive)
      return true;
  } else {
    if (m_webrtcHw) {
      m_webrtcHw->shutdown();
      m_webrtcHwActive = false;
      m_webrtcHwPts = 0;
    }
  }

#if defined(ENABLE_VAAPI) || defined(ENABLE_NVDEC)
  constexpr bool kClientHwDecodeCompiled = true;
#else
  constexpr bool kClientHwDecodeCompiled = false;
#endif
  // 检查硬解配置与可用性
  if (H264WebRtcHwBridge::hardwareDecodeRequested() && !m_webrtcHwActive &&
      !m_sps.isEmpty() && !m_pps.isEmpty()) {
    // ★ 节流：仅在首帧或每 300 帧打印一次 fallback 警告，消除日志风暴
    const bool logThrottle = (m_framesEmitted == 0 || (m_framesEmitted % 300) == 0);
    
    // 硬解被要求，但检查是否编译支持
    if (!kClientHwDecodeCompiled && Configuration::instance().requireHardwareDecode()) {
      if (logThrottle) {
        qCritical() << "[H264][" << m_streamTag
                    << "][HW-REQUIRED] media.require_hardware_decode=true 但未编译 VA-API/NVDEC，拒绝软解。"
                       "请装 libva-dev 或 cmake -DENABLE_NVDEC=ON（及 FFmpeg CUDA），或配置 "
                       "media.require_hardware_decode=false / CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0";
      }
      return false;
    }
    // 硬解初始化失败，检查是否允许降级
    const bool strictHw = qEnvironmentVariableIsSet("CLIENT_STRICT_HW_DECODE_REQUIRED");
    if (Configuration::instance().requireHardwareDecode() && strictHw) {
      if (logThrottle) {
        qCritical() << "[H264][" << m_streamTag
                    << "][HW-REQUIRED] 硬解已编译但未激活（设备/驱动不可用），media.require_hardware_decode=true "
                       "且已设 CLIENT_STRICT_HW_DECODE_REQUIRED，禁止退回软解。检查 VA-API/NVDEC 设备可用性或设置 "
                       "media.require_hardware_decode=false / CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0";
      }
      return false;
    } else if (Configuration::instance().requireHardwareDecode()) {
      if (logThrottle) {
        qWarning() << "[H264][" << m_streamTag
                   << "][HW-FALLBACK] 硬解已编译但未激活（设备/驱动不可用）；虽然 media.require_hardware_decode=true，"
                      "但未设 CLIENT_STRICT_HW_DECODE_REQUIRED，为保证可用性，降级至 CPU 软解。";
      }
    }
    // 允许软解，输出信息日志而非错误
    if (logThrottle) {
      qWarning() << "[H264][" << m_streamTag
                 << "][HW-AVAILABLE] 硬解配置已启用，但当前不可用；将降级至 CPU 软解"
                 << "（硬解编译=" << (kClientHwDecodeCompiled ? "Yes" : "No")
                 << "，require=" << (Configuration::instance().requireHardwareDecode() ? "Yes" : "No") << "）";
    }
  }

  if (!m_ctx) {
    m_ctx = avcodec_alloc_context3(m_codec);
    if (!m_ctx) {
      qCritical() << "[H264] ensureDecoder: avcodec_alloc_context3 失败!";
      return false;
    }
    // FFmpeg slice 级多线程（FF_THREAD_SLICE）与「每帧多 slice」H.264（如 CARLA/WebRTC 常见 8~16
    // slices） 组合时，若码流未启用跨 slice 去块（loop_filter_across_slices），并行 slice
    // 解码会在边界产生 明显水平带状伪影（用户描述的「条状看不清」）。官方行为见 libavcodec
    // 线程文档： https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html
    //
    // 彻底消除条纹方案：强制 thread_count=1。
    // 日志表明 expSlices=11 说明编码端受环境干扰严重，客户端必须启用防御性单线程解码。
    const QByteArray tcEnv = qgetenv("CLIENT_FFMPEG_DECODE_THREADS");
    bool tcOk = false;
    const int tcEnvVal = tcEnv.isEmpty() ? 0 : tcEnv.toInt(&tcOk);
    
    int threadCount = 1; 
    if (m_expectedSliceCount > 1) {
        qWarning() << "[H264][" << m_streamTag << "][StripeDefense] 检测到多切片流 expSlices=" << m_expectedSliceCount 
                   << "，强制使用单线程解码以规避条纹风险";
        threadCount = 1;
    } else if (tcOk && tcEnvVal > 1) {
        qWarning() << "[H264] 检测到环境变量 CLIENT_FFMPEG_DECODE_THREADS=" << tcEnvVal 
                   << "，但为消除条纹，强制修正为 1 (单线程解码)";
        threadCount = 1;
    }
    
    if (m_forcedDecodeThreadCount >= 1) {
      threadCount = m_forcedDecodeThreadCount;
      qInfo() << "[H264] ensureDecoder: 使用强制 thread_count=" << threadCount
              << "（条状风险自愈或手动覆盖）CLIENT_FFMPEG_DECODE_THREADS 环境值将被忽略";
    } else if (ClientVideoStreamHealth::shouldForceSingleThreadDecodeUnderSoftwareRaster(
                   threadCount)) {
      threadCount = 1;
    }
    m_ctx->thread_count = threadCount;
    // ★ v5 修复：只有在真正多线程时才开启 SLICE 模式，否则设为 0 以规避内部逻辑干扰
    m_ctx->thread_type = (threadCount > 1) ? FF_THREAD_SLICE : 0;
    
    // ── ★ v5 根本原因定死：Error Concealment 配置 ────────────────────────────
    // 默认开启。如果禁用后条纹变黑块，则证明是 FFmpeg 在尝试补全丢失的 slice 数据。
    const bool disableEc = qEnvironmentVariableIntValue("CLIENT_H264_DISABLE_EC") != 0;
    if (disableEc) {
        m_ctx->error_concealment = 0;
        qInfo() << "[H264][" << m_streamTag << "][EC] 强制禁用错误隐藏（Error Concealment）";
    } else {
        m_ctx->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
    }
    
    qInfo() << "[H264] ensureDecoder: 解码器上下文 thread_count=" << threadCount
            << " thread_type=" << m_ctx->thread_type << " EC=" << m_ctx->error_concealment
            << " CLIENT_FFMPEG_DECODE_THREADS="
            << (tcEnv.isEmpty() ? QByteArrayLiteral("(unset→1)") : tcEnv);
    if (tcOk && tcEnvVal > 1 && threadCount == 1 && m_forcedDecodeThreadCount < 1 &&
        ClientVideoStreamHealth::displayStackAssumedSoftwareRaster()) {
      qInfo().noquote()
          << "[H264][" << m_streamTag << "][DecodeThreadClamp] CLIENT_FFMPEG_DECODE_THREADS env="
          << tcEnvVal << " → effective libavcodec thread_count=1（软件光栅栈策略；规避 H264 多 slice × "
                          "FF_THREAD_SLICE 条状伪影）放行："
                          "CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL=1 "
                          "或 unset LIBGL_ALWAYS_SOFTWARE / 使用硬件 GL"
          << " " << ClientVideoDiagCache::videoPipelineEnvFingerprint();
    }
  }
  if (m_codecOpen)
    return true;
  if (m_sps.isEmpty() || m_pps.isEmpty()) {
    qDebug() << "[H264] ensureDecoder: SPS 或 PPS 未就绪 sps.empty=" << m_sps.isEmpty()
             << " pps.empty=" << m_pps.isEmpty();
    return false;
  }
  bool ok = openDecoderWithExtradata();
  if (ok && !m_videoHealthLoggedSwContract) {
    m_videoHealthLoggedSwContract = true;
    logVideoStreamHealthContract(QStringLiteral("path_sw"));
    const int thr = m_ctx ? static_cast<int>(m_ctx->thread_count) : -1;
    qInfo().noquote()
        << "[H264][" << m_streamTag << "][CpuPresentContract] path=software_decode "
           "sws_dst=AV_PIX_FMT_RGBA QImage=Format_RGBA8888 Qt6_RHI→GL_RGBA "
           "(规避 Mesa llvmpipe+GL_BGRA stride 类水平条带) libavThr=" << thr
        << " swRasterAssumed=" << (ClientVideoStreamHealth::displayStackAssumedSoftwareRaster() ? "Y" : "N")
        << " cpuTexFmtStrict=" << (ClientVideoStreamHealth::cpuPresentFormatStrict() ? "Y" : "N")
        << " stripeAutoMit="
        << (ClientVideoStreamHealth::globalPolicy().stripeAutoMitigationEffective ? 1 : 0)
        << " " << ClientVideoDiagCache::videoPipelineEnvFingerprint()
        << " ★对照 [Client][UI][RemoteVideoSurface][PresentContract] 与 [Client][CodecHealth][1Hz]";
  }
  if (!ok) {
    qCritical() << "[H264] ensureDecoder: openDecoderWithExtradata 失败!";
  }
  return ok;
}

QByteArray H264Decoder::buildAvccExtradata() const {
  if (m_sps.isEmpty() || m_pps.isEmpty())
    return {};
  const int spsLen = m_sps.size();
  const int ppsLen = m_pps.size();
  if (spsLen < 4 || ppsLen < 1)
    return {};
  const int extSize = 11 + spsLen + ppsLen;
  QByteArray out(extSize, '\0');
  uint8_t *p = reinterpret_cast<uint8_t *>(out.data());
  *p++ = 1;
  *p++ = static_cast<uint8_t>(m_sps[1]);
  *p++ = static_cast<uint8_t>(m_sps[2]);
  *p++ = static_cast<uint8_t>(m_sps[3]);
  *p++ = 0xFF;
  *p++ = 0xE1;
  *p++ = (spsLen >> 8) & 0xFF;
  *p++ = spsLen & 0xFF;
  memcpy(p, m_sps.constData(), static_cast<size_t>(spsLen));
  p += spsLen;
  *p++ = 1;
  *p++ = (ppsLen >> 8) & 0xFF;
  *p++ = ppsLen & 0xFF;
  memcpy(p, m_pps.constData(), static_cast<size_t>(ppsLen));
  return out;
}

bool H264Decoder::openDecoderWithExtradata() {
  if (m_codecOpen || !m_ctx || m_sps.isEmpty() || m_pps.isEmpty())
    return false;

  const QByteArray avcc = buildAvccExtradata();
  if (avcc.isEmpty())
    return false;

  const int extSize = avcc.size();
  uint8_t *ext = static_cast<uint8_t *>(av_malloc(static_cast<size_t>(extSize) + AV_INPUT_BUFFER_PADDING_SIZE));
  if (!ext)
    return false;

  memcpy(ext, avcc.constData(), static_cast<size_t>(extSize));
  memset(ext + extSize, 0, AV_INPUT_BUFFER_PADDING_SIZE);

  m_ctx->extradata = ext;
  m_ctx->extradata_size = extSize;

  if (avcodec_open2(m_ctx, m_codec, nullptr) < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(AVERROR_UNKNOWN, errbuf, sizeof(errbuf));
    qCritical() << "[H264] avcodec_open2 失败! err=" << errbuf << " avccSize=" << extSize
                << " spsLen=" << m_sps.size() << " ppsLen=" << m_pps.size();
    av_freep(&m_ctx->extradata);
    m_ctx->extradata_size = 0;
    return false;
  }
  m_codecOpen = true;

  qInfo() << "[H264][" << m_streamTag << "][CodecOpen] libavcodec 解码器已打开"
          << " thread_count=" << m_ctx->thread_count
          << " thread_type=" << m_ctx->thread_type
          << " err_recognition=" << m_ctx->err_recognition
          << " error_concealment=" << m_ctx->error_concealment
          << " skip_loop_filter=" << m_ctx->skip_loop_filter
          << " flags=0x" << QString::number(m_ctx->flags, 16)
          << " flags2=0x" << QString::number(m_ctx->flags2, 16);
  qDebug() << "[H264] openDecoderWithExtradata ok sps=" << m_sps.size() << "pps=" << m_pps.size();
  qInfo() << "[H264][" << m_streamTag << "][DecodePath] WebRTC=libavcodec CPU decode + sws→RGBA8888"
          << "（与 MediaPipeline/DecoderFactory 硬解独立；硬解需走独立管道或后续为本路径接 hwaccel）";
  return true;
}

void H264Decoder::flushCodecBuffers() {
  if (m_webrtcHw && m_webrtcHwActive) {
    m_webrtcHw->shutdown();
    m_webrtcHwActive = false;
    m_webrtcHwPts = 0;
    qDebug() << "[H264][flushCodecBuffers] WebRTC 硬解旁路 shutdown stream=" << m_streamTag;
  }
  if (m_ctx && m_codecOpen) {
    qDebug() << "[H264][flushCodecBuffers] flushing libavcodec stream=" << m_streamTag;
    avcodec_send_packet(m_ctx, nullptr);
    AVFrame *f = av_frame_alloc();
    if (f) {
      int drained = 0;
      while (avcodec_receive_frame(m_ctx, f) == 0) {
        ++drained;
      }
      if (drained > 0)
        qDebug() << "[H264][flushCodecBuffers] drained residual frames=" << drained;
      av_frame_free(&f);
    } else {
      qWarning() << "[H264][flushCodecBuffers] av_frame_alloc failed stream=" << m_streamTag;
    }
    avcodec_flush_buffers(m_ctx);
  } else {
    qDebug() << "[H264][flushCodecBuffers] no soft codec to flush stream=" << m_streamTag
             << " m_ctx=" << (void *)m_ctx << " codecOpen=" << m_codecOpen
             << " webrtcHwActive=" << m_webrtcHwActive;
  }
}

void H264Decoder::resetStreamStateIncludingExtradata() {
  m_sps.clear();
  m_pps.clear();
  qDebug() << "[H264][resetStreamStateIncludingExtradata] cleared SPS/PPS stream=" << m_streamTag;
}

void H264Decoder::resetKeyframeGopExpectation() {
  m_haveDecodedKeyframe = false;
  m_needKeyframe = true;
}

void H264Decoder::idrRecoveryFlushCodecOnly() {
  flushCodecBuffers();
  resetKeyframeGopExpectation();
}

void H264Decoder::recoverDecoderAfterCorruptBitstream() {
  flushCodecBuffers();
  resetKeyframeGopExpectation();
  resetStreamStateIncludingExtradata();
}

void H264Decoder::reset() {
  qDebug() << "[H264] reset: 开始重置"
           << " framesEmitted=" << m_framesEmitted << " droppedPFrames=" << m_droppedPFrameCount
           << " codecOpen=" << m_codecOpen << " bufSize=" << m_rtpBuffer.size();
  closeDecoder();
  if (m_webrtcHw) {
    m_webrtcHw->shutdown();
    m_webrtcHw.reset();
  }
  m_webrtcHwActive = false;
  m_webrtcHwPts = 0;
  m_codec = nullptr;
  if (m_sws) {
    sws_freeContext(m_sws);
    m_sws = nullptr;
  }
  m_swsSrcFmt = AV_PIX_FMT_NONE;
  m_width = m_height = 0;
  m_fuBuffer.clear();
  m_fuStarted = false;
  m_sps.clear();
  m_pps.clear();
  m_haveDecodedKeyframe = false;
  m_needKeyframe = true;  // 重置后需要等待 IDR
  m_framesSinceKeyframeRequest = 0;
  m_expectedSliceCount = 0;
  m_pendingFrame = PendingFrame();
  m_rtpBuffer.clear();
  m_rtpSeqInitialized = false;
  m_rtpNextExpectedSeq = 0;
  m_lastRtpPacketTime = 0;
  m_framesEmitted = 0;
  m_droppedPFrameCount = 0;
  m_rtpPacketsProcessed = 0;
  m_lastRtpSeq = 0;
  m_lastSeenSsrc = 0;
  m_logSendCount = 0;
  // ── 诊断窗口重置 ─────────────────────────────────────────────────────────
  m_statsWindowStart = 0;
  m_statsFramesInWindow = 0;
  m_statsPacketsInWindow = 0;
  m_statsDroppedInWindow = 0;
  m_lastStatSeqNum = 0;
  for (auto &fb : m_framePool)
    fb = QImage();  // 释放帧缓冲池（维度可能变化）
  m_framePoolIdx = 0;
  m_droppedNonH264RtpTotal = 0;
  m_droppedNonH264RtpInStatsWindow = 0;
  m_feedTooShortInWindow = 0;
  m_feedBadRtpVersionInWindow = 0;
  m_dupRtpIgnoredInWindow = 0;
  m_reorderEnqueueInWindow = 0;
  m_seqJumpResyncInWindow = 0;
  m_rtpHdrParseFailInWindow = 0;
  m_rtpNonMinimalHdrInWindow = 0;
  m_statsAvSendFailInWindow = 0;
  m_statsAvRecvFailInWindow = 0;
  m_statsEnsureDecoderFailInWindow = 0;
  m_statsQualityDropInWindow = 0;
  m_statsEagainSendInWindow = 0;
  m_diagFrameReadyEmitAccum.store(0, std::memory_order_relaxed);
  m_diagFrameDumpCount = 0;
  m_forcedDecodeThreadCount = -1;
  m_stripeMitigationApplied = false;
  m_loggedMultiSliceThreadOk = false;
  m_decodePathDiagFramesRemaining = 16;
  m_stripeDiagSkipLoggedMask = 0;
  m_trueJitter.clear();
  if (m_playoutWakeTimer)
    m_playoutWakeTimer->stop();
  qInfo() << "[Client][CodecHealth][Reset] stream=" << m_streamTag
          << " ★解码器状态已清空，下一窗 [Client][CodecHealth][1Hz] 可能为 WAIT_IDR/NO_MEDIA_RTP";
  qDebug() << "[H264] reset: 完成";
}

void H264Decoder::setIngressQueue(RtpPacketSpscQueue *q) { m_ingressQueue = q; }

void H264Decoder::setRtpClockContext(RtpStreamClockContext *ctx) {
  m_trueJitter.setClockContext(ctx);
}

void H264Decoder::emitKeyframeSuggestThrottled(const char *reason) {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  // 从 450ms 降低到 200ms，高丢包环境下更快请求 IDR 恢复视频，减少黑屏时长
  if (now - m_lastKeyframeSuggestMs < 200)
    return;
  m_lastKeyframeSuggestMs = now;
  qInfo() << "[H264][RTCP][Hint] senderKeyframeSuggested stream=" << m_streamTag
          << " reason=" << reason;
  emit senderKeyframeSuggested();
}

void H264Decoder::drainRtpIngressQueue() {
  if (!m_ingressQueue) {
    emit ingressDrainFinished(false);
    return;
  }
  m_trueJitter.reloadEnv();
  const bool jitterOn = m_trueJitter.isActive();
  if (jitterOn && !m_playoutEnvLogged) {
    m_playoutEnvLogged = true;
    qInfo() << "[Client][Media][Jitter] 真抖动缓冲已启用 stream=" << m_streamTag
            << " mode=" << static_cast<int>(m_trueJitter.mode())
            << " CLIENT_RTP_PLAYOUT_MODE / CLIENT_RTP_JITTER_* / RtpTrueJitterBuffer.cpp 头注释";
  }
  if (!jitterOn)
    m_playoutEnvLogged = false;

  int maxBatch = qEnvironmentVariableIntValue("CLIENT_RTP_INGRESS_BATCH_MAX");
  if (maxBatch <= 0)
    maxBatch = 384;

  int nPull = 0;
  int jitterDrops = 0;
  while (nPull < maxBatch) {
    RtpIngressPacket p;
    if (!m_ingressQueue->pop(p))
      break;
    const uint8_t *d = reinterpret_cast<const uint8_t *>(p.bytes.constData());
    const size_t plen = static_cast<size_t>(p.bytes.size());
    if (!jitterOn) {
      feedRtp(d, plen, p.lifecycleId);
    } else {
      const qint64 recvWallMs = QDateTime::currentMSecsSinceEpoch();
      jitterDrops += m_trueJitter.enqueue(std::move(p), recvWallMs, d, plen);
    }
    ++nPull;
  }
  if (jitterDrops > 0)
    emitKeyframeSuggestThrottled("jitter_overflow_or_late");

  // ★ WHY5 修复：在此处也触发一次 NACK 检查，处理重试逻辑
  checkAndRequestNacks();

  const qint64 nowWall = QDateTime::currentMSecsSinceEpoch();
  if (jitterOn) {
    int nFed = 0;
    while (nFed < maxBatch) {
      RtpIngressPacket p;
      if (!m_trueJitter.tryPopDue(nowWall, p))
        break;
      feedRtp(reinterpret_cast<const uint8_t *>(p.bytes.constData()),
              static_cast<size_t>(p.bytes.size()), p.lifecycleId);
      ++nFed;
    }
  }

  if (m_trueJitter.consumeHoleKeyframeRequest())
    emitKeyframeSuggestThrottled("jitter_seq_hole");

  m_trueJitter.logMetricsIfDue(nowWall, m_streamTag);

  const bool morePending = !m_ingressQueue->empty() || (jitterOn && !m_trueJitter.empty());
  emit ingressDrainFinished(morePending);

  if (jitterOn && !m_trueJitter.empty() && m_playoutWakeTimer) {
    int wait = m_trueJitter.millisUntilNextDue(QDateTime::currentMSecsSinceEpoch());
    if (wait < 0)
      wait = 1;
    else if (wait == 0)
      wait = 1;
    wait = qMin(wait, 500);
    m_playoutWakeTimer->start(wait);
  } else if (m_playoutWakeTimer) {
    m_playoutWakeTimer->stop();
  }
}

void H264Decoder::clearIngressQueue() {
  m_trueJitter.clear();
  if (m_playoutWakeTimer)
    m_playoutWakeTimer->stop();
  if (m_ingressQueue)
    m_ingressQueue->discardAll();
}

bool H264Decoder::handleParameterSet(const uint8_t *nal, size_t nalLen) {
  if (nalLen < 1) {
    qWarning() << "[H264] handleParameterSet: nalLen=0，忽略";
    return false;
  }
  int nalType = nal[0] & 0x1f;

  if (nalType == 7) {
    QByteArray newSps(reinterpret_cast<const char *>(nal), static_cast<int>(nalLen));
    if (newSps != m_sps) {
      // ── 诊断：SPS 变化时打印完整 hex（前 8 字节）+ 与 ZLM 侧抓包对比 ─────
      QString spsHex;
      for (size_t i = 0; i < std::min(nalLen, static_cast<size_t>(8)); ++i)
        spsHex += QString::asprintf("%02X ", static_cast<unsigned char>(nal[i]));
      qInfo() << "[H264][SPS] SPS 变化 len=" << nalLen << " oldSize=" << m_sps.size()
              << " hex(前8)=" << spsHex << "... 【与 ZLM 侧抓包对比确认参数集是否一致】";
      m_sps = newSps;
      closeDecoder();
      m_haveDecodedKeyframe = false;
      m_needKeyframe = true;
      m_framesSinceKeyframeRequest = 0;
    }
    if (!m_sps.isEmpty() && !m_pps.isEmpty())
      H264ClientDiag::logParameterSetsIfRequested(m_streamTag, m_sps, m_pps);
    return true;
  }
  if (nalType == 8) {
    QByteArray newPps(reinterpret_cast<const char *>(nal), static_cast<int>(nalLen));
    if (newPps != m_pps) {
      QString ppsHex;
      for (size_t i = 0; i < std::min(nalLen, static_cast<size_t>(8)); ++i)
        ppsHex += QString::asprintf("%02X ", static_cast<unsigned char>(nal[i]));
      qInfo() << "[H264][PPS] PPS 变化 len=" << nalLen << " oldSize=" << m_pps.size()
              << " hex(前8)=" << ppsHex;
      m_pps = newPps;
      closeDecoder();
      m_haveDecodedKeyframe = false;
      m_needKeyframe = true;
      m_framesSinceKeyframeRequest = 0;
    }
    if (!m_sps.isEmpty() && !m_pps.isEmpty())
      H264ClientDiag::logParameterSetsIfRequested(m_streamTag, m_sps, m_pps);
    return true;
  }
  return false;
}

// ============================================================================
// RTP
// ============================================================================
void H264Decoder::feedRtp(const uint8_t *data, size_t len, quint64 lifecycleId) {
  // ── ★★★ 端到端追踪：feedRtp 进入（解码线程；经 SPSC + 可选固定 playout 后） ★★★ ─────────
  const int64_t feedRtpEnterTime = QDateTime::currentMSecsSinceEpoch();
  m_lastFeedRtpTime = feedRtpEnterTime;
  m_currentLifecycleId.store(lifecycleId,
                             std::memory_order_release);  // 传递给解码流程，emitDecodedFrames 使用

  const int64_t now = feedRtpEnterTime;
  if (m_statsWindowStart == 0) {
    m_statsWindowStart = now;
  } else if (now - m_statsWindowStart >= kStatsIntervalMs) {
    const int64_t winMs = now - m_statsWindowStart;
    const int processedDelta = m_rtpPacketsProcessed - m_lastStatSeqNum;
    const int legacyHeuristic = m_statsPacketsInWindow - processedDelta;
    const int droppedInWindow = m_droppedPFrameCount - m_statsDroppedInWindow;
    const double fps =
        (winMs > 0)
            ? (static_cast<double>(m_statsFramesInWindow) * 1000.0 / static_cast<double>(winMs))
            : 0.0;
    const int statsFrames = m_statsFramesInWindow;
    const int statsPkts = m_statsPacketsInWindow;
    qInfo() << "[H264][Stats] ★★★ 帧统计(每秒) stream=" << m_streamTag << " ★★★"
            << " fps=" << fps << " emitted=" << statsFrames
            << " ★ emitted=本窗解码输出帧数；与 [Client][VideoPresent][1Hz] 同路 dE/n "
               "对齐（dE=emit frameReady, n=setVideoFrame）"
            << " droppedBeforeIdr=" << droppedInWindow
            << " mediaRtpAccepted=" << statsPkts
            << " payloadProcessedDelta=" << processedDelta
            << " heuristicAcceptedMinusProcessed=" << legacyHeuristic
            << " ★≈dupIgnored+乱序缓冲未 drain；负值见 dup/reorder 计数"
            << " feedTooShort=" << m_feedTooShortInWindow
            << " badRtpVersion=" << m_feedBadRtpVersionInWindow
            << " droppedWrongPt=" << m_droppedNonH264RtpInStatsWindow
            << " dupRtpIgnored=" << m_dupRtpIgnoredInWindow
            << " reorderEnqueued=" << m_reorderEnqueueInWindow
            << " seqJumpResync=" << m_seqJumpResyncInWindow
            << " rtpHdrParseFail=" << m_rtpHdrParseFailInWindow
            << " rtpNonMinimalHdr=" << m_rtpNonMinimalHdrInWindow << " durationMs=" << winMs
            << " bufSize=" << m_rtpBuffer.size() << " needKeyframe=" << m_needKeyframe
            << " codecOpen=" << m_codecOpen << " totalProcessed=" << m_rtpPacketsProcessed
            << " avSendFail=" << m_statsAvSendFailInWindow
            << " avRecvFail=" << m_statsAvRecvFailInWindow
            << " ensureDecFail=" << m_statsEnsureDecoderFailInWindow
            << " qualityDrop=" << m_statsQualityDropInWindow
            << " eagainSend=" << m_statsEagainSendInWindow
            << " incompleteDrop=" << m_statsIncompleteFrameDropInWindow
            << " incompleteEmit=" << m_statsIncompleteFrameEmitInWindow
            << " holeTotal=" << m_statsIncompleteHoleTotalInWindow
            << " ★incompleteDrop=丢弃的RTP空洞帧(防条状)；holeTotal=本窗总丢包数；"
               "incompleteEmit=已允许emit的不完整帧(CLIENT_H264_ALLOW_INCOMPLETE_FRAMES=1时>0)"
            << " ★与[Client][CodecHealth][1Hz]同源计数";
    m_statsWindowStart = now;
    m_statsFramesInWindow = 0;
    m_statsPacketsInWindow = 0;
    m_statsDroppedInWindow = m_droppedPFrameCount;
    m_lastStatSeqNum = m_rtpPacketsProcessed;
    m_droppedNonH264RtpInStatsWindow = 0;
    m_feedTooShortInWindow = 0;
    m_feedBadRtpVersionInWindow = 0;
    m_dupRtpIgnoredInWindow = 0;
    m_reorderEnqueueInWindow = 0;
    m_seqJumpResyncInWindow = 0;
    m_rtpHdrParseFailInWindow = 0;
    m_rtpNonMinimalHdrInWindow = 0;

    const bool codecHealthLine =
        qEnvironmentVariableIsEmpty("CLIENT_CODEC_HEALTH_1HZ") ||
        qEnvironmentVariableIntValue("CLIENT_CODEC_HEALTH_1HZ") != 0;
    if (codecHealthLine) {
      QString verdict;
      if (m_statsAvSendFailInWindow > 0 || m_statsAvRecvFailInWindow > 0 ||
          m_statsEnsureDecoderFailInWindow > 0) {
        verdict = QStringLiteral("DECODE_API_ERR");
      } else if (m_statsQualityDropInWindow > 0) {
        verdict = QStringLiteral("QUALITY_DROP");
      } else if (!m_haveDecodedKeyframe && statsPkts > 25) {
        verdict = QStringLiteral("WAIT_IDR");
      } else if (m_needKeyframe) {
        verdict = QStringLiteral("RECOVERING");
      } else if (m_haveDecodedKeyframe && statsFrames == 0 && statsPkts > 20) {
        verdict = QStringLiteral("STALL");
      } else if (statsPkts == 0) {
        verdict = QStringLiteral("NO_MEDIA_RTP");
      } else {
        verdict = QStringLiteral("OK");
      }
      const int decThreads =
          m_webrtcHwActive ? -2
                           : ((m_codecOpen && m_ctx) ? static_cast<int>(m_ctx->thread_count) : -1);
      qInfo().noquote()
          << QStringLiteral(
                 "[Client][CodecHealth][1Hz] stream=%1 verdict=%2 fps=%3 decFrames=%4 rtpPkts=%5 "
                 "wxh=%6x%7 codecOpen=%8 haveKeyframe=%9 needKeyframe=%10 stripeMitigation=%11 "
                 "decThreads=%12 expSlices=%13 avSendFail=%14 avRecvFail=%15 ensureDecFail=%16 "
                 "qualityDrop=%17 eagainSend=%18 webrtcHw=%19 "
                 "incompleteDrop=%20 holeTotal=%21 "
                 "★expSlices=已学习每帧slice数(0=未知)；>1且decThreads>1见[H264][STRIPE_RISK]；"
                 "incompleteDrop>0=有RTP空洞帧被丢弃(防条状)；holeTotal=本秒总丢包；"
                 "grep本行确认解码健康；DECODE_API_ERR/STALL/WAIT_IDR需对照[H264][Stats]")
                 .arg(m_streamTag)
                 .arg(verdict)
                 .arg(fps, 0, 'f', 2)
                 .arg(statsFrames)
                 .arg(statsPkts)
                 .arg(m_width)
                 .arg(m_height)
                 .arg(m_codecOpen ? 1 : 0)
                 .arg(m_haveDecodedKeyframe ? 1 : 0)
                 .arg(m_needKeyframe ? 1 : 0)
                 .arg(m_stripeMitigationApplied ? 1 : 0)
                 .arg(decThreads)
                 .arg(m_expectedSliceCount)
                 .arg(m_statsAvSendFailInWindow)
                 .arg(m_statsAvRecvFailInWindow)
                 .arg(m_statsEnsureDecoderFailInWindow)
                 .arg(m_statsQualityDropInWindow)
                 .arg(m_statsEagainSendInWindow)
                 .arg(m_webrtcHwActive ? 1 : 0)
                 .arg(m_statsIncompleteFrameDropInWindow)
                 .arg(m_statsIncompleteHoleTotalInWindow);
    }
    m_statsAvSendFailInWindow = 0;
    m_statsAvRecvFailInWindow = 0;
    m_statsEnsureDecoderFailInWindow = 0;
    m_statsQualityDropInWindow = 0;
    m_statsEagainSendInWindow = 0;
    m_statsIncompleteFrameDropInWindow = 0;
    m_statsIncompleteFrameEmitInWindow = 0;
    m_statsIncompleteHoleTotalInWindow = 0;
  }

  if (len <= kRtpHeaderMinLen) {
    ++m_feedTooShortInWindow;
    qWarning() << "[H264][feedRtp] RTP 包太短，忽略 len=" << len << " lifecycleId=" << lifecycleId;
    return;
  }

  quint16 seq = rtpSeqNum(data);
  quint32 ts = rtpTimestamp(data);
  bool marker = rtpMarkerBit(data);
  const quint32 curSsrc = rtpSsrc(data);
  const int rtpVersion = data[0] >> 6;
  const uint8_t rawRtpByte1 = data[1];
  const int payloadType = rawRtpByte1 & 0x7F;

  // ── RFC 3550：Track 回调可能混入 RTCP（如 SR 包类型 200，即第二字节 0xC8）。
  //    误按 RTP 解析时 M=1、PT=72（200&0x7F），会打乱 seq/SSRC 并产生 STAP-A/FU-A 伪解析 ——
  //    花屏与闪烁。
  if (rtpVersion != 2) {
    ++m_feedBadRtpVersionInWindow;
    qWarning() << "[H264][feedRtp] 丢弃: RTP Version!=2 stream=" << m_streamTag << " b0=0x"
               << Qt::hex << static_cast<unsigned>(data[0]) << Qt::dec << " len=" << len;
    return;
  }
  if (payloadType != m_expectedRtpPayloadType) {
    ++m_droppedNonH264RtpTotal;
    ++m_droppedNonH264RtpInStatsWindow;
    const bool likelyRtcp = (rawRtpByte1 >= 200 && rawRtpByte1 <= 204);
    if (m_droppedNonH264RtpTotal <= 16 || (m_droppedNonH264RtpTotal % 250) == 0) {
      qWarning() << "[H264][feedRtp] 丢弃非协商媒体包 stream=" << m_streamTag
                 << " expectPt=" << m_expectedRtpPayloadType << " parsedPt=" << payloadType
                 << " rawB1=0x" << Qt::hex << static_cast<unsigned>(rawRtpByte1) << Qt::dec
                 << (likelyRtcp ? " ★疑似RTCP(SR/RR/SDES/BYE/APP)误入，RFC3550 §6.4" : "")
                 << " len=" << len << " lifecycleId=" << lifecycleId;
    }
    return;
  }

  {
    ++m_statsPacketsInWindow;

    if (!m_rtpSeqInitialized) {
      m_rtpNextExpectedSeq = seq;
      m_rtpSeqInitialized = true;
      m_lastSeenSsrc = curSsrc;
      qInfo() << "[H264][feedRtp] ★★★ RTP 序列初始化 ★★★ stream=" << m_streamTag
              << " firstSeq=" << seq << " ts=" << ts << " marker=" << marker << " ssrc=0x"
              << Qt::hex << curSsrc << Qt::dec << " rtpV=" << rtpVersion << " pt=" << payloadType;
    }

    if (m_rtpSeqInitialized && m_lastSeenSsrc != 0 && curSsrc != m_lastSeenSsrc) {
      qWarning() << "[H264][RTP][Diag] SSRC 变化 stream=" << m_streamTag << " was=0x" << Qt::hex
                 << m_lastSeenSsrc << " now=0x" << curSsrc << Qt::dec << " seq=" << seq
                 << " ★ RFC3550 新同步源常伴随 seq 突变；若频繁出现查 WebRTC track/多路复用";
    }

    // ★ 记录 RTP seq 历史（环形缓冲），用于大幅跳跃时输出跳跃前 10 包上下文
    {
      RtpSeqHistEntry &e = m_rtpSeqHist[m_rtpSeqHistIdx];
      e.seq = static_cast<quint16>(seq);
      e.rtpTs = static_cast<quint32>(ts);
      e.wallMs = now;
      e.marker = static_cast<quint8>(marker);
      e.payloadType = static_cast<quint8>(payloadType);
      m_rtpSeqHistIdx = (m_rtpSeqHistIdx + 1) % kRtpSeqHistSize;
      if (m_rtpSeqHistCount < kRtpSeqHistSize)
        ++m_rtpSeqHistCount;
    }

    if (seq == m_rtpNextExpectedSeq) {
      // ★ 关键修复：先处理当前包，再排空缓冲
      // 旧逻辑错误：先 drainRtpBuffer()，导致缓冲包比当前包晚一帧处理
      m_rtpNextExpectedSeq = static_cast<quint16>(seq + 1);
      m_lastSeenSsrc = curSsrc;
      // ── ★★★ 端到端追踪：RTP 包 seq 进入解码器 ★★★ ───────────────────────
      static QHash<QString, int> s_pktCountPerStream;
      const QString tag = m_streamTag.isEmpty() ? "unknown" : m_streamTag;
      ++s_pktCountPerStream[tag];
      if (s_pktCountPerStream[tag] <= 20) {
        qInfo() << "[H264][feedRtp] ★ RTP包seq=" << seq << " → 解码器 stream=" << tag
                << " pktCount=" << s_pktCountPerStream[tag] << " ts=" << ts << " marker=" << marker
                << " payloadLen=" << (len - kRtpHeaderMinLen);
      }
      processRtpPacket(data, len);
      drainRtpBuffer();  // 排空已收到的连续包
      return;
    }

    int16_t diff = static_cast<int16_t>(seq - m_rtpNextExpectedSeq);
    // 256 曾被用作边界导致 diff==256 误走「seq 大跳」清空缓冲（RFC3550 下偶发乱序/突发可达数百包）
    if (diff > 0 && diff < 512) {
      // 乱序但可恢复：缓冲起来
      if (m_rtpBuffer.size() >= kRtpReorderBufferMax) {
        qWarning() << "[H264][feedRtp] 缓冲已满，丢弃最旧的包来接收 seq=" << seq
                   << " bufSize=" << m_rtpBuffer.size() << " max=" << kRtpReorderBufferMax;
        // 丢弃最早的一个
        QList<quint16> keys = m_rtpBuffer.keys();
        if (!keys.isEmpty()) {
          std::sort(keys.begin(), keys.end());
          m_rtpBuffer.remove(keys.first());
        }
      }
      m_rtpBuffer[seq] = QByteArray(reinterpret_cast<const char *>(data), static_cast<int>(len));
      m_lastRtpPacketTime = static_cast<qint64>(now);
      ++m_reorderEnqueueInWindow;
      // ── 诊断：乱序包进入缓冲 ─────────────────────────────────────────────
      static QHash<QString, int> s_outOfOrderCount;
      const QString tag2 = m_streamTag.isEmpty() ? "unknown" : m_streamTag;
      int ooCount = ++s_outOfOrderCount[tag2];
      
      bool wasPending = m_pendingNacks.contains(seq);
      if (wasPending) {
          qInfo() << "[H264][" << m_streamTag << "][NACK][Recovered] ★ 成功通过 NACK 补回 RTP 包 seq=" << seq
                  << " expected=" << m_rtpNextExpectedSeq;
      }

      if (ooCount <= 10) {
        qInfo() << "[H264][feedRtp] 乱序包缓冲 stream=" << tag2 << " seq=" << seq
                << " expected=" << m_rtpNextExpectedSeq << " diff=" << diff
                << " bufSize=" << m_rtpBuffer.size() << " ooCount=" << ooCount;
      }
      // ★ WHY5 修复：检测到乱序即触发 NACK 检查
      checkAndRequestNacks();
      return;
    }
    if (diff < 0 && diff > -512) {
      ++m_dupRtpIgnoredInWindow;
      // ★ WHY5 修复：如果是重传包，则不应直接 ignore，而是在 drainRtpBuffer 中处理
      // 但实际上 m_rtpBuffer 不会包含 diff < 0 的包，因为 m_rtpNextExpectedSeq 已经过了它们
      // 除非是极度延迟的重传。这里我们记录一下。
      if (m_pendingNacks.contains(seq)) {
          qInfo() << "[H264][" << m_streamTag << "][NACK][Recovered] ★ 收到 NACK 重传包(但已过期) seq=" << seq
                  << " expected=" << m_rtpNextExpectedSeq;
      }
      return;
    }
    // 大幅跳跃（重连/乱序）：以新序列为基准，清空旧缓冲
    // ── 诊断：打印跳跃时间戳，便于与 ZLM 侧日志对比是哪一刻断的 ───────────
    QString hexDump;
    const int dumpN = qMin(static_cast<int>(len), 24);
    for (int i = 0; i < dumpN; ++i)
      hexDump += QString::asprintf("%02X ", static_cast<unsigned char>(data[i]));
    qWarning()
        << "[H264][RTP][Diag] RTP 序列大幅跳跃: oldExpected=" << m_rtpNextExpectedSeq
        << " newSeq=" << seq << " diff=" << diff << " bufSize=" << m_rtpBuffer.size()
        << " now=" << now << "ms"
        << " ssrc=0x" << Qt::hex << curSsrc << Qt::dec << " lastSsrc=0x" << Qt::hex
        << m_lastSeenSsrc << Qt::dec << " rtpV=" << rtpVersion << " pt=" << payloadType
        << " hdrHex[" << dumpN << "]=" << hexDump.trimmed()
        << "【RFC3550: 合法 seq 为 16-bit 模 65536；若 ssrc/pt 异常或非 V=2 则可能非 RTP 误入】";
    // ★ 输出跳跃前 10 包历史（seq+rtpTs+wallMs），便于与 ZLM/CARLA 发送侧时间戳对比
    if (m_rtpSeqHistCount > 0) {
      QString histLine =
          QStringLiteral("[H264][RTP][SeqHist] stream=%1 跳跃前最后%2包历史（从旧到新）: ")
              .arg(m_streamTag)
              .arg(m_rtpSeqHistCount);
      // 环形缓冲：m_rtpSeqHistIdx 指向下一个写入位置（最旧）
      for (int i = 0; i < m_rtpSeqHistCount; ++i) {
        int idx = (m_rtpSeqHistIdx - m_rtpSeqHistCount + i + kRtpSeqHistSize) % kRtpSeqHistSize;
        const RtpSeqHistEntry &e = m_rtpSeqHist[idx];
        histLine += QStringLiteral("[seq=%1 ts=%2 wall=%3 M=%4 pt=%5] ")
                        .arg(e.seq)
                        .arg(e.rtpTs)
                        .arg(e.wallMs)
                        .arg(e.marker)
                        .arg(e.payloadType);
      }
      histLine +=
          QStringLiteral("★ 对比 ZLM/carla-bridge 发送侧 seq/ts，确认是发送端 reset 还是网络丢包");
      qWarning().noquote() << histLine;
    }
    m_rtpBuffer.clear();
    m_rtpNextExpectedSeq = static_cast<quint16>(seq + 1);
    m_lastSeenSsrc = curSsrc;
    ++m_seqJumpResyncInWindow;
    emitKeyframeSuggestThrottled("rtp_seq_resync");
    processRtpPacket(data, len);
    m_needKeyframe = true;
    m_framesSinceKeyframeRequest = 0;
  }
}

void H264Decoder::drainRtpBuffer() {
  // 连续排空
  int drained = 0;
  int loopCount = 0;
  while (m_rtpBuffer.contains(m_rtpNextExpectedSeq)) {
    loopCount++;
    if (loopCount > 500) {
      qWarning() << "[H264][drain] 循环次数过多，强制退出 drained=" << drained
                 << " bufSize=" << m_rtpBuffer.size();
      break;
    }
    QByteArray pkt = m_rtpBuffer.take(m_rtpNextExpectedSeq);
    m_rtpNextExpectedSeq = static_cast<quint16>(m_rtpNextExpectedSeq + 1);
    processRtpPacket(reinterpret_cast<const uint8_t *>(pkt.constData()), pkt.size());
    drained++;

    // ★ 防止单次排空过多导致卡顿
    if (drained > 100) {
      qWarning() << "[H264][drain] 单次排空过多，强制退出 drained=" << drained
                 << " bufSize=" << m_rtpBuffer.size();
      break;
    }
  }

  // ★ 缓冲溢出处理
  if (m_rtpBuffer.size() > kRtpReorderBufferMax) {
    QList<quint16> keys = m_rtpBuffer.keys();
    if (keys.isEmpty()) {
      m_rtpBuffer.clear();
      return;
    }

    std::sort(keys.begin(), keys.end());
    quint16 minSeq = keys.first();
    quint16 maxSeq = keys.last();

    int16_t gap = static_cast<int16_t>(minSeq - m_rtpNextExpectedSeq);

    // ★ 智能判断：小间隙跳过，大间隙重置
    if (gap > 0 && gap < 200) {
      // 少量丢包：跳过缺失部分
      qDebug() << "[H264][drain] 跳过丢失 RTP 包，从 seq" << m_rtpNextExpectedSeq << "跳到"
               << minSeq << "(gap=" << gap << ")";

      // ★ WHY5 修复：跳过前请求一下 NACK，万一还能补回来
      checkAndRequestNacks();

      m_rtpNextExpectedSeq = minSeq;
      // m_needKeyframe = true; // 移除：由 flushPending 根据 bitstreamIncomplete 决定是否请求 IDR
      // m_framesSinceKeyframeRequest = 0;

      // ★ 关键修复：不要清空当前帧，允许 decodeCompleteFrame 尝试纠错解码（FFmpeg Error Concealment）
      // 理由：高丢包环境下，如果每次丢包都清空帧并重新请求 IDR，会导致永远在等待 IDR 的黑屏循环中。
      // m_pendingFrame = PendingFrame(); 
      // m_fuBuffer.clear();
      // m_fuStarted = false;

      // 递归排空
      drainRtpBuffer();
    } else {
      // 大量丢包或乱序：清空重建
      qWarning() << "[H264][drain] RTP 缓冲严重错乱，清空重建"
                 << " bufSize=" << m_rtpBuffer.size() << " expect=" << m_rtpNextExpectedSeq
                 << " minSeq=" << minSeq << " maxSeq=" << maxSeq << " gap=" << gap;

      // 从最新的包开始
      m_rtpNextExpectedSeq = static_cast<quint16>(maxSeq + 1);
      m_rtpBuffer.clear();
      m_pendingFrame = PendingFrame();
      m_fuBuffer.clear();
      m_fuStarted = false;
      m_needKeyframe = true;
      m_framesSinceKeyframeRequest = 0;
    }
  }
}

void H264Decoder::processRtpPacket(const uint8_t *data, size_t len) {
  if (len <= kRtpHeaderMinLen)
    return;

  size_t payloadOff = kRtpHeaderMinLen;
  QString herr;
  if (!rtpComputePayloadOffset(data, len, &payloadOff, &herr)) {
    ++m_rtpHdrParseFailInWindow;
    static QHash<QString, int> s_hdrFailLog;
    if (++s_hdrFailLog[m_streamTag] <= 20 || (s_hdrFailLog[m_streamTag] % 120) == 0)
      qWarning() << "[H264][RTP][Hdr] payload 偏移解析失败 stream=" << m_streamTag << " len=" << len
                 << " err=" << herr << " ★RFC3550 CSRC/extension 与长度不一致或非 RTP";
    return;
  }
  if (payloadOff != kRtpHeaderMinLen) {
    ++m_rtpNonMinimalHdrInWindow;
    static QHash<QString, int> s_nmLog;
    if (++s_nmLog[m_streamTag] <= 30 || (s_nmLog[m_streamTag] % 200) == 0)
      qWarning() << "[H264][RTP][Hdr] payloadOffset=" << payloadOff
                 << " (非12) stream=" << m_streamTag << " len=" << len
                 << " ★含 CSRC 或 RTP extension，须用动态偏移取 H264 payload";
  }
  if (payloadOff > len)
    return;

  m_lastRtpPacketTime = QDateTime::currentMSecsSinceEpoch();

  quint16 seq = rtpSeqNum(data);
  quint32 ts = rtpTimestamp(data);
  bool marker = rtpMarkerBit(data);

  m_lastRtpSeq = seq;
  m_rtpPacketsProcessed++;

  processRtpPayload(seq, ts, marker, data + payloadOff, len - payloadOff);
}

void H264Decoder::processRtpPayload(quint16 rtpSeq, quint32 rtpTs, bool marker,
                                    const uint8_t *payload, size_t payloadLen) {
  {
    if (payloadLen < 1) {
      qWarning() << "[H264][payload] 空 payload rtpSeq=" << rtpSeq;
      return;
    }

    if (payloadLen < 2) {
      qWarning() << "[H264][payload][WARN] payload 长度不足 rtpSeq=" << rtpSeq
                 << " payloadLen=" << payloadLen << "，忽略";
      return;
    }

    uint8_t nalByte = payload[0];
    int nalType = nalByte & 0x1f;

    if (nalType == 28) {
      if (payloadLen < 2) {
        qWarning() << "[H264][payload][WARN] FU-A 包太短 rtpSeq=" << rtpSeq
                   << " len=" << payloadLen;
        return;
      }
      uint8_t fuHeader = payload[1];
      bool start = (fuHeader & kFuAStart) != 0;
      bool end = (fuHeader & kFuAEnd) != 0;
      int fuNalType = fuHeader & 0x1f;

      if (start) {
        if (!m_fuStarted || m_fuNalType != fuNalType) {
          // NAL type 变化或未开始，重置
          m_fuBuffer.clear();
        }
        m_fuNalType = fuNalType;
        m_fuBuffer.clear();
        m_fuBuffer.append(static_cast<char>((nalByte & 0xe0) | fuNalType));
        m_fuBuffer.append(reinterpret_cast<const char *>(payload + 2),
                          static_cast<int>(payloadLen - 2));
        m_fuStarted = true;
      } else if (m_fuStarted) {
        m_fuBuffer.append(reinterpret_cast<const char *>(payload + 2),
                          static_cast<int>(payloadLen - 2));
      } else {
        // FU-A 中间/结束包，但 m_fuStarted=false（丢掉了 start）
        qDebug() << "[H264][payload] FU-A 片段丢失 start rtpSeq=" << rtpSeq
                 << " fuNalType=" << fuNalType << " ignored";
      }

      if (end && m_fuStarted) {
        const uint8_t *nal = reinterpret_cast<const uint8_t *>(m_fuBuffer.constData());
        size_t nalLen = m_fuBuffer.size();
        // ── ★★★ 端到端追踪：FU-A 组装完成，准备送入帧缓冲 ★★★ ──────────────
        static QSet<QString> s_loggedFuStreams;
        if (!s_loggedFuStreams.contains(m_streamTag)) {
          s_loggedFuStreams.insert(m_streamTag);
          qInfo() << "[H264][" << m_streamTag << "] FU-A 组装完成"
                  << " fuNalType=" << fuNalType << " nalLen=" << nalLen << " ts=" << rtpTs
                  << " marker=" << marker << " ★ 对比 emitDecoded frameId 确认 FU-A→帧缓冲链路";
        }
        {
          bool handled = handleParameterSet(nal, nalLen);
          if (!handled) {
            // ── ★★★ 端到端追踪：NAL 送入帧缓冲 ★★★ ──────────────────────
            appendNalToFrame(rtpTs, rtpSeq, nal, nalLen, marker);
          } else {
            // ── SPS/PPS 参数集，跳过帧缓冲 ────────────────────────────────────
            static QSet<QString> s_loggedSpsPps;
            if (!s_loggedSpsPps.contains(m_streamTag)) {
              s_loggedSpsPps.insert(m_streamTag);
              qInfo() << "[H264][" << m_streamTag << "] SPS/PPS 已处理，FU-A type=" << fuNalType
                      << " ★ 对比 emitDecoded 确认解码器参数就绪";
            }
          }
        }
        m_fuBuffer.clear();
        m_fuStarted = false;
      }
      return;
    }

    if (m_fuStarted) {
      // 收到非 FU-A NAL，但 FU-A 还未结束，清除 FU 缓冲
      static QHash<QString, int> s_fuBreakSeq;
      const int n = ++s_fuBreakSeq[m_streamTag];
      if (n <= 8 || (n % 40 == 0)) {
        qWarning() << "[H264][payload][FU-A_break] stream=" << m_streamTag << " count=" << n
                   << " rtpSeq=" << rtpSeq << " nalType=" << nalType
                   << " needKeyframe=" << m_needKeyframe
                   << " ★ 常与 RTP 丢包/错序/混入非视频包有关，易导致花屏与闪烁";
      }
      m_fuBuffer.clear();
      m_fuStarted = false;
    }

    if (nalType == 24) {
      size_t offset = 1;
      std::vector<std::pair<const uint8_t *, size_t>> nalList;
      while (offset + 2 <= payloadLen) {
        uint16_t size = (static_cast<uint16_t>(payload[offset]) << 8) |
                        static_cast<uint16_t>(payload[offset + 1]);
        offset += 2;
        if (offset + size > payloadLen) {
          qWarning() << "[H264][payload][WARN] STAP-A 长度字段越界 rtpSeq=" << rtpSeq
                     << " size=" << size << " remaining=" << (payloadLen - offset);
          break;
        }
        const uint8_t *nal = payload + offset;
        {
          if (!handleParameterSet(nal, size)) {
            nalList.push_back({nal, size});
          }
        }
        offset += size;
      }
      for (size_t i = 0; i < nalList.size(); ++i) {
        bool isLast = (i == nalList.size() - 1);
        {
          appendNalToFrame(rtpTs, rtpSeq, nalList[i].first, nalList[i].second, isLast && marker);
        }
      }
      return;
    }

    if (nalType >= 1 && nalType <= 23) {
      {
        if (!handleParameterSet(payload, payloadLen)) {
          appendNalToFrame(rtpTs, rtpSeq, payload, payloadLen, marker);
        }
      }
      return;
    }

    // 其他 NAL type：暂时忽略
    qDebug() << "[H264][payload] 忽略未知 NAL type rtpSeq=" << rtpSeq << " nalType=" << nalType;
  }
}

// ============================================================================
// 帧聚合
// ============================================================================
void H264Decoder::appendNalToFrame(quint32 ts, quint16 rtpSeq, const uint8_t *nal, size_t nalLen, bool marker) {
  if (nalLen < 1) {
    qWarning() << "[H264][appendNal] nalLen=0，忽略";
    return;
  }

  int nalType = nal[0] & 0x1f;
  if (nalType == 5 || nalType == 7 || nalType == 8) {
      m_pendingFrame.hasKeyframe = true;
  }
  static QSet<int> s_reportedNalTypes;
  if (s_reportedNalTypes.size() < 20 && !s_reportedNalTypes.contains(nalType)) {
    s_reportedNalTypes.insert(nalType);
    qDebug() << "[H264][appendNal] 首次见到 NAL type=" << nalType << " ts=" << ts
             << " marker=" << marker;
  }

  // ★ 时间戳切换处理
  if (m_pendingFrame.timestamp != 0 && m_pendingFrame.timestamp != ts) {
    if (!m_pendingFrame.nalUnits.empty()) {
      // ★ 宽容处理：只要有 slice 就尝试解码
      int sliceCount = 0;
      for (const auto &n : m_pendingFrame.nalUnits) {
        if (n.isEmpty())
          continue;
        int t = static_cast<uint8_t>(n[0]) & 0x1f;
        if (t == 1 || t == 5)
          sliceCount++;
      }

      if (sliceCount > 0) {
        m_pendingFrame.complete = true;
        flushPendingFrame();
      }
    }
    m_pendingFrame = PendingFrame();
  }

  m_pendingFrame.timestamp = ts;
  m_pendingFrame.nalUnits.emplace_back(reinterpret_cast<const char *>(nal),
                                       static_cast<int>(nalLen));
  m_pendingFrame.rtpSeqs.push_back(rtpSeq); // v4: 假设 appendNalToFrame 参数列表已含 rtpSeq，或调用处含

  if (marker) {
    m_pendingFrame.closedByRtpMarker = true;
    m_pendingFrame.complete = true;
    flushPendingFrame();
  }
}

void H264Decoder::flushPendingFrame() {
  {
    if (m_pendingFrame.nalUnits.empty()) {
      m_pendingFrame = PendingFrame();
      return;
    }

    bool bitstreamIncomplete = false;
    int frameHoleCount = 0;
    QString holes;
    if (!m_pendingFrame.rtpSeqs.empty()) {
        std::vector<quint16> sortedSeqs = m_pendingFrame.rtpSeqs;
        std::sort(sortedSeqs.begin(), sortedSeqs.end());
        for (size_t i = 1; i < sortedSeqs.size(); ++i) {
            int16_t diff = static_cast<int16_t>(sortedSeqs[i] - sortedSeqs[i-1]);
            if (diff > 1) {
                frameHoleCount += (diff - 1);
                if (holes.length() < 128) {
                    if (!holes.isEmpty()) holes += QLatin1Char(',');
                    holes += QString("%1..%2").arg(sortedSeqs[i-1] + 1).arg(sortedSeqs[i] - 1);
                }
            }
        }
        bitstreamIncomplete = (frameHoleCount > 0);
    }

    bool hasIdr = false;
    int sliceCount = 0;
    for (const auto &nal : m_pendingFrame.nalUnits) {
      if (nal.isEmpty())
        continue;
      int t = static_cast<uint8_t>(nal[0]) & 0x1f;
      if (t == 5)
        hasIdr = true;
      if (t == 1 || t == 5)
        sliceCount++;
    }

    if (sliceCount == 0) {
      qDebug() << "[H264][flushPending] 无有效 slice，丢弃 ts=" << m_pendingFrame.timestamp;
      m_pendingFrame = PendingFrame();
      return;
    }

    // ★ 学习 slice 数量（仅从第一个完美的完整帧开始）。
    if (m_expectedSliceCount == 0 && m_pendingFrame.complete && !bitstreamIncomplete) {
      m_expectedSliceCount = sliceCount;
      qInfo() << "[H264][" << m_streamTag << "][FlushPath] learned slicesPerFrame=" << m_expectedSliceCount
              << " pendingNalUnits=" << m_pendingFrame.nalUnits.size()
              << " complete=1 bitstreamIncomplete=0";
    }

    // ★★★ 5-WHY 闭环修复：统一 StripeDefense (WHY3) ★★★
    // 之前只在「时间戳切换」路径有此检查，忽略了「Marker 位触发」路径，导致不完整帧漏过产生条纹。
    if (m_expectedSliceCount > 0 && sliceCount < m_expectedSliceCount && !hasIdr) {
        static QHash<QString, int> s_stripeDefenseLog;
        if (++s_stripeDefenseLog[m_streamTag] <= 20 || (s_stripeDefenseLog[m_streamTag] % 100 == 0)) {
            qWarning() << "[H264][" << m_streamTag << "][StripeDefense][REJECT] 丢弃 slice 不足的 P 帧"
                       << " ts=" << m_pendingFrame.timestamp << " slices=" << sliceCount << "/" << m_expectedSliceCount
                       << " | bitstreamIncomplete=" << (bitstreamIncomplete ? 1 : 0)
                       << " ★ 缺失 slice 会触发 FFmpeg 条纹恢复，此处强行拦截";
        }
        m_needKeyframe = true;
        m_framesSinceKeyframeRequest = 0;
        m_pendingFrame = PendingFrame();
        return;
    }

    // ★ 额外诊断：如果 slices 超过预期，可能学习值太小了
    if (m_expectedSliceCount > 0 && sliceCount > m_expectedSliceCount) {
        static QHash<QString, int> s_sliceOverLog;
        if (++s_sliceOverLog[m_streamTag] <= 10) {
            qInfo() << "[H264][" << m_streamTag << "][StripeDefense][INFO] sliceCount (" << sliceCount 
                    << ") > exp (" << m_expectedSliceCount << ")，可能需要重新学习";
        }
    }

    // 首次必须等 IDR
    if (!m_haveDecodedKeyframe && !hasIdr) {
      m_droppedPFrameCount++;
      if (m_droppedPFrameCount <= 5 || m_droppedPFrameCount % 100 == 0)
        qDebug() << "[H264] 丢弃帧(首次等IDR) #" << m_droppedPFrameCount
                 << "ts=" << m_pendingFrame.timestamp << "sliceCount=" << sliceCount;
      m_pendingFrame = PendingFrame();
      return;
    }

    // ★ 丢包恢复策略：
    if (m_needKeyframe) {
      if (hasIdr) {
        // bitstreamIncomplete 时下方仍会 IncompleteFrameDrop，勿误报「恢复完成」
        if (!bitstreamIncomplete) {
          qInfo() << "[H264] 收到 IDR，丢包恢复完成 等待了" << m_framesSinceKeyframeRequest << "帧"
                  << " ts=" << m_pendingFrame.timestamp;
        } else {
          qWarning() << "[H264][" << m_streamTag << "][IdrButHoles] 收到 IDR 但本帧 RTP 仍有空洞，"
                     << "不能算恢复完成 等待了" << m_framesSinceKeyframeRequest << "帧"
                     << " ts=" << m_pendingFrame.timestamp << " holeCount=" << frameHoleCount
                     << " | 将走 IncompleteFrameDrop 或允许首屏不完整路径";
        }
        // 勿清 SPS/PPS：紧随 decodeCompleteFrame 会 ensureDecoder()；专用组合 API（非 recover*）
        idrRecoveryFlushCodecOnly();
        m_needKeyframe = false;
        m_framesSinceKeyframeRequest = 0;
      } else {
        m_framesSinceKeyframeRequest++;
        if (m_framesSinceKeyframeRequest <= 30 || (m_framesSinceKeyframeRequest % 50 == 0)) {
          qWarning() << "[H264][" << m_streamTag << "][StripeDefense] 丢弃 P 帧(等待 IDR 恢复中) #" << m_framesSinceKeyframeRequest
                   << " ts=" << m_pendingFrame.timestamp << " sliceCount=" << sliceCount
                   << " hasKeyframe=" << (m_pendingFrame.hasKeyframe ? 1 : 0)
                   << " bitstreamIncomplete=" << (bitstreamIncomplete ? 1 : 0)
                   << " | ★ 根本原因分析：丢包且无 NACK 重传，导致此流持续处于等待 IDR 状态";
        }
        m_pendingFrame = PendingFrame();
        return;
      }
    }

    if (hasIdr) {
      m_haveDecodedKeyframe = true;
    }

    // ★ v4: RTP 序列连续性审计（已在函数开头计算 bitstreamIncomplete）
    if (!m_pendingFrame.rtpSeqs.empty()) {
        m_statsIncompleteHoleTotalInWindow += frameHoleCount;
        if (bitstreamIncomplete) {
            const int expectedPkts =
                (m_pendingFrame.closedByRtpMarker
                     ? (int)(m_pendingFrame.rtpSeqs.size() + frameHoleCount)
                     : -1);
            qWarning() << "[H264][" << m_streamTag << "][FrameHole] ★ 检测到当前帧 RTP 序列空洞！"
                       << " ts=" << m_pendingFrame.timestamp << " holeCount=" << frameHoleCount
                       << " recvPkts=" << m_pendingFrame.rtpSeqs.size() << " totalExpected=" << expectedPkts
                       << " holes=[" << holes << "]"
                       << " ★ 缺失的 RTP 包会导致 NALU 不完整，从而触发 FFmpeg 条纹恢复";
        }
    }

    // ★★★ 5-WHY 根本原因修复（WHY3）★★★
    //
    // 条状完整因果链（从日志分析得出）：
    //   WHY1 症状：视频有水平条状花屏
    //   WHY2 直接原因：emitDecodedFrames() 对 bitstreamIncomplete=true 的帧无条件 emit
    //   WHY3 设计缺陷：marker-bit flush 路径缺少丢帧保护，StripeDefense 只在时间戳切换路径
    //   WHY4 上游原因：每帧有 1-9 个 RTP 包丢失（空洞），4 路并发 × 5fps × ~20pkt/frame
    //   WHY5 根本原因：WebRTC UDP 无 NACK 重传（reorderEnqueued=0），发送端 burst 导致
    //                  接收端缓冲溢出，丢包不可恢复
    //
    // 此处修复 WHY3：bitstreamIncomplete=true 时默认丢弃帧并请求 IDR，
    // 防止 FFmpeg Error Concealment 产生条状。
    // ★ 关键例外：仅在流启动阶段（!m_haveDecodedKeyframe）允许不完整 IDR，否则会陷入无限黑屏。
    // 一旦流已正常启动，不完整 IDR 也应丢弃以防条状污染后续帧。
    if (bitstreamIncomplete) {
        bool shouldDrop = false;
        if (!m_pendingFrame.hasKeyframe) {
            // P 帧不完整必丢
            shouldDrop = true;
        } else {
            // IDR 帧不完整：若已有健康参考，则丢弃以保质量；若无参考，则允许以保首屏
            if (m_haveDecodedKeyframe) {
                shouldDrop = true;
            }
        }

        const bool allowIncomplete =
            qEnvironmentVariableIntValue("CLIENT_H264_ALLOW_INCOMPLETE_FRAMES") != 0;

        if (shouldDrop && !allowIncomplete) {
            ++m_statsIncompleteFrameDropInWindow;
            m_needKeyframe = true;
            m_framesSinceKeyframeRequest = 0;
            static QHash<QString, int> s_incompleteDropLog;
            const int logN = ++s_incompleteDropLog[m_streamTag];
            if (logN <= 20 || (logN % 100 == 0)) {
                qWarning().noquote()
                    << QStringLiteral(
                           "[H264][%1][IncompleteFrameDrop] ★★★ 丢弃不完整帧（防条状） ★★★"
                           " type=%2 holeCount=%3 recvPkts=%4 slices=%5 ts=%6 drop#%7"
                           " haveDecodedKeyframe=%8"
                           " | 已设 m_needKeyframe=true 等待新 IDR | 设 CLIENT_H264_ALLOW_INCOMPLETE_FRAMES=1 可跳过此保护"
                           " | haveDecodedKeyframe=1 时不完整 IDR 必丢→易陷入「IDR 到但永远不完整」循环，查 NACK/ZLM/带宽")
                           .arg(m_streamTag)
                           .arg(m_pendingFrame.hasKeyframe ? "IDR" : "P")
                           .arg(frameHoleCount)
                           .arg(m_pendingFrame.rtpSeqs.size())
                           .arg(sliceCount)
                           .arg(m_pendingFrame.timestamp)
                           .arg(logN)
                           .arg(m_haveDecodedKeyframe ? 1 : 0);
            }
            emitKeyframeSuggestThrottled("bitstream_incomplete_drop");
            m_pendingFrame = PendingFrame();
            return;
        } else if (bitstreamIncomplete) {
            ++m_statsIncompleteFrameEmitInWindow;
            static QHash<QString, int> s_incompleteEmitLog;
            const int emitN = ++s_incompleteEmitLog[m_streamTag];
            if (emitN <= 5 || (emitN % 50 == 0)) {
                qWarning() << "[H264][" << m_streamTag
                           << "][IncompleteFrameEmit] ★ 允许 emit 不完整帧（首屏或测试模式）"
                           << " type=" << (m_pendingFrame.hasKeyframe ? "IDR" : "P")
                           << " emit#" << emitN << " holeCount=" << frameHoleCount
                           << " ★ 此模式下 IDR 不完整会导致持久条状";
            }
        }
    }

    {
      decodeCompleteFrame(m_pendingFrame.nalUnits, bitstreamIncomplete);
    }
    m_pendingFrame = PendingFrame();
  }
}

QByteArray H264Decoder::buildNackPacket(quint32 senderSsrc, quint32 mediaSsrc, const std::vector<quint16> &lostSeqs) {
  if (lostSeqs.empty())
    return {};

  // 将序列号分组为 PID + BLP
  // BLP: bitmask of following lost packets. bit 0 is PID+1, bit 15 is PID+16.
  struct NackBlock {
    quint16 pid;
    quint16 blp;
  };
  std::vector<NackBlock> blocks;

  std::vector<quint16> sortedLost = lostSeqs;
  std::sort(sortedLost.begin(), sortedLost.end());

  for (size_t i = 0; i < sortedLost.size(); ) {
    NackBlock b;
    b.pid = sortedLost[i];
    b.blp = 0;
    i++;
    while (i < sortedLost.size()) {
      int diff = static_cast<uint16_t>(sortedLost[i] - b.pid);
      if (diff > 0 && diff <= 16) {
        b.blp |= (1 << (diff - 1));
        i++;
      } else {
        break;
      }
    }
    blocks.push_back(b);
  }

  // RTCP Header: V=2, P=0, FMT=1, PT=205, Length
  // Payload: Sender SSRC (4), Source SSRC (4), Blocks (4 * n)
  int totalLen = 8 + 4 + 4 + (static_cast<int>(blocks.size()) * 4);
  QByteArray rtcp(totalLen, '\0');
  uint8_t *p = reinterpret_cast<uint8_t *>(rtcp.data());

  p[0] = 0x81; // V=2, P=0, FMT=1
  p[1] = 205;  // PT=RTPFB
  uint16_t words = static_cast<uint16_t>((totalLen / 4) - 1);
  p[2] = (words >> 8) & 0xFF;
  p[3] = words & 0xFF;

  // Sender SSRC
  p[4] = (senderSsrc >> 24) & 0xFF;
  p[5] = (senderSsrc >> 16) & 0xFF;
  p[6] = (senderSsrc >> 8) & 0xFF;
  p[7] = senderSsrc & 0xFF;

  // Media SSRC
  p[8] = (mediaSsrc >> 24) & 0xFF;
  p[9] = (mediaSsrc >> 16) & 0xFF;
  p[10] = (mediaSsrc >> 8) & 0xFF;
  p[11] = mediaSsrc & 0xFF;

  p += 12;
  for (const auto &b : blocks) {
    p[0] = (b.pid >> 8) & 0xFF;
    p[1] = b.pid & 0xFF;
    p[2] = (b.blp >> 8) & 0xFF;
    p[3] = b.blp & 0xFF;
    p += 4;
  }

  return rtcp;
}

void H264Decoder::checkAndRequestNacks() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const char *earlyReason = nullptr;
    if (!m_rtpSeqInitialized)
      earlyReason = "seq_uninit";
    else if (m_lastSeenSsrc == 0)
      earlyReason = "ssrc_zero";
    else if (m_rtpBuffer.isEmpty())
      earlyReason = "reorder_buf_empty";

    if (earlyReason) {
      static QHash<QString, qint64> s_lastNackEarlyLogMs;
      const QString k =
          m_streamTag + QLatin1Char('#') + QString::fromLatin1(earlyReason);
      const qint64 last = s_lastNackEarlyLogMs.value(k, 0);
      // 降低 noise，改为 10s 一次
      if (now - last >= 10000) {
        s_lastNackEarlyLogMs.insert(k, now);
        const char *hint =
            std::strcmp(earlyReason, "seq_uninit") == 0
                ? "尚未收到首包，无法 NACK"
                : (std::strcmp(earlyReason, "ssrc_zero") == 0
                       ? "SSRC 未建立"
                       : "重排缓冲空：无「后续乱序包」则本函数无法扫描 seq 空洞发 NACK");
        qInfo() << "[H264][" << m_streamTag << "][NackCheck][skip][throttled5s] reason=" << earlyReason
                << " nextExpected=" << m_rtpNextExpectedSeq << " bufPkts=" << m_rtpBuffer.size()
                << " ssrc=0x" << Qt::hex << m_lastSeenSsrc << Qt::dec << " |" << hint
                << "；对照 [Client][WebRTC][IngressStats5s]、[H264][FrameHole]";
      }
      return;
    }

    // 基础限频：5ms 内不重复发 NACK
    if (now - m_lastNackSentMs < 5)
        return;

    std::vector<quint16> lostToRequest;
    
    // WHY5 增强：即使 reorder_buf_empty，若 nextExpected 停滞 > 100ms，说明后续包全丢，强发 PLI 或 NACK
    if (m_rtpSeqInitialized && now - m_lastRtpPacketTime > 100) {
        emitKeyframeSuggestThrottled("rtp_stuck_100ms");
    }

    // 找出从 m_rtpNextExpectedSeq 到当前已收到最大 seq 之间的所有空洞
    // 注意：m_rtpBuffer 中存储的是未来的乱序包
    if (!m_rtpBuffer.isEmpty()) {
        QList<quint16> keys = m_rtpBuffer.keys();
        std::sort(keys.begin(), keys.end());
        quint16 maxReceived = keys.last();
        
        for (quint16 s = m_rtpNextExpectedSeq; s != maxReceived; ++s) {
            if (!m_rtpBuffer.contains(s)) {
                // 检查是否在等待重传中
                auto it = m_pendingNacks.find(s);
                bool shouldRequest = false;
                if (it == m_pendingNacks.end()) {
                    shouldRequest = true;
                } else {
                    // 重试逻辑：150ms 后重试，最多 3 次
                    if (now - it->lastRequestMs > 150 && it->retryCount < 3) {
                        shouldRequest = true;
                    }
                }
                
                if (shouldRequest) {
                    lostToRequest.push_back(s);
                    NackRequest &req = m_pendingNacks[s];
                    req.lastRequestMs = now;
                    req.retryCount++;
                }
            }
        }
    }

    if (!lostToRequest.empty()) {
        // 限制单次 NACK 包大小，避免过长
        if (lostToRequest.size() > 50) {
            lostToRequest.resize(50);
        }
        
        QByteArray nackPkt = buildNackPacket(0, m_lastSeenSsrc, lostToRequest);
        if (!nackPkt.isEmpty()) {
            m_lastNackSentMs = now;
            qInfo() << "[H264][" << m_streamTag << "][NACK] ★ 发送 NACK 请求，丢失数=" << lostToRequest.size()
                    << " first=" << lostToRequest.front() << " last=" << lostToRequest.back()
                    << " ssrc=0x" << Qt::hex << m_lastSeenSsrc << Qt::dec;
            emit nackPacketsRequested(nackPkt);
        }
    }
    
    // 清理已收到的或超时的 pendingNacks
    auto it = m_pendingNacks.begin();
    while (it != m_pendingNacks.end()) {
        if (m_rtpBuffer.contains(it.key()) || (now - it->lastRequestMs > 1000)) {
            it = m_pendingNacks.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// 解码
// ============================================================================

bool H264Decoder::tryMitigateStripeRiskIfNeeded(int sliceCount, const char *phaseTag) {
  const char *phase = phaseTag ? phaseTag : "?";
  const QString envThr = QString::fromUtf8(qgetenv("CLIENT_FFMPEG_DECODE_THREADS"));

  if (h264StripeDiagVerbose()) {
    qInfo().noquote()
        << "[H264][" << m_streamTag << "][StripeDiag][enter] phase=" << phase
        << " slices=" << sliceCount << " ctx=" << (m_ctx ? "ok" : "null")
        << " thread_count=" << (m_ctx ? m_ctx->thread_count : -1)
        << " mitApplied=" << m_stripeMitigationApplied << " forcedThr=" << m_forcedDecodeThreadCount
        << " expectedSliceLearned=" << m_expectedSliceCount << " codecOpen=" << m_codecOpen
        << " env_CLIENT_FFMPEG_DECODE_THREADS=" << envThr << " frame#=" << m_logSendCount
        << " rtpSeq=" << m_lastRtpSeq;
  }

  if (sliceCount <= 1) {
    if (!(m_stripeDiagSkipLoggedMask & kStripeSkipLoggedSliceLe1)) {
      m_stripeDiagSkipLoggedMask |= kStripeSkipLoggedSliceLe1;
      qInfo() << "[H264][" << m_streamTag << "][StripeDiag][skip_once] reason=slices_lte_1 slices="
              << sliceCount << " phase=" << phase << " frame#=" << m_logSendCount;
    }
    return false;
  }
  if (!m_ctx) {
    if (!(m_stripeDiagSkipLoggedMask & kStripeSkipLoggedNoCtx)) {
      m_stripeDiagSkipLoggedMask |= kStripeSkipLoggedNoCtx;
      qWarning() << "[H264][" << m_streamTag
                 << "][StripeDiag][skip_once][BUG] reason=no_codec_context phase=" << phase;
    }
    return false;
  }
  if (m_ctx->thread_count <= 1) {
    if (!(m_stripeDiagSkipLoggedMask & kStripeSkipLoggedThrLe1)) {
      m_stripeDiagSkipLoggedMask |= kStripeSkipLoggedThrLe1;
      qInfo() << "[H264][" << m_streamTag << "][StripeDiag][skip_once] reason=libav_threads_lte_1 "
              << "slices=" << sliceCount << " thread_count=" << m_ctx->thread_count << " phase=" << phase
              << " env_CLIENT_FFMPEG_DECODE_THREADS=" << envThr;
    }
    return false;
  }
  if (m_stripeMitigationApplied) {
    if (!(m_stripeDiagSkipLoggedMask & kStripeSkipLoggedMitigated)) {
      m_stripeDiagSkipLoggedMask |= kStripeSkipLoggedMitigated;
      qInfo() << "[H264][" << m_streamTag
              << "][StripeDiag][skip_once] reason=mitigation_already_applied slices=" << sliceCount
              << " thread_count=" << m_ctx->thread_count << " forcedThr=" << m_forcedDecodeThreadCount;
    }
    return false;
  }

  const QByteArray mitEnv = qgetenv("CLIENT_H264_STRIPE_AUTO_MITIGATION");
  const bool autoMit =
      mitEnv.isEmpty() ||
      (mitEnv.trimmed() != QByteArrayLiteral("0") &&
       mitEnv.trimmed().toLower() != QByteArrayLiteral("false") &&
       mitEnv.trimmed().toLower() != QByteArrayLiteral("off"));
  const QString detail =
      QStringLiteral("slicesPerFrame=%1 libavcodec_thread_count=%2 env_CLIENT_FFMPEG_DECODE_THREADS=%3 "
                     "phase=%4 "
                     "★ ITU-T H.264 多 slice + FFmpeg FF_THREAD_SLICE 并行易在 slice 边界产生水平带状伪影")
          .arg(sliceCount)
          .arg(m_ctx->thread_count)
          .arg(QString::fromUtf8(qgetenv("CLIENT_FFMPEG_DECODE_THREADS")))
          .arg(QLatin1String(phase));

  qCritical().noquote()
      << "[H264][" << m_streamTag
      << "][STRIPE_RISK] 码流每帧多 slice 且解码线程数>1 — 条状花屏高风险。" << detail
      << " autoMitigation=" << (autoMit ? QStringLiteral("on") : QStringLiteral("off"))
      << " unset CLIENT_H264_STRIPE_AUTO_MITIGATION 或设为 1 可自动强制单线程并请求 IDR";

  if (autoMit) {
    const QString healthPreMitigation = buildVideoStreamHealthDetailLine();
    m_stripeMitigationApplied = true;
    m_forcedDecodeThreadCount = 1;
    closeDecoder();  // 保留 SPS/PPS；勿在此后调 recoverDecoderAfterCorruptBitstream（会清参数集）
    m_haveDecodedKeyframe = false;
    m_needKeyframe = true;
    m_framesSinceKeyframeRequest = 0;
    emit decodeIntegrityAlert(QStringLiteral("MULTI_SLICE_MULTITHREAD_STRIPE_RISK"), detail, true,
                              healthPreMitigation);
    emit senderKeyframeSuggested();
    qWarning().noquote()
        << "[H264][" << m_streamTag
        << "][STRIPE_FIX] 已应用：强制单线程解码、关闭解码器并请求关键帧；下一完整 GOP 后应恢复。"
           " 若仍异常查编码端 slice 配置与 CLIENT_VIDEO_EVIDENCE_* / CLIENT_VIDEO_SAVE_FRAME"
        << " correlate_frame#=" << m_logSendCount << " rtpSeq=" << m_lastRtpSeq << " phase=" << phase;
    logVideoStreamHealthContract(QStringLiteral("stripe_mitigation"));
    return true;
  }
  qWarning().noquote() << "[H264][" << m_streamTag
                         << "][STRIPE_RISK_NO_AUTO] 未自动缓解（CLIENT_H264_STRIPE_AUTO_MITIGATION=0）；"
                            "条状风险仍存在。 correlate_frame#="
                         << m_logSendCount << " rtpSeq=" << m_lastRtpSeq << " " << detail;
  emit decodeIntegrityAlert(QStringLiteral("MULTI_SLICE_MULTITHREAD_STRIPE_RISK"), detail, false,
                            buildVideoStreamHealthDetailLine());
  return false;
}

void H264Decoder::decodeCompleteFrame(const std::vector<QByteArray> &nalUnits, bool bitstreamIncomplete) {
  // ── ★★★ 端到端追踪：decodeCompleteFrame 进入 ★★★ ───────────────────────
  const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
  const int64_t feedRtpToDecodeMs =
      (m_lastFeedRtpTime > 0) ? (funcEnterTime - m_lastFeedRtpTime) : -1;
  m_logSendCount++;
  
  if (bitstreamIncomplete) {
      qDebug() << "[H264][" << m_streamTag << "][decode] bitstreamIncomplete=true, 将标记 CORRUPT";
  }
  if (m_logSendCount <= 10) {
    // 先计算 slice 信息
    int sliceCount = 0, idrCount = 0;
    for (const auto &nal : nalUnits) {
      if (nal.isEmpty())
        continue;
      int t = static_cast<uint8_t>(nal[0]) & 0x1f;
      if (t == 1)
        sliceCount++;
      if (t == 5) {
        sliceCount++;
        idrCount++;
      }
    }
    size_t totalBytes = 0;
    for (const auto &nal : nalUnits) {
      if (!nal.isEmpty())
        totalBytes += 4 + nal.size();
    }
    qInfo() << "[H264][decode] ★★★ decodeCompleteFrame ENTER ★★★ stream=" << m_streamTag
            << " frame#=" << m_logSendCount << " slices=" << sliceCount << "(IDR=" << idrCount
            << ")"
            << " totalBytes=" << totalBytes << " rtpSeq=" << m_lastRtpSeq
            << " feedRtpToDecodeMs=" << feedRtpToDecodeMs << " needKeyframe=" << m_needKeyframe
            << " haveDecodedKeyframe=" << m_haveDecodedKeyframe
            << " bitstreamIncomplete=" << bitstreamIncomplete
            << " codecOpen=" << m_codecOpen << "（>100ms=解码线程积压，<50ms=正常）";
  }

  {
    if (!ensureDecoder()) {
      ++m_statsEnsureDecoderFailInWindow;
      qWarning() << "[H264][decode][DecodePath] ensureDecoder_failed drop_frame stream=" << m_streamTag
                 << " frame#=" << m_logSendCount << " rtpSeq=" << m_lastRtpSeq
                 << " needKf=" << m_needKeyframe << " sps=" << m_sps.size() << " pps=" << m_pps.size();
      return;
    }

    static const uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};

    // ★ 计算总大小并构建 Annex-B 格式
    size_t totalSize = 0;
    int sliceCount = 0;
    int idrCount = 0;
    for (const auto &nal : nalUnits) {
      if (nal.isEmpty())
        continue;
      totalSize += 4 + nal.size();
      int t = static_cast<uint8_t>(nal[0]) & 0x1f;
      if (t == 1)
        sliceCount++;
      if (t == 5) {
        sliceCount++;
        idrCount++;
      }
    }

    {
      const int pathMode = h264DecodePathDiagMode();
      const bool pathEvery = (pathMode == 1);
      const bool pathSample = (!pathEvery && m_decodePathDiagFramesRemaining > 0);
      if (pathEvery || pathSample) {
        if (!pathEvery && m_decodePathDiagFramesRemaining > 0)
          --m_decodePathDiagFramesRemaining;
        
        QString nalSizes;
        for (const auto &nal : nalUnits) {
            if (nalSizes.length() < 256) {
                if (!nalSizes.isEmpty()) nalSizes += QLatin1Char(',');
                // 增加 NAL 头部的类型信息 (bits 0-4)
                int nType = (nal.isEmpty() ? -1 : (static_cast<uint8_t>(nal[0]) & 0x1f));
                nalSizes += QStringLiteral("%1(T%2)").arg(nal.size()).arg(nType);
            }
        }
        if (nalUnits.size() > 16) nalSizes += QStringLiteral("...");

        qInfo().noquote()
            << "[H264][" << m_streamTag << "][DecodePath] frame#=" << m_logSendCount
            << " slices=" << sliceCount << " idrNals=" << idrCount << " annexBBytes=" << totalSize
            << " nalSizes(bytes[Type])=[" << nalSizes << "]"
            << " expSliceLearned=" << m_expectedSliceCount
            << " bitstreamIncomplete=" << (bitstreamIncomplete ? 1 : 0)
            << " libavThr=" << (m_ctx ? m_ctx->thread_count : -1)
            << " forcedThr=" << m_forcedDecodeThreadCount << " mitApplied=" << m_stripeMitigationApplied
            << " codecOpen=" << m_codecOpen << " needKf=" << m_needKeyframe
            << " haveDecodedKf=" << m_haveDecodedKeyframe << " rtpSeq=" << m_lastRtpSeq
            << " feedRtpToDecodeMs=" << feedRtpToDecodeMs
            << " BITRATE_ENV=" << qgetenv("VIDEO_BITRATE_KBPS");
      }
    }

    if (tryMitigateStripeRiskIfNeeded(sliceCount, "post_ensureDecoder")) {
      qWarning() << "[H264][" << m_streamTag
                 << "][DecodePath] abort_decode stripe_mitigation_applied frame#=" << m_logSendCount
                 << " rtpSeq=" << m_lastRtpSeq << " slices=" << sliceCount;
      return;
    }

    if (sliceCount > 1 && m_ctx && m_ctx->thread_count <= 1 && !m_loggedMultiSliceThreadOk) {
      m_loggedMultiSliceThreadOk = true;
      qInfo() << "[H264][" << m_streamTag << "][DecodeCheck] multi-slice stream slices=" << sliceCount
              << " thread_count=1 — 条状风险低（保持默认单线程解码）";
    }

    QByteArray annexB;
    annexB.reserve(static_cast<int>(totalSize) + AV_INPUT_BUFFER_PADDING_SIZE);
    for (const auto &nal : nalUnits) {
      if (nal.isEmpty())
        continue;
      annexB.append(reinterpret_cast<const char *>(kStartCode), 4);
      annexB.append(nal);
    }
    // ★★★ 根本原因修复：FFmpeg 要求输入缓冲必须有 PADDING_SIZE 结尾 ★★★
    // 否则优化后的 bitstream reader 会读取越界，导致 NAL 提前终止或解码花屏（条状根源之一）
    for (int i = 0; i < AV_INPUT_BUFFER_PADDING_SIZE; ++i) {
        annexB.append('\0');
    }

    if (m_webrtcHwActive && m_webrtcHw) {
      const int64_t pts = m_webrtcHwPts++;
      static int s_hwInCount = 0;
      if (s_hwInCount <= 5 || (s_hwInCount % 300) == 0) {
          qDebug() << "[H264][" << m_streamTag << "][HW] submitCompleteAnnexB size=" << annexB.size()
                   << " pts=" << pts << " totalHwIn=" << s_hwInCount;
      }
      s_hwInCount++;
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
      return;
    }

    // m_logSendCount 已在函数入口递增，NAL/slice 信息已打印

    // ★ 创建并发送 AVPacket
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
      ++m_statsAvSendFailInWindow;
      qCritical() << "[H264][decode][ERROR] av_packet_alloc 失败 stream=" << m_streamTag;
      return;
    }

    pkt->data = reinterpret_cast<uint8_t *>(annexB.data());
    pkt->size = annexB.size();

    // ★ 标记可能损坏的帧
    if (m_needKeyframe || bitstreamIncomplete) {
      pkt->flags |= AV_PKT_FLAG_CORRUPT;
    }

    int ret = avcodec_send_packet(m_ctx, pkt);
    static int s_swInCount = 0;
    if (ret >= 0 && (s_swInCount <= 5 || (s_swInCount % 300) == 0)) {
        qDebug() << "[H264][" << m_streamTag << "][SW] avcodec_send_packet ok size=" << annexB.size()
                 << " totalSwIn=" << s_swInCount << " corruptFlag=" << ((pkt->flags & AV_PKT_FLAG_CORRUPT) ? 1 : 0);
    }
    s_swInCount++;
    av_packet_free(&pkt);

    if (ret == AVERROR(EAGAIN)) {
      ++m_statsEagainSendInWindow;
      // 解码器缓冲满，先取出帧
      qDebug() << "[H264][decode] avcodec_send_packet 返回 EAGAIN，先取出缓冲帧 stream=" << m_streamTag;
      {
        emitDecodedFrames(bitstreamIncomplete);
      }
      return;
    }

    if (ret == AVERROR_INVALIDDATA || ret == AVERROR(EIO)) {
      ++m_statsAvSendFailInWindow;
      // 数据无效或硬件解码器 I/O 错误，立即 flush 解码器并标记需要 IDR
      const char *errName = (ret == AVERROR(EIO)) ? "EIO" : "INVALIDDATA";
      qWarning() << "[H264][decode][ERROR] avcodec_send_packet 失败(" << errName << ")"
                 << " ret=" << ret << " 立即 flush 并等待 IDR"
                 << " slices=" << sliceCount << " codecOpen=" << m_codecOpen
                 << " stream=" << m_streamTag;
      emitKeyframeSuggestThrottled("avcodec_invalid_data");
      recoverDecoderAfterCorruptBitstream();
      return;
    }

    if (ret < 0 && ret != AVERROR_EOF && ret != AVERROR(EAGAIN) && ret != AVERROR(EIO)) {
      ++m_statsAvSendFailInWindow;
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      qCritical() << "[H264][decode][ERROR] avcodec_send_packet 未知错误 ret=" << ret
                  << " err=" << errbuf << " stream=" << m_streamTag;
      return;
    }

    {
      emitDecodedFrames(bitstreamIncomplete);
    }
  }
}

void H264Decoder::emitDecodedFrames(bool bitstreamIncomplete) {
  // ── 端到端追踪（默认关闭）：每条 RTP 解码路径都会进此函数，qInfo 同步写 stderr
  // 会长时间占用主线程， 拖慢 Qt 事件循环 → frameReady 槽排队膨胀 → VideoOutput
  // 更新不均匀，主观上像「整屏闪烁/刷新」。 与 webrtcclient.cpp 中「勿对 cam_front 每条 RTP
  // 打日志」同一机理。需要全量追踪时：
  //   CLIENT_H264_LOG_EVERY_EMIT=1
  const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
  const int64_t feedRtpToEmitMs =
      (m_lastFeedRtpTime > 0) ? (funcEnterTime - m_lastFeedRtpTime) : -1;
  if (bitstreamIncomplete || qEnvironmentVariableIntValue("CLIENT_H264_LOG_EVERY_EMIT") != 0) {
    qInfo() << "[H264][emit] ★★★ emitDecodedFrames ENTER ★★★ stream=" << m_streamTag
            << " lifecycleId=" << m_currentLifecycleId.load()
            << " bitstreamIncomplete=" << bitstreamIncomplete
            << " feedRtpToEmitMs=" << feedRtpToEmitMs
            << "（从 RTP 包到解码输出的端到端耗时，>200ms 说明解码线程阻塞或解码卡顿）";
  }

  AVFrame *frame = av_frame_alloc();
  if (!frame) {
    qCritical() << "[H264][" << m_streamTag << "] av_frame_alloc 失败!";
    return;
  }

  int consecutive_errors = 0;
  const int max_consecutive_errors = 3;  // 连续3次错误后 flush
  int framesOut = 0;

  while (true) {
    int ret;
    {
      ret = avcodec_receive_frame(m_ctx, frame);
    }

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;  // 正常结束
    }

    if (ret < 0) {
      ++m_statsAvRecvFailInWindow;
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      static QHash<QString, int> s_recvErrLogN;
      const int errN = ++s_recvErrLogN[m_streamTag];
      if (errN <= 25 || (errN % 60) == 0) {
        qWarning() << "[H264][" << m_streamTag
                   << "][decode][ERROR] avcodec_receive_frame ret=" << ret << " err=" << errbuf
                   << " ★码流/参考帧损坏或解码器异常；连续多次将 flush 等 IDR";
      }
      // 解码错误
      consecutive_errors++;
      if (consecutive_errors >= max_consecutive_errors && !m_needKeyframe) {
        qWarning() << "[H264][" << m_streamTag << "][ERROR] 连续" << consecutive_errors
                   << "次解码错误，强制 flush 并等待 IDR";
        recoverDecoderAfterCorruptBitstream();
        m_framesSinceKeyframeRequest = 0;
      }
      break;  // 跳过当前帧
    }

    consecutive_errors = 0;  // 重置错误计数

    {
      int w = frame->width;
      int h = frame->height;
      if (w <= 0 || h <= 0) {
        ++m_statsQualityDropInWindow;
        qWarning() << "[H264][" << m_streamTag << "] 无效帧尺寸 w=" << w << " h=" << h << "，跳过 ★解码输出异常";
        continue;
      }

      // ── ★ v4: 解码器内部策略审计 ──────────────────────────────────────────
      if (m_ctx) {
          if (m_ctx->skip_frame != m_lastSkipFrame || m_ctx->skip_loop_filter != m_lastSkipLoopFilter) {
              qInfo() << "[H264][" << m_streamTag << "][SkipPolicy] ★ 解码器内部策略变更"
                      << " skip_frame=" << m_ctx->skip_frame << " (was " << m_lastSkipFrame << ")"
                      << " skip_loop_filter=" << m_ctx->skip_loop_filter << " (was " << m_lastSkipLoopFilter << ")"
                      << " ★ 策略降低（如跳过去块滤波）会导致条状感加重";
              m_lastSkipFrame = m_ctx->skip_frame;
              m_lastSkipLoopFilter = m_ctx->skip_loop_filter;
          }
      }

      if (frame->decode_error_flags != 0 || bitstreamIncomplete) {
          static QHash<QString, int> s_errFlagLog;
          if (++s_errFlagLog[m_streamTag] <= 50 || (s_errFlagLog[m_streamTag] % 100 == 0)) {
              qWarning() << "[H264][" << m_streamTag << "][decode][WARN] 帧解码异常标记"
                         << " fid=" << m_frameIdCounter + 1
                         << " decode_error_flags=" << frame->decode_error_flags
                         << " bitstreamIncomplete=" << bitstreamIncomplete
                         << " ★ 非 0 表示 FFmpeg 检出损坏；1=BITSTREAM, 2=BUFFER, 4=CONCEALED";
          }
      }

      const AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);

      // ── ★ 深入诊断：AVFrame 原始属性与 Y 平面指纹 ────────────────────────────
      // 只有在疑似条状帧（由下文 QImage 检测触发后，或采样帧）才打印，避免干扰性能
      const bool isSampleFrame = (m_framesEmitted % 60 == 0);
      uint32_t y0_before = 0, y1_before = 0;
      if (isSampleFrame) {
          const char* fmtName = av_get_pix_fmt_name(srcFmt);
          uint32_t ySamples[5] = {0};
          if (frame->data[0] && frame->linesize[0] >= 64) {
              const uint8_t* pY = frame->data[0];
              const int lsY = frame->linesize[0];
              const int frameH = frame->height;
              // 采样 5 个垂直位置：顶(0), 1/4, 中(1/2), 3/4, 底(-1)
              const int rows[] = {0, frameH/4, frameH/2, 3*frameH/4, frameH-1};
              for(int r=0; r<5; ++r) {
                  uint32_t hval = 2166136261u;
                  const uint8_t* rowPtr = pY + rows[r] * lsY;
                  for(int i=0; i<64; ++i) { hval ^= rowPtr[i]; hval *= 16777619u; }
                  ySamples[r] = hval;
              }
              y0_before = ySamples[0];
              y1_before = ySamples[2]; // 存中间行用于对比
          }
          qInfo().noquote() << QStringLiteral("[H264][%1][AVFrameDiag] fid=%2 fmt=%3(%4) sz=%5x%6 ls0=%7 ls1=%8 ls2=%9 "
                                             "interlaced=%10 topFirst=%11 Y_Samples[top,1/4,mid,3/4,bot]=[%12,%13,%14,%15,%16] addr0=%17")
                               .arg(m_streamTag).arg(m_frameIdCounter + 1).arg(srcFmt).arg(fmtName ? fmtName : "?")
                               .arg(w).arg(h).arg(frame->linesize[0]).arg(frame->linesize[1]).arg(frame->linesize[2])
                               .arg(frame->interlaced_frame).arg(frame->top_field_first)
                               .arg(ySamples[0], 8, 16, QLatin1Char('0')).arg(ySamples[1], 8, 16, QLatin1Char('0'))
                               .arg(ySamples[2], 8, 16, QLatin1Char('0')).arg(ySamples[3], 8, 16, QLatin1Char('0'))
                               .arg(ySamples[4], 8, 16, QLatin1Char('0'))
                               .arg(reinterpret_cast<uintptr_t>(frame->data[0]), 0, 16);
      }

      // 隔行元数据明确时：在 YUV→RGBA 前做简易去隔行/场重复（CLIENT_VIDEO_INTERLACED_POLICY）
      // ── ★ 5-WHY 加固：建立解码减速反馈（Backpressure） ────────────────────────
      const double pressure = poolPressure();
      if (pressure > 0.5) {
        static QHash<QString, int> s_pressureLog;
        if (++s_pressureLog[m_streamTag] <= 5 || (s_pressureLog[m_streamTag] % 100 == 0)) {
          qWarning().noquote() << QStringLiteral(
                                      "[H264][%1][Backpressure] ★ 帧池积压严重 pressure=%2 "
                                      "busySlots=%3 ★ 主线程卡顿，解码器将主动丢弃非关键帧以减轻 CPU "
                                      "负担")
                                      .arg(m_streamTag)
                                      .arg(pressure, 0, 'f', 2)
                                      .arg(static_cast<int>(pressure * kFramePoolSize));
        }

        // 积压严重时，仅保留关键帧（IDR）的呈现，丢弃 P 帧的 RGBA 转换和 emit
        // 注意：不能停止 avcodec_send_packet，否则参考链会断；只能停止昂贵的 sws_scale 和信号投递。
        const bool isKey = (frame->key_frame || (frame->pict_type == AV_PICTURE_TYPE_I));
        if (!isKey && pressure > 0.7) {
          m_droppedPFrameCount++;
          m_statsQualityDropInWindow++;
          continue;  // 丢弃 P 帧转换
        }
      }

      VideoInterlacedPolicy::maybeApplyAvFrame(frame, m_streamTag);

      if (isSampleFrame) {
          uint32_t y0_after = 0, y1_after = 0;
          if (frame->data[0] && frame->linesize[0] >= 64) {
              for(int i=0; i<64; ++i) {
                  y0_after = y0_after * 31 + frame->data[0][i];
                  y1_after = y1_after * 31 + (frame->data[0] + frame->linesize[0])[i];
              }
          }
          if (y0_before != y0_after || y1_before != y1_after) {
              qInfo().noquote() << QStringLiteral("[H264][%1][AVFrameDiag] ★ VideoInterlacedPolicy 修改了像素！ y0: %2->%3 y1: %4->%5")
                                   .arg(m_streamTag).arg(y0_before, 8, 16, QLatin1Char('0')).arg(y0_after, 8, 16, QLatin1Char('0'))
                                   .arg(y1_before, 8, 16, QLatin1Char('0')).arg(y1_after, 8, 16, QLatin1Char('0'));
          }
      }

      if (m_width != w || m_height != h) {
        qInfo() << "[H264][" << m_streamTag << "] 视频分辨率变化: " << m_width << "x" << m_height
                << " -> " << w << "x" << h << "，重建 sws 上下文";
        m_width = w;
        m_height = h;
        if (m_sws) {
          sws_freeContext(m_sws);
          m_sws = nullptr;
        }
        m_swsSrcFmt = AV_PIX_FMT_NONE;
      }
      // sws_getContext 绑定源 AVPixelFormat；仅按分辨率失效会漏掉「宽高不变、fmt 从例如
      // YUV420P→NV12」的换流。
      if (m_sws && m_swsSrcFmt != AV_PIX_FMT_NONE && m_swsSrcFmt != srcFmt) {
        const char *const oldName = av_get_pix_fmt_name(m_swsSrcFmt);
        const char *const newName = av_get_pix_fmt_name(srcFmt);
        qWarning() << "[H264][" << m_streamTag
                   << "][SwsFmt] 源像素格式变化，重建 sws ★ 若频繁出现需查换流/解码器"
                   << " oldFmt=" << m_swsSrcFmt << "(" << (oldName ? oldName : "?") << ")"
                   << " newFmt=" << srcFmt << "(" << (newName ? newName : "?") << ")";
        if (h264SwsDiagEnabled()) {
          qInfo() << "[H264][SwsDiag][" << m_streamTag
                  << "] invalidate m_sws reason=src_pixel_format_change"
                  << " linesize0=" << frame->linesize[0] << " linesize1=" << frame->linesize[1]
                  << " linesize2=" << frame->linesize[2];
        }
        sws_freeContext(m_sws);
        m_sws = nullptr;
        m_swsSrcFmt = AV_PIX_FMT_NONE;
      }
      if (!m_sws) {
        // ★ 格式选择说明（2024-04 修复条纹问题）：
        //
        // 改用 AV_PIX_FMT_RGBA → QImage::Format_RGBA8888 的原因：
        //
        // 旧方案 AV_PIX_FMT_BGR0 → QImage::Format_RGB32 → Qt6 RHI → GL_BGRA, GL_UNSIGNED_BYTE
        //   在 LIBGL_ALWAYS_SOFTWARE=1 + QT_XCB_GL_INTEGRATION=glx (Mesa llvmpipe) 路径下，
        //   Mesa 对 GL_BGRA 外部格式的 stride 计算存在 bug：
        //   - 按 3 字节/像素（RGB）计算 row stride = 1280×3 = 3840 字节
        //   - 实际数据 stride = 1280×4 = 5120 字节
        //   - 每显示 4 行后偏移 1 行数据 → 典型水平条纹（条状），完全看不清图像
        //
        // 新方案 AV_PIX_FMT_RGBA → QImage::Format_RGBA8888：
        //   Qt6 RHI 映射到 GL_RGBA, GL_UNSIGNED_BYTE（OpenGL 1.0 核心，无扩展依赖）
        //   - 所有 GL 实现（硬件、软件、ES2+）均原生支持
        //   - stride = 1280×4 = 5120 字节，与 QImage::bytesPerLine() 完全一致
        //   - 彻底消除 GL_BGRA stride 错误风险
        //
        // 性能影响：YUV420P→RGBA 与 YUV420P→BGR0 在 sws_scale
        // 耗时上等价（均为色彩空间转换，无缩放）。 在硬件 GL 环境下也无性能回退：RGBA 同样可被 RHI
        // 高效上传（GL_UNPACK_ALIGNMENT=4）。
        //
        // SWS_BILINEAR：比 FAST_BILINEAR 更稳健，且开启 SWS_BITEXACT 确保输出确定性
        m_sws = sws_getContext(w, h, srcFmt, w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR | SWS_BITEXACT, nullptr,
                               nullptr, nullptr);
        if (!m_sws) {
          ++m_statsQualityDropInWindow;
          qWarning() << "[H264][" << m_streamTag << "][ERROR] sws_getContext 返回 nullptr:"
                     << " w=" << w << " h=" << h << " fmt=" << frame->format << "，跳过帧";
          continue;
        }
        m_swsSrcFmt = srcFmt;
        videoSwsConfigureYuvToRgbaColorspace(m_sws);
        // ★ 始终打印 sws 建立信息（不依赖 CLIENT_H264_SWS_DIAG），便于验证格式路径
        {
          const char *const nm = av_get_pix_fmt_name(srcFmt);
          qInfo() << "[H264][" << m_streamTag << "][SwsSetup] ★ sws_getContext OK"
                  << " srcFmt=" << (nm ? nm : "?") << "(" << srcFmt << ")"
                  << " dstFmt=AV_PIX_FMT_RGBA(QImage::RGBA8888)"
                  << " w=" << w << " h=" << h << " linesize0=" << frame->linesize[0]
                  << " linesize1=" << frame->linesize[1] << " linesize2=" << frame->linesize[2]
                  << " ★ QImage::RGBA8888→GL_RGBA→无GL_BGRA stride风险";
        }
        if (h264SwsDiagEnabled()) {
          const char *const nm = av_get_pix_fmt_name(srcFmt);
          qInfo() << "[H264][SwsDiag][" << m_streamTag << "] sws_getContext ok srcFmt=" << srcFmt
                  << " name=" << (nm ? nm : "?") << " linesize0=" << frame->linesize[0]
                  << " linesize1=" << frame->linesize[1] << " linesize2=" << frame->linesize[2]
                  << " linesize3=" << frame->linesize[3];
        }
      }

      // ── 3槽帧缓冲池（CPU-only 性能优化）──────────────────────────────────────
      // 单帧缓冲方案下：emit frameReady() 后主线程持有 QImage（refcount=2），
      // 解码线程下一帧 detach() 触发 8MB COW 堆分配（4路×30fps = 960MB/s 额外开销）。
      //
      // 3槽轮转：解码线程写 pool[i]，主线程最多持有 1~2 个槽，
      // pool[(i+2)%3] 返回时通常 refcount==1，detach() 为 no-op → 零分配。
      //
      // ★ v4: 槽位生命周期审计
      {
          SlotAudit& audit = m_slotAudit[m_framePoolIdx];
          SlotStatus prevStatus = audit.status.exchange(SlotStatus::Decoding, std::memory_order_acq_rel);
          if (prevStatus == SlotStatus::Queued) {
              qCritical() << "[H264][" << m_streamTag << "][MemoryPool][CRITICAL_RACE] ★ 槽位状态异常！"
                          << " pool_idx=" << m_framePoolIdx << " status_was=Queued"
                          << " lastFid=" << audit.lastFid.load() << " lastAcquireMs=" << audit.lastAcquireMs.load()
                          << " ★ 说明主线程队列中仍持有此槽位，解码器强行写入可能导致花屏/条状";
          }
          audit.lastFid.store(m_frameIdCounter + 1, std::memory_order_release);
          audit.lastAcquireMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_release);
      }

      QImage &dstFrame = m_framePool[m_framePoolIdx];
      m_framePoolIdx = (m_framePoolIdx + 1) % kFramePoolSize;

      if (dstFrame.width() != w || dstFrame.height() != h ||
          dstFrame.format() != QImage::Format_RGBA8888) {
        dstFrame = QImage(w, h, QImage::Format_RGBA8888);
        if (dstFrame.isNull()) {
          ++m_statsQualityDropInWindow;
          qCritical() << "[H264][" << m_streamTag << "][ERROR] QImage 分配失败 w=" << w
                      << " h=" << h << "，跳过帧";
          continue;
        }
      } else {
        // COW：若主线程仍持有此槽（refcount>1）则 detach 分配新缓冲；否则通常 no-op（零分配）
        const uchar* oldBits = dstFrame.constBits();
        dstFrame.detach();
        const uchar* newBits = dstFrame.constBits();
        if (oldBits != newBits) {
            static std::atomic<int> s_cowCount{0};
            if (s_cowCount.fetch_add(1, std::memory_order_relaxed) < 10) {
                qInfo() << "[H264][" << m_streamTag << "][MemoryPool] ★ 帧池溢出触发 COW 拷贝！"
                        << " pool_idx=" << m_framePoolIdx << " addr_was=" << (void*)oldBits 
                        << " addr_now=" << (void*)newBits
                        << " ★ 说明主线程积压严重，建议增加 kFramePoolSize 或优化主线程";
            }
        }
      }

      // ★★★ 根本原因定死：强制清理缓冲区 ★★★
      // 如果 memset 为 0 后，画面仍有条带，说明是 sws_scale 写入的坏像素。
      // 如果画面变黑且有零星条带，说明是解码器部分宏块未写。
      std::memset(dstFrame.bits(), 0, dstFrame.sizeInBytes());

      uint8_t *dst[] = {dstFrame.bits()};
      int dstStride[] = {static_cast<int>(dstFrame.bytesPerLine())};
      if (!dst[0]) {
        ++m_statsQualityDropInWindow;
        qWarning() << "[H264][" << m_streamTag << "][ERROR] dstFrame.bits() 返回 nullptr:"
                   << " w=" << w << " h=" << h << " pool_idx=" << m_framePoolIdx << "，跳过帧";
        continue;
      }
      QElapsedTimer swsTimer;
      swsTimer.start();
      int scaleRet = sws_scale(m_sws, frame->data, frame->linesize, 0, h, dst, dstStride);
      const qint64 swsUs = swsTimer.nsecsElapsed() / 1000;
      if (scaleRet != h) {
        ++m_statsQualityDropInWindow;
        qWarning() << "[H264][" << m_streamTag << "][WARN] sws_scale 返回" << scaleRet << "期望"
                   << h << " w=" << w << " h=" << h
                   << " ★ 竖向未填满，丢弃本帧不 emit（保证显示仅完整帧）";
        emit decodeIntegrityAlert(
            QStringLiteral("SWS_SCALE_INCOMPLETE"),
            QStringLiteral("gotRows=%1 expectH=%2 w=%3 stream=%4")
                .arg(scaleRet)
                .arg(h)
                .arg(w)
                .arg(m_streamTag),
            false, buildVideoStreamHealthDetailLine());
        continue;
      }
      if (dstFrame.bytesPerLine() < w * 4) {
        ++m_statsQualityDropInWindow;
        qWarning() << "[H264][" << m_streamTag
                   << "][WARN] RGBA stride 异常 bpl=" << dstFrame.bytesPerLine() << " w=" << w
                   << " ★ 丢弃本帧";
        emit decodeIntegrityAlert(QStringLiteral("RGBA_STRIDE_ANOMALY"),
                                  QStringLiteral("bpl=%1 w=%2 need>=%3 stream=%4")
                                      .arg(dstFrame.bytesPerLine())
                                      .arg(w)
                                      .arg(w * 4)
                                      .arg(m_streamTag),
                                  false, buildVideoStreamHealthDetailLine());
        continue;
      }
      // ★ 始终打印前3帧的像素采样，用于验证 sws_scale 输出正确性
      // 若 R/G/B 值全为 0 或完全相同 → sws 输出异常；若 R≠B → 颜色通道符合预期
      if (m_framesEmitted < 3) {
        const char *const nm = av_get_pix_fmt_name(srcFmt);
        // 采样左上角(0,0)和中心点像素验证解码正确性
        const uint8_t *rowTL = dstFrame.constScanLine(0);
        const uint8_t *rowCtr = dstFrame.constScanLine(h / 2);
        const int ctrX = w / 2;
        qInfo() << "[H264][" << m_streamTag << "][PixelVerify] frame#" << (m_framesEmitted + 1)
                << " srcFmt=" << (nm ? nm : "?") << " dstFmt=RGBA8888"
                << " swsTime=" << swsUs << "us"
                << " bpl=" << dstFrame.bytesPerLine() << " w=" << w << " h=" << h << " px[0,0]=(R"
                << (int)rowTL[0] << ",G" << (int)rowTL[1] << ",B" << (int)rowTL[2] << ",A"
                << (int)rowTL[3] << ")"
                << " px[ctr]=(R" << (int)rowCtr[ctrX * 4] << ",G" << (int)rowCtr[ctrX * 4 + 1]
                << ",B" << (int)rowCtr[ctrX * 4 + 2] << ",A" << (int)rowCtr[ctrX * 4 + 3] << ")"
                << " ★ A应=255;若R/G/B全0则sws异常;若颜色合理则解码正确";

        // ★ v4 Hyper-Log：记录 AVFrame (YUV) 的原始数据，看 corruption 是否源于 scaler 之前
        if (frame->data[0]) {
            qInfo() << "[H264][" << m_streamTag << "][SourceVerify] AVFrame data pointers:"
                    << " Y=" << (void*)frame->data[0] << " linesize0=" << frame->linesize[0]
                    << " U=" << (void*)frame->data[1] << " linesize1=" << frame->linesize[1]
                    << " V=" << (void*)frame->data[2] << " linesize2=" << frame->linesize[2];
        }
      }
      if (h264SwsDiagEnabled() && m_framesEmitted < 6) {
        const char *const nm = av_get_pix_fmt_name(srcFmt);
        qInfo() << "[H264][SwsDiag][" << m_streamTag << "] post_sws frame#" << (m_framesEmitted + 1)
                << " srcFmt=" << (nm ? nm : "?") << " dstBpl=" << dstFrame.bytesPerLine()
                << " w=" << w << " h=" << h;
      }

      // 解码后落盘 PNG/RAW（环境变量 CLIENT_VIDEO_SAVE_FRAME），用于区分解码花屏 vs 仅 GPU 显示异常
      H264ClientDiag::maybeDumpDecodedFrame(dstFrame, m_streamTag, m_frameIdCounter + 1,
                                            &m_diagFrameDumpCount);

      // ── ★★★ 实时条状检测 + STRIPE_VERDICT + 可选 stripe-alerts PNG ─────────
      QElapsedTimer decAnalyzeTimer;
      decAnalyzeTimer.start();
      const VideoFrameEvidence::StripeHeuristicReport sh =
          VideoFrameEvidence::analyzeStripeHeuristic(dstFrame);
      const qint64 decAnalyzeUs = decAnalyzeTimer.nsecsElapsed() / 1000;
  if (sh.verdict != VideoFrameEvidence::StripeHeuristicVerdict::Clean) {
    H264ClientDiag::maybeDumpStripeAlertCapture(
        dstFrame, m_streamTag, m_frameIdCounter + 1, VideoFrameEvidence::stripeVerdictTag(sh.verdict),
        sh.horizontalShift, sh.fineTop, sh.fineMid, sh.fineBot);

    static QHash<QString, int> s_stripeAlerts;
    if (++s_stripeAlerts[m_streamTag] <= 15) {
      // ── ★ 精准定位：条状触发时的 AVFrame 状态 ────────────────────────────
      const AVPixelFormat curSrcFmt = static_cast<AVPixelFormat>(frame->format);
      const char *curFmtName = av_get_pix_fmt_name(curSrcFmt);
      
      // ── ★ 取证：计算 AVFrame Y/U/V 平面指纹，判断是否「源头已坏」 ─────────────
      uint32_t ySamples[5] = {0};
      uint32_t uTop0 = 0, vTop0 = 0;
      if (frame->data[0] && frame->linesize[0] >= 64) {
          const uint8_t* pY = frame->data[0];
          const int lsY = frame->linesize[0];
          const int frameH = frame->height;
          const int rows[] = {0, frameH/4, frameH/2, 3*frameH/4, frameH-1};
          for(int r=0; r<5; ++r) {
              uint32_t hval = 2166136261u;
              const uint8_t* rowPtr = pY + rows[r] * lsY;
              for(int i=0; i<64; ++i) { hval ^= rowPtr[i]; hval *= 16777619u; }
              ySamples[r] = hval;
          }
          // 增加 U/V 平面指纹，排查是否仅色彩平面损坏
          if (frame->data[1] && frame->data[2] && frame->linesize[1] >= 32) {
              for(int i=0; i<32; ++i) { 
                  uTop0 = uTop0 * 31 + frame->data[1][i]; 
                  vTop0 = vTop0 * 31 + frame->data[2][i]; 
              }
          }
      }

      qCritical().noquote() << QStringLiteral("[H264][%1][STRIPE_POSITIONING] ★★★ 条状精准定界 ★★★")
                                   .arg(m_streamTag);
      qCritical().noquote() << QStringLiteral("  -> QImage: fid=%1 shift=%2 fineTop=%3 bpl=%4 addr=%5")
                                   .arg(m_frameIdCounter + 1)
                                   .arg(sh.horizontalShift)
                                   .arg(sh.fineTop)
                                   .arg(dstFrame.bytesPerLine())
                                   .arg(reinterpret_cast<uintptr_t>(dstFrame.constBits()), 0, 16);
      
      QString errDetail;
      if (frame->decode_error_flags & 1) errDetail += "BITSTREAM ";
      if (frame->decode_error_flags & 2) errDetail += "BUFFER ";
      if (frame->decode_error_flags & 4) errDetail += "CONCEALED ";
      
      qCritical().noquote() << QStringLiteral("  -> AVFrame: wxh=%1x%2 fmt=%3(%4) ls=%5,%6,%7 interlaced=%8 "
                                              "topFirst=%9 flags=0x%10 err=0x%11(%12) addr0=%13 ssrc=0x%14")
                                   .arg(frame->width)
                                   .arg(frame->height)
                                   .arg(curSrcFmt)
                                   .arg(curFmtName ? curFmtName : "?")
                                   .arg(frame->linesize[0])
                                   .arg(frame->linesize[1])
                                   .arg(frame->linesize[2])
                                   .arg(frame->interlaced_frame)
                                   .arg(frame->top_field_first)
                                   .arg(frame->flags, 0, 16)
                                   .arg(frame->decode_error_flags, 0, 16)
                                   .arg(errDetail.trimmed())
                                   .arg(reinterpret_cast<uintptr_t>(frame->data[0]), 0, 16)
                                   .arg(m_lastSeenSsrc, 0, 16);
      qCritical().noquote() << QStringLiteral("  -> AVFrame Fingerprint: Y_Samples[top,1/4,mid,3/4,bot]=[%1,%2,%3,%4,%5] U_top0=%6 V_top0=%7 ★ 若 Samples 中任意相邻两项相等则源头已成条")
                                   .arg(ySamples[0], 8, 16, QLatin1Char('0')).arg(ySamples[1], 8, 16, QLatin1Char('0'))
                                   .arg(ySamples[2], 8, 16, QLatin1Char('0')).arg(ySamples[3], 8, 16, QLatin1Char('0'))
                                   .arg(ySamples[4], 8, 16, QLatin1Char('0'))
                                   .arg(uTop0, 8, 16, QLatin1Char('0')).arg(vTop0, 8, 16, QLatin1Char('0'));
      
      // ── ★ 深入取证：QImage 故障行前 8 字节摘要 ───────────────────────
      if (dstFrame.width() >= 8) {
          QString rowHex;
          const int startR = (sh.fineTop != 0) ? 0 : (sh.fineBot != 0 ? (dstFrame.height() - 16) : 0);
          for(int i=0; i<4; ++i) {
              const uint8_t* sl = dstFrame.constScanLine(startR + i);
              rowHex += QStringLiteral(" R%1:[%2%3%4%5%6%7%8%9]").arg(startR+i)
                  .arg(sl[0],2,16,QLatin1Char('0')).arg(sl[1],2,16,QLatin1Char('0'))
                  .arg(sl[2],2,16,QLatin1Char('0')).arg(sl[3],2,16,QLatin1Char('0'))
                  .arg(sl[4],2,16,QLatin1Char('0')).arg(sl[5],2,16,QLatin1Char('0'))
                  .arg(sl[6],2,16,QLatin1Char('0')).arg(sl[7],2,16,QLatin1Char('0'));
          }
          qCritical().noquote() << QStringLiteral("  -> QImage RowPrefix(8B):") << rowHex;
      }

      // ── ★ 历史溯源：打印最近 10 包 RTP 历史，看是否有丢包/空洞影响本帧 ────────
      if (m_rtpSeqHistCount > 0) {
          QString rtpHist = QStringLiteral("  -> Recent RTP: ");
          for (int i = 0; i < qMin(m_rtpSeqHistCount, 10); ++i) {
              int idx = (m_rtpSeqHistIdx - qMin(m_rtpSeqHistCount, 10) + i + kRtpSeqHistSize) % kRtpSeqHistSize;
              rtpHist += QStringLiteral("[%1]").arg(m_rtpSeqHist[idx].seq);
          }
          qCritical().noquote() << rtpHist << QStringLiteral(" ★ 对比本帧 fid=%1 所需 seq，确认是否存在空洞导致解码器 Error Concealment").arg(m_frameIdCounter + 1);
      }

      qCritical().noquote()
          << QStringLiteral("  -> Policy: tag=%1 env=%2 ★ 若 interlaced=1 且 policy 非 off，则条状极可能由 "
                            "VideoInterlacedPolicy 产生")
                     .arg(VideoInterlacedPolicy::diagnosticsTag())
                     .arg(VideoInterlacedPolicy::envRaw());
          qCritical() << "[H264][" << m_streamTag << "][STRIPE_DETECTED] ★★★ 内存检测到异常模式！ shift="
                      << sh.horizontalShift << " fineTop=" << sh.fineTop
                      << " frame#" << m_framesEmitted + 1 << " fid=" << m_frameIdCounter + 1
                      << " addr=" << (void*)dstFrame.constBits() << " bpl=" << dstFrame.bytesPerLine()
                      << " ★ shift!=0: stride/位移; fine=100: 全同行; fine=200: 奇偶行同";
          qCritical().noquote()
              << "[H264][" << m_streamTag << "][STRIPE_VERDICT] verdict="
              << VideoFrameEvidence::stripeVerdictTag(sh.verdict) << " shift=" << sh.horizontalShift
              << " top=" << sh.fineTop << " mid@" << sh.midRow << "=" << sh.fineMid << " bot@" << sh.botRow
              << "=" << sh.fineBot << " hint=" << VideoFrameEvidence::stripeVerdictHintZh(sh.verdict)
              << " analyzeTime=" << decAnalyzeUs << "us";
        }
        
        // ★★★ 5-WHY 闭环修复：实时条状检测触发 IDR 请求 ★★★
        // 如果检测到条状（无论 verdict 是 suspect 还是 fp_top），说明解码输出已不可用。
        // 即便 RTP 序列是完整的，也可能是解码器内部状态损坏。
        if (sh.verdict != VideoFrameEvidence::StripeHeuristicVerdict::Clean) {
            static QHash<QString, int> s_stripeIdrReq;
            if (++s_stripeIdrReq[m_streamTag] <= 5 || (s_stripeIdrReq[m_streamTag] % 50 == 0)) {
                qWarning() << "[H264][" << m_streamTag << "][StripeFeedback] 检测到条状 verdict=" 
                           << (int)sh.verdict << "，主动请求 IDR 以恢复参考帧";
            }
            emitKeyframeSuggestThrottled("stripe_detected_feedback");
            // 注意：不在这里设 m_needKeyframe=true，因为我们还是希望能看到后续帧，直到新 IDR 到达。
            // 设 m_needKeyframe 会导致在收到新 IDR 前所有帧被丢弃（见 flushPending）。
        }
      }

      // ── ★★★ 端到端追踪：色彩转换完成，QImage 帧数据就绪 ★★★ ─────────────
      // dstFrame 已是完整 Format_RGBA8888 (AV_PIX_FMT_RGBA) QImage（来自 3槽帧池），可送入 Qt
      // 信号系统 记录这一刻的时间戳，用于计算 解码→QML 的端到端延迟
      const int64_t colorConvertDoneTime = QDateTime::currentMSecsSinceEpoch();
      m_framesEmitted++;
      m_statsFramesInWindow++;
      framesOut++;
      m_frameIdCounter++;
      if (VideoFrameEvidence::shouldLogVideoStage(m_frameIdCounter)) {
        qInfo().noquote() << VideoFrameEvidence::diagLine("DECODE_OUT", m_streamTag,
                                                          m_frameIdCounter, dstFrame);
      }
      {
        VideoFrameFingerprintCache::Fingerprint fp;
        fp.rowHash = VideoFrameEvidence::rowHashSample(dstFrame);
        fp.fullCrc = VideoFrameEvidence::wantsFullImageCrcForFrame(m_frameIdCounter)
                         ? VideoFrameEvidence::crc32IeeeOverImageBytes(dstFrame)
                         : 0u;
        fp.width = w;
        fp.height = h;
        VideoFrameFingerprintCache::instance().record(m_streamTag, m_frameIdCounter, fp);
      }
      // frameId 用于端到端追踪：feedRtp → frameReady → onVideoFrameFromDecoder → QML handler
      if (m_framesEmitted <= 5) {
        qInfo() << "[H264][" << m_streamTag << "] ★★★ emitDecoded 输出帧 ★★★ #" << m_framesEmitted
                << " frameId=" << m_frameIdCounter << " lifecycleId=" << m_currentLifecycleId.load()
                << " w=" << w << " h=" << h
                << " pool_slot=" << ((m_framePoolIdx + kFramePoolSize - 1) % kFramePoolSize)
                << " rtpSeq=" << m_lastRtpSeq << " codecOpen=" << m_codecOpen
                << " colorConvertDoneMs=" << colorConvertDoneTime
                << " ★ 对比 onVideoFrameFromDecoder frameId=" << m_frameIdCounter
                << " 确认解码→emit 链路";
      }
      {
        const int every = h264DecodeFrameSummaryEveryN();
        if (every > 0 && (m_framesEmitted == 1 || (m_framesEmitted % every) == 0)) {
          const char *const nm = av_get_pix_fmt_name(srcFmt);
          const int thr = m_ctx ? m_ctx->thread_count : -1;
          qInfo().noquote()
              << "[H264][" << m_streamTag << "][FrameSummary] frame#=" << m_framesEmitted
              << " frameId=" << m_frameIdCounter << " lifecycleId=" << m_currentLifecycleId.load()
              << " srcFmt=" << (nm ? nm : "?") << "(" << static_cast<int>(srcFmt) << ")"
              << " linesize=" << frame->linesize[0] << "," << frame->linesize[1] << ","
              << frame->linesize[2] << "," << frame->linesize[3] << " expSlices=" << m_expectedSliceCount
              << " libavThr=" << thr << " forcedThr=" << m_forcedDecodeThreadCount
              << " dstBpl=" << dstFrame.bytesPerLine() << " wxh=" << w << "x" << h
              << " stripeMit=" << (m_stripeMitigationApplied ? 1 : 0)
              << " webrtcHwActive=" << (m_webrtcHwActive ? 1 : 0)
              << " ★条状排障: CLIENT_H264_STRIPE_DIAG CLIENT_VIDEO_FORENSICS "
                 "CLIENT_VIDEO_EVIDENCE_STRIPE_ROWS；契约 bracket= 见 [Client][VideoHealth][Global]";
        }
      }

      {
        // ── ★★★ 端到端追踪：发出 frameReady 信号（进入 Qt 事件队列）★★★ ─────────
        // 注意：QueuedConnection 下，emit 立即返回，实际 handler 在主线程事件循环中执行
        // frameId 必须与 onVideoFrameFromDecoder 中的 frameId 一致
        // ★ 记录 emit 时刻：主线程收到时与 QDateTime::currentMSecsSinceEpoch() 差值 = 事件队列延迟
        m_lastFrameReadyEmitWallMs.store(static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch()),
                                         std::memory_order_release);
        // dstFrame 来自 3槽池：主线程通过 QueuedConnection 接收时 refcount=2（池槽+事件副本）；
        // 解码线程下次使用该槽时 detach() 见 refcount>1 则新分配（安全），无并发写。
        int lastSlotIdx = (m_framePoolIdx + kFramePoolSize - 1) % kFramePoolSize;
        m_slotAudit[lastSlotIdx].status.store(SlotStatus::Queued, std::memory_order_release);
        emit frameReady(dstFrame, m_frameIdCounter);
        m_diagFrameReadyEmitAccum.fetch_add(1, std::memory_order_relaxed);
        // ★★★ 如果此日志不出现但 emitDecoded 日志出现 → QueuedConnection 失效 ★★★
        if (m_framesEmitted <= 5 || (m_framesEmitted % 300) == 0) {
          int queuedConnCount = this->receivers(SIGNAL(frameReady(const QImage &, quint64)));
          qInfo() << "[H264][" << m_streamTag << "] ★★★ emit frameReady 完成 ★★★"
                  << " frameId=" << m_frameIdCounter
                  << " totalEmitted=" << m_framesEmitted
                  << " lifecycleId=" << m_currentLifecycleId.load()
                  << " queuedConnections=" << queuedConnCount
                  << " ★ queuedConnections=0 → WebRtcClient::onVideoFrameFromDecoder 未连接到 "
                     "frameReady 信号！"
                  << " queuedConnections>0 → 链路完整，对比 onVideoFrameFromDecoder frameId 确认";
        }
      }
    }
  }

  av_frame_free(&frame);

  // 默认大幅降频：该路径每帧可达 30+ 次/秒，同步写日志会拖慢解码线程与异步日志队列。
  // CLIENT_H264_LOG_EMIT_BATCH=1 恢复每批一条；否则仅前 3 次输出 + 约每 300 帧一条。
  if (framesOut > 0) {
    const bool everyBatch = qEnvironmentVariableIntValue("CLIENT_H264_LOG_EMIT_BATCH") != 0;
    if (everyBatch || m_framesEmitted <= 3 || (m_framesEmitted % 300) == 0) {
      qDebug() << "[H264][" << m_streamTag << "] emitDecoded: 本次输出" << framesOut
               << "帧，total=" << m_framesEmitted;
    }
  }
}

void H264Decoder::ingestHardwareRgbaFrame(QImage&& rgba, const QString& hwBackendLabel) {
  if (rgba.format() != QImage::Format_RGBA8888 || rgba.isNull()) {
    ++m_statsQualityDropInWindow;
    qWarning() << "[H264][" << m_streamTag << "][HW-E2E] ingest skip invalid image fmt="
                << static_cast<int>(rgba.format());
    return;
  }
  const int w = rgba.width();
  const int h = rgba.height();
  if (w <= 0 || h <= 0) {
    ++m_statsQualityDropInWindow;
    return;
  }

  QImage& slot = m_framePool[m_framePoolIdx];
  m_framePoolIdx = (m_framePoolIdx + 1) % kFramePoolSize;
  slot = std::move(rgba);

  if (slot.bytesPerLine() < w * 4) {
    ++m_statsQualityDropInWindow;
    qWarning() << "[H264][" << m_streamTag << "][HW-E2E] ingest bad stride bpl=" << slot.bytesPerLine()
               << " w=" << w;
    return;
  }

  m_width = w;
  m_height = h;
  m_haveDecodedKeyframe = true;

  m_framesEmitted++;
  m_statsFramesInWindow++;
  m_frameIdCounter++;

  if (VideoFrameEvidence::shouldLogVideoStage(m_frameIdCounter)) {
    qInfo().noquote() << VideoFrameEvidence::diagLine("DECODE_OUT_HW", m_streamTag, m_frameIdCounter,
                                                      slot);
  }
  {
    VideoFrameFingerprintCache::Fingerprint fp;
    fp.rowHash = VideoFrameEvidence::rowHashSample(slot);
    fp.fullCrc = VideoFrameEvidence::wantsFullImageCrcForFrame(m_frameIdCounter)
                     ? VideoFrameEvidence::crc32IeeeOverImageBytes(slot)
                     : 0u;
    fp.width = w;
    fp.height = h;
    VideoFrameFingerprintCache::instance().record(m_streamTag, m_frameIdCounter, fp);
  }

  H264ClientDiag::maybeDumpDecodedFrame(slot, m_streamTag, m_frameIdCounter, &m_diagFrameDumpCount);

  {
    m_lastFrameReadyEmitWallMs.store(static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch()),
                                     std::memory_order_release);
    emit frameReady(slot, m_frameIdCounter);
    m_diagFrameReadyEmitAccum.fetch_add(1, std::memory_order_relaxed);
    if (m_framesEmitted <= 8) {
      qInfo() << "[H264][" << m_streamTag << "][HW-E2E] emit frameReady backend=" << hwBackendLabel
              << " frameId=" << m_frameIdCounter << " lifecycleId=" << m_currentLifecycleId.load()
              << " wxh=" << w << "x" << h << " rtpSeq=" << m_lastRtpSeq;
    }
  }
}

void H264Decoder::ingestHardwareDmaBufFrame(VideoFrame&& vf, const QString& hwBackendLabel) {
  if (vf.memoryType != VideoFrame::MemoryType::DMA_BUF) {
    ++m_statsQualityDropInWindow;
    qWarning() << "[H264][" << m_streamTag << "][HW-DMABUF] ingest skip memoryType="
               << static_cast<int>(vf.memoryType);
    return;
  }
  if (vf.pixelFormat != VideoFrame::PixelFormat::NV12) {
    ++m_statsQualityDropInWindow;
    qWarning() << "[H264][" << m_streamTag << "][HW-DMABUF] ingest skip pixelFormat="
               << static_cast<int>(vf.pixelFormat);
    return;
  }
  const int w = static_cast<int>(vf.width);
  const int h = static_cast<int>(vf.height);
  if (w <= 0 || h <= 0 || vf.dmaBuf.nbPlanes < 2) {
    ++m_statsQualityDropInWindow;
    qWarning() << "[H264][" << m_streamTag << "][HW-DMABUF] ingest bad geom wxh=" << w << "x" << h
               << " planes=" << vf.dmaBuf.nbPlanes;
    return;
  }
  if (vf.dmaBuf.fds[0] < 0) {
    ++m_statsQualityDropInWindow;
    qWarning() << "[H264][" << m_streamTag << "][HW-DMABUF] ingest bad fd0";
    return;
  }

  auto handle = std::make_shared<DmaBufFrameHandle>();
  handle->frame = std::move(vf);

  m_width = w;
  m_height = h;
  m_haveDecodedKeyframe = true;

  m_framesEmitted++;
  m_statsFramesInWindow++;
  m_frameIdCounter++;

  if (VideoFrameEvidence::shouldLogVideoStage(m_frameIdCounter)) {
    const auto &db = handle->frame.dmaBuf;
    qInfo().noquote() << "[H264][" << m_streamTag << "][HW-DMABUF] DECODE_OUT_DMABUF fid="
                      << m_frameIdCounter << " wxh=" << w << "x" << h << " fourcc=0x" << Qt::hex
                      << db.drmFourcc << Qt::dec << " planes=" << db.nbPlanes << " pitchY=" << db.pitches[0]
                      << " pitchUV=" << db.pitches[1] << " offY=" << db.offsets[0] << " offUV=" << db.offsets[1]
                      << " fd0=" << db.fds[0] << " fd1=" << db.fds[1]
                      << " ★ 与 [VideoE2E][RS][applyDmaBuf]/[DMABUF-SG][Layout] 对照";
  }

  {
    m_lastFrameReadyEmitWallMs.store(static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch()),
                                     std::memory_order_release);
    emit frameReadyDmaBuf(handle, m_frameIdCounter);
    m_diagFrameReadyEmitAccum.fetch_add(1, std::memory_order_relaxed);
    if (m_framesEmitted <= 8) {
      qInfo() << "[H264][" << m_streamTag << "][HW-DMABUF] emit frameReadyDmaBuf backend="
              << hwBackendLabel << " frameId=" << m_frameIdCounter
              << " lifecycleId=" << m_currentLifecycleId.load() << " wxh=" << w << "x" << h
              << " rtpSeq=" << m_lastRtpSeq;
    }
  }
}

int H264Decoder::takeAndResetFrameReadyEmitDiagCount() {
  return m_diagFrameReadyEmitAccum.exchange(0, std::memory_order_acq_rel);
}

void H264Decoder::onDmaBufSceneGraphPresentFailed() {
  if (m_webrtcHw && m_webrtcHwActive) {
    m_webrtcHw->disableDmaBufExportForCpuFallback();
    logVideoStreamHealthContract(QStringLiteral("dma_cpu_fallback"));
  }
}

QString H264Decoder::buildVideoStreamHealthDetailLine() const {
  ClientVideoStreamHealth::globalPolicy();
  const ClientVideoGlobalPolicySnapshot& g = ClientVideoStreamHealth::globalPolicy();
  QString part;
  if (m_webrtcHwActive && m_webrtcHw) {
    part = QStringLiteral(
               "decodePath=HW_E2E backend=%1 exportDma=%2 sgEnv=%3 sgBroken=%4 bridgeOpen=%5 "
               "stripeMit=%6 expSlices=%7 sz=%8x%9")
               .arg(m_webrtcHw->backendLabel())
               .arg(m_webrtcHw->preferDmaBufExport() ? 1 : 0)
               .arg(m_webrtcHw->dmaBufSceneGraphEnvWanted() ? 1 : 0)
               .arg(m_webrtcHw->dmaBufSgBroken() ? 1 : 0)
               .arg(m_webrtcHw->isActive() ? 1 : 0)
               .arg(m_stripeMitigationApplied ? 1 : 0)
               .arg(m_expectedSliceCount)
               .arg(m_width)
               .arg(m_height);
  } else {
    const int libavThr = m_ctx ? m_ctx->thread_count : -1;
    const bool hwBridgeExists = static_cast<bool>(m_webrtcHw);
    const int bridgeAct = (m_webrtcHw && m_webrtcHw->isActive()) ? 1 : 0;
    part = QStringLiteral(
               "decodePath=SW_LAVC libavThr=%1 forcedThr=%2 codecOpen=%3 stripeMit=%4 expSlices=%5 "
               "hwDecodeEnv=%6 hwBridgeObj=%7 hwBridgeDecOpen=%8 sz=%9x%10")
               .arg(libavThr)
               .arg(m_forcedDecodeThreadCount)
               .arg(m_codecOpen ? 1 : 0)
               .arg(m_stripeMitigationApplied ? 1 : 0)
               .arg(m_expectedSliceCount)
               .arg(g.webRtcHwDecodeEffective ? 1 : 0)
               .arg(hwBridgeExists ? 1 : 0)
               .arg(bridgeAct)
               .arg(m_width)
               .arg(m_height);
  }
  return part + QLatin1Char(' ') + g.formatGlobalBracket();
}

void H264Decoder::logVideoStreamHealthContract(const QString& phase) const {
  qInfo().noquote() << "[Client][VideoHealth][Stream] phase=" << phase << " stream=" << m_streamTag
                    << " " << buildVideoStreamHealthDetailLine();
}

void H264Decoder::releaseFrame(quint64 frameId) {
  if (frameId == 0)
    return;
  // 遍历寻找匹配 frameId 的槽位并释放
  for (int i = 0; i < kFramePoolSize; ++i) {
    if (m_slotAudit[i].lastFid.load(std::memory_order_acquire) == frameId) {
      SlotStatus prev = m_slotAudit[i].status.exchange(SlotStatus::Idle, std::memory_order_acq_rel);
      if (prev == SlotStatus::Queued || prev == SlotStatus::Reading) {
        // 正常释放
      }
      return;
    }
  }
}

double H264Decoder::poolPressure() const {
  int busy = 0;
  for (int i = 0; i < kFramePoolSize; ++i) {
    SlotStatus s = m_slotAudit[i].status.load(std::memory_order_acquire);
    // Queued: 在 Qt 事件队列中；Reading: QML 正在渲染；Decoding: 解码器正在写
    if (s == SlotStatus::Queued || s == SlotStatus::Reading || s == SlotStatus::Decoding) {
      busy++;
    }
  }
  return static_cast<double>(busy) / kFramePoolSize;
}
