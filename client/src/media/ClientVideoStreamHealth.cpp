#include "ClientVideoStreamHealth.h"

#include "core/configuration.h"
#include "media/H264WebRtcHwBridge.h"
#include "media/VideoFrameEvidenceDiag.h"
#include "media/VideoInterlacedPolicy.h"
#include "media/VideoSwsColorHelper.h"

#include <QDebug>
#include <QtGlobal>

#include <mutex>

namespace {

bool envTruthyLoose(const QByteArray& raw) {
  if (raw.isEmpty())
    return true;
  const QString t = QString::fromLatin1(raw).trimmed().toLower();
  return t != QLatin1String("0") && t != QLatin1String("false") && t != QLatin1String("off") &&
         t != QLatin1String("no");
}

bool webRtcDmabufSgEnvEffectiveFromEnv() {
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

bool videoDecoupledPresentFromEnv() {
  const QByteArray v = qgetenv("CLIENT_VIDEO_DECOUPLED_PRESENT");
  if (v.isEmpty())
    return true;
  const QByteArray vl = v.trimmed().toLower();
  return vl != "0" && vl != "false" && vl != "off" && vl != "no";
}

bool cpuPresentFormatStrictFromEnvRaw(const QByteArray &raw) {
  if (raw.isEmpty())
    return true;
  const QString t = QString::fromLatin1(raw).trimmed().toLower();
  return !(t == QLatin1String("0") || t == QLatin1String("false") || t == QLatin1String("off") ||
           t == QLatin1String("no"));
}

bool dmabufSgEnvExplicitlyOn(const QString &raw) {
  if (raw.isEmpty())
    return false;
  const QString t = raw.trimmed().toLower();
  return t == QLatin1String("1") || t == QLatin1String("true") || t == QLatin1String("yes") ||
         t == QLatin1String("on");
}

}  // namespace

ClientVideoGlobalPolicySnapshot ClientVideoGlobalPolicySnapshot::capture() {
  ClientVideoGlobalPolicySnapshot s;
  s.webRtcHwDecodeEnvRaw = QString::fromUtf8(qgetenv("CLIENT_WEBRTC_HW_DECODE"));
  s.webRtcHwDecodeEffective = H264WebRtcHwBridge::hardwareDecodeRequested();
  s.mediaRequireHardwareDecode = Configuration::instance().requireHardwareDecode();

#if defined(ENABLE_VAAPI)
  s.hwDecodeVaapiCompiled = true;
#else
  s.hwDecodeVaapiCompiled = false;
#endif
#if defined(ENABLE_NVDEC)
  s.hwDecodeNvdecCompiled = true;
#else
  s.hwDecodeNvdecCompiled = false;
#endif

#if defined(ENABLE_FFMPEG)
  s.ffmpegCompiled = true;
#else
  s.ffmpegCompiled = false;
#endif
#if defined(ENABLE_EGL_DMABUF)
  s.eglDmabufCompiled = true;
#else
  s.eglDmabufCompiled = false;
#endif

  s.webRtcDmabufSgEnvRaw = QString::fromUtf8(qgetenv("CLIENT_WEBRTC_DMABUF_SG"));

  s.webRtcHwExportDmaBufEnv = qEnvironmentVariableIntValue("CLIENT_WEBRTC_HW_EXPORT_DMABUF");

  s.webRtcDmabufSgEnvEffective = webRtcDmabufSgEnvEffectiveFromEnv();
#if defined(CLIENT_HAVE_NV12_DMABUF_SG) && defined(ENABLE_EGL_DMABUF)
  s.nv12DmabufSgCompiled = true;
#else
  s.nv12DmabufSgCompiled = false;
#endif

  const QByteArray tcRaw = qgetenv("CLIENT_FFMPEG_DECODE_THREADS");
  s.ffmpegDecodeThreadsEnvRaw = QString::fromUtf8(tcRaw);
  bool tcOk = false;
  const int tcVal = tcRaw.isEmpty() ? 0 : tcRaw.toInt(&tcOk);
  s.ffmpegDecodeThreadsParsedDefault = (tcOk && tcVal > 0) ? tcVal : 1;

  const QByteArray mitRaw = qgetenv("CLIENT_H264_STRIPE_AUTO_MITIGATION");
  s.stripeAutoMitigationEnvRaw = QString::fromUtf8(mitRaw);
  s.stripeAutoMitigationEffective = envTruthyLoose(mitRaw);

  s.libglAlwaysSoftware = QString::fromUtf8(qgetenv("LIBGL_ALWAYS_SOFTWARE"));
  s.qsgRhiBackend = QString::fromUtf8(qgetenv("QSG_RHI_BACKEND"));
  s.clientAssumeSoftwareGl = QString::fromUtf8(qgetenv("CLIENT_ASSUME_SOFTWARE_GL"));
  s.videoDecoupledPresentEffective = videoDecoupledPresentFromEnv();

  s.h264DecodeFrameSummaryEveryEnvRaw =
      QString::fromUtf8(qgetenv("CLIENT_H264_DECODE_FRAME_SUMMARY_EVERY"));
  {
    bool ok = false;
    const int v = qEnvironmentVariableIntValue("CLIENT_H264_DECODE_FRAME_SUMMARY_EVERY", &ok);
    s.decodeFrameSummaryEveryEffective = ok ? qMax(0, v) : 60;
  }
  s.h264StripeDiagEnv = qEnvironmentVariableIntValue("CLIENT_H264_STRIPE_DIAG");

  const QByteArray cfs = qgetenv("CLIENT_VIDEO_CPU_PRESENT_FORMAT_STRICT");
  s.cpuPresentFormatStrictEnvRaw = QString::fromUtf8(cfs);
  s.cpuPresentFormatStrictEffective = cpuPresentFormatStrictFromEnvRaw(cfs);

  s.interlacedPolicyEnvRaw = VideoInterlacedPolicy::envRaw();
  s.interlacedPolicyTag = VideoInterlacedPolicy::diagnosticsTag();
  s.swsColorspaceEnvRaw = QString::fromUtf8(qgetenv("CLIENT_VIDEO_SWS_COLORSPACE"));
  s.swsColorspaceTag = videoSwsColorspaceDiagnosticsTag();
  return s;
}

QString ClientVideoGlobalPolicySnapshot::formatGlobalBracket() const {
  return QStringLiteral(
             "G[hwDec=%1,reqHw=%2,exDma=%3,sgEnv=%4,sgBld=%5,thrDef=%6,strAuto=%7,swGL=%8,rhi=%9,"
             "decoup=%10,dfSum=%11,strD=%12,cpuFmtStr=%13,vaC=%14,nvC=%15,ffC=%16,eglC=%17,ilace=%18,"
             "yuvM=%19]")
      .arg(webRtcHwDecodeEffective ? 1 : 0)
      .arg(mediaRequireHardwareDecode ? 1 : 0)
      .arg(webRtcHwExportDmaBufEnv)
      .arg(webRtcDmabufSgEnvEffective ? 1 : 0)
      .arg(nv12DmabufSgCompiled ? 1 : 0)
      .arg(ffmpegDecodeThreadsParsedDefault)
      .arg(stripeAutoMitigationEffective ? 1 : 0)
      .arg(libglAlwaysSoftware.isEmpty() ? QStringLiteral("-") : libglAlwaysSoftware)
      .arg(qsgRhiBackend.isEmpty() ? QStringLiteral("-") : qsgRhiBackend)
      .arg(videoDecoupledPresentEffective ? 1 : 0)
      .arg(decodeFrameSummaryEveryEffective)
      .arg(h264StripeDiagEnv)
      .arg(cpuPresentFormatStrictEffective ? 1 : 0)
      .arg(hwDecodeVaapiCompiled ? 1 : 0)
      .arg(hwDecodeNvdecCompiled ? 1 : 0)
      .arg(ffmpegCompiled ? 1 : 0)
      .arg(eglDmabufCompiled ? 1 : 0)
      .arg(interlacedPolicyTag.isEmpty() ? QStringLiteral("-") : interlacedPolicyTag)
      .arg(swsColorspaceTag.isEmpty() ? QStringLiteral("-") : swsColorspaceTag);
}

namespace ClientVideoStreamHealth {

static std::mutex g_policyMutex;
static ClientVideoGlobalPolicySnapshot g_cachedPolicy;
static std::atomic_bool g_policyInit{false};

const ClientVideoGlobalPolicySnapshot& globalPolicy() {
  if (g_policyInit.load(std::memory_order_acquire))
    return g_cachedPolicy;
  std::lock_guard<std::mutex> lock(g_policyMutex);
  if (!g_policyInit.load(std::memory_order_relaxed)) {
    g_cachedPolicy = ClientVideoGlobalPolicySnapshot::capture();
    g_policyInit.store(true, std::memory_order_release);
  }
  return g_cachedPolicy;
}

void logGlobalEnvOnce() {
  static std::once_flag once;
  std::call_once(once, []() {
    const ClientVideoGlobalPolicySnapshot& g = globalPolicy();
    const bool hwDecodeCompiled = g.hwDecodeVaapiCompiled || g.hwDecodeNvdecCompiled;
    const bool swRaster = displayStackAssumedSoftwareRaster();
    int contractErrors = 0;

    if (g.webRtcHwDecodeEffective && !hwDecodeCompiled) {
      ++contractErrors;
      qCritical().noquote()
          << "[Client][VideoHealth][ERROR] 策略与构建不一致：media.hardware_decode 生效（硬解请求=ON，"
             "CLIENT_WEBRTC_HW_DECODE_raw="
          << g.webRtcHwDecodeEnvRaw
          << " require_hw="
          << (g.mediaRequireHardwareDecode ? 1 : 0)
          << "）但本二进制未编译硬件解码（VA-API="
          << (g.hwDecodeVaapiCompiled ? QLatin1String("ON") : QLatin1String("OFF"))
          << " NVDEC="
          << (g.hwDecodeNvdecCompiled ? QLatin1String("ON") : QLatin1String("OFF"))
          << "）。Intel/AMD：安装 libva-dev、libdrm-dev 后重新 cmake；NVIDIA：ENABLE_NVDEC=ON（默认）且 "
             "FFmpeg 带 CUDA。"
             "若故意使用软解：media.hardware_decode=false 或 CLIENT_MEDIA_HARDWARE_DECODE=0，并设 "
             "media.require_hardware_decode=false。";
    }

    if (dmabufSgEnvExplicitlyOn(g.webRtcDmabufSgEnvRaw) && !g.nv12DmabufSgCompiled) {
      ++contractErrors;
      qCritical().noquote()
          << "[Client][VideoHealth][ERROR] 策略与构建不一致：CLIENT_WEBRTC_DMABUF_SG 显式开启（raw="
          << g.webRtcDmabufSgEnvRaw
          << "）但本二进制未编译 NV12 DMA-BUF Scene Graph（需 ENABLE_EGL_DMABUF + Qt ShaderTools + "
             "CLIENT_HAVE_NV12_DMABUF_SG）。请用完整 client-dev 依赖重新 cmake，或置 0|false|off。";
    }

    if (g.webRtcHwExportDmaBufEnv != 0 && !g.eglDmabufCompiled && hwDecodeCompiled) {
      qWarning().noquote()
          << "[Client][VideoHealth][WARN] CLIENT_WEBRTC_HW_EXPORT_DMABUF="
          << g.webRtcHwExportDmaBufEnv
          << " 但 EGL DMA-BUF 未编译（eglDmabuf=OFF）；零拷贝导出可能降级或失败。需 ENABLE_EGL_DMABUF "
             "与 libegl+libdrm。";
    }

    if (g.webRtcHwExportDmaBufEnv != 0 && swRaster &&
        qEnvironmentVariableIntValue("CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL") == 0) {
      qInfo().noquote()
          << "[Client][VideoHealth][INFO] 软件光栅栈下 DMA-BUF 导出通常关闭（effective="
          << (effectivePreferDmaBufExport() ? 1 : 0)
          << "）；若刻意测试可设 CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL=1。";
    }

    const int crcDiag = VideoFrameEvidence::fullCrcSampleEveryForDiagnostics();
    const QString crcTok = VideoFrameEvidence::fullCrcModeStartupToken();
    qInfo().noquote()
        << "[Client][VideoHealth][Contract] schema=1"
        << " verdict=" << (contractErrors > 0 ? QLatin1String("ERROR") : QLatin1String("OK"))
        << " errors=" << contractErrors
        << " ffmpeg_compiled=" << (g.ffmpegCompiled ? QLatin1String("ON") : QLatin1String("OFF"))
        << " eglDmabuf_compiled=" << (g.eglDmabufCompiled ? QLatin1String("ON") : QLatin1String("OFF"))
        << " hwDecode_env=" << (g.webRtcHwDecodeEffective ? 1 : 0)
        << " hwDecode_compiled=" << (hwDecodeCompiled ? 1 : 0)
        << " dmaExport_env=" << g.webRtcHwExportDmaBufEnv
        << " dmaSg_env_raw=" << g.webRtcDmabufSgEnvRaw
        << " nv12Sg_compiled=" << (g.nv12DmabufSgCompiled ? 1 : 0)
        << " swRaster_assumed=" << (swRaster ? 1 : 0)
        << " evidence_chain=" << (VideoFrameEvidence::chainEnabled() ? 1 : 0)
        << " fullCrc_diag=" << crcDiag
        << " fullCrc_mode=" << crcTok
        << " ★verdict=ERROR 时先修构建/环境再排视频花屏；fullCrc_mode=sample 时默认每 N 帧整图 CRC（见 "
           "CLIENT_VIDEO_EVIDENCE_FULL_CRC_EVERY）";

    qInfo().noquote()
        << "[Client][VideoHealth][Global] schema=1"
        << " CLIENT_WEBRTC_HW_DECODE_raw=" << g.webRtcHwDecodeEnvRaw
        << " hw_decode_effective=" << g.webRtcHwDecodeEffective
        << " media.require_hardware_decode=" << (g.mediaRequireHardwareDecode ? 1 : 0)
        << " VA-API_compiled=" << (g.hwDecodeVaapiCompiled ? QLatin1String("ON") : QLatin1String("OFF"))
        << " NVDEC_compiled=" << (g.hwDecodeNvdecCompiled ? QLatin1String("ON") : QLatin1String("OFF"))
        << " FFmpeg_compiled=" << (g.ffmpegCompiled ? QLatin1String("ON") : QLatin1String("OFF"))
        << " EGL_DMABUF_compiled=" << (g.eglDmabufCompiled ? QLatin1String("ON") : QLatin1String("OFF"))
        << " CLIENT_WEBRTC_DMABUF_SG_raw=" << g.webRtcDmabufSgEnvRaw
        << " CLIENT_WEBRTC_HW_EXPORT_DMABUF=" << g.webRtcHwExportDmaBufEnv
        << " CLIENT_WEBRTC_DMABUF_SG_effective=" << g.webRtcDmabufSgEnvEffective
        << " NV12_DMABUF_SG_compiled=" << g.nv12DmabufSgCompiled
        << " CLIENT_FFMPEG_DECODE_THREADS_raw=" << g.ffmpegDecodeThreadsEnvRaw
        << " parsedDefaultThr=" << g.ffmpegDecodeThreadsParsedDefault
        << " CLIENT_H264_STRIPE_AUTO_MITIGATION_raw=" << g.stripeAutoMitigationEnvRaw
        << " effective=" << g.stripeAutoMitigationEffective
        << " LIBGL_ALWAYS_SOFTWARE=" << g.libglAlwaysSoftware
        << " QSG_RHI_BACKEND=" << g.qsgRhiBackend
        << " CLIENT_ASSUME_SOFTWARE_GL=" << g.clientAssumeSoftwareGl
        << " CLIENT_VIDEO_DECOUPLED_PRESENT_effective=" << g.videoDecoupledPresentEffective
        << " CLIENT_H264_DECODE_FRAME_SUMMARY_EVERY_raw=" << g.h264DecodeFrameSummaryEveryEnvRaw
        << " effective_dfSum=" << g.decodeFrameSummaryEveryEffective
        << " CLIENT_H264_STRIPE_DIAG=" << g.h264StripeDiagEnv
        << " CLIENT_VIDEO_CPU_PRESENT_FORMAT_STRICT_raw=" << g.cpuPresentFormatStrictEnvRaw
        << " effective_cpuFmtStrict=" << (g.cpuPresentFormatStrictEffective ? 1 : 0)
        << " CLIENT_VIDEO_INTERLACED_POLICY_raw=" << g.interlacedPolicyEnvRaw
        << " ilaceTag=" << g.interlacedPolicyTag
        << " CLIENT_VIDEO_SWS_COLORSPACE_raw=" << g.swsColorspaceEnvRaw
        << " yuvMatrixTag=" << g.swsColorspaceTag
        << " CLIENT_VIDEO_EVIDENCE_CHAIN_raw=" << QString::fromUtf8(qgetenv("CLIENT_VIDEO_EVIDENCE_CHAIN"))
        << " CLIENT_VIDEO_FORENSICS_raw=" << QString::fromUtf8(qgetenv("CLIENT_VIDEO_FORENSICS"))
        << " CLIENT_VIDEO_EVIDENCE_STRIPE_ROWS_raw="
        << QString::fromUtf8(qgetenv("CLIENT_VIDEO_EVIDENCE_STRIPE_ROWS"))
        << " CLIENT_VIDEO_EVIDENCE_FULL_CRC_EVERY_raw="
        << QString::fromUtf8(qgetenv("CLIENT_VIDEO_EVIDENCE_FULL_CRC_EVERY"))
        << " fullCrc_mode=" << VideoFrameEvidence::fullCrcModeStartupToken()
        << " bracket=" << g.formatGlobalBracket()
        << " ★显示正确性策略：software_raster 时 effective DMA export="
        << (effectivePreferDmaBufExport() ? 1 : 0)
        << "（见 CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL）；软栈多线程解码限制见 "
           "CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL"
        << " ★多路并行：每路解码契约见 [Client][VideoHealth][Stream] stream=…"
        << " ★CPU 呈现契约（RemoteVideoSurface+QVideoSink）：默认仅 RGBA8888+bpl>=4*w；"
           "关闭严格：CLIENT_VIDEO_CPU_PRESENT_FORMAT_STRICT=0";
  });
}

void fillJsonClientVideoPolicy(QJsonObject& out) {
  const ClientVideoGlobalPolicySnapshot& g = globalPolicy();
  out.insert(QStringLiteral("schemaVersion"), 1);
  out.insert(QStringLiteral("clientWebRtcHwDecodeEffective"), g.webRtcHwDecodeEffective);
  out.insert(QStringLiteral("clientMediaRequireHardwareDecode"), g.mediaRequireHardwareDecode);
  out.insert(QStringLiteral("clientWebRtcHwDecodeEnv"), g.webRtcHwDecodeEnvRaw);
  out.insert(QStringLiteral("clientHwDecodeVaapiCompiled"), g.hwDecodeVaapiCompiled);
  out.insert(QStringLiteral("clientHwDecodeNvdecCompiled"), g.hwDecodeNvdecCompiled);
  out.insert(QStringLiteral("clientFfmpegCompiled"), g.ffmpegCompiled);
  out.insert(QStringLiteral("clientEglDmabufCompiled"), g.eglDmabufCompiled);
  out.insert(QStringLiteral("clientWebRtcDmabufSgEnvRaw"), g.webRtcDmabufSgEnvRaw);
  out.insert(QStringLiteral("clientWebRtcHwExportDmaBuf"), g.webRtcHwExportDmaBufEnv);
  out.insert(QStringLiteral("clientWebRtcHwExportDmaBufEffective"), effectivePreferDmaBufExport());
  out.insert(QStringLiteral("clientWebRtcDmabufSgEffective"), g.webRtcDmabufSgEnvEffective);
  out.insert(QStringLiteral("clientNv12DmabufSgCompiled"), g.nv12DmabufSgCompiled);
  out.insert(QStringLiteral("clientFfmpegDecodeThreadsEnv"), g.ffmpegDecodeThreadsEnvRaw);
  out.insert(QStringLiteral("clientFfmpegDecodeThreadsParsedDefault"),
             g.ffmpegDecodeThreadsParsedDefault);
  out.insert(QStringLiteral("clientStripeAutoMitigationEffective"), g.stripeAutoMitigationEffective);
  out.insert(QStringLiteral("clientStripeAutoMitigationEnv"), g.stripeAutoMitigationEnvRaw);
  out.insert(QStringLiteral("clientLibglAlwaysSoftware"), g.libglAlwaysSoftware);
  out.insert(QStringLiteral("clientQsgRhiBackend"), g.qsgRhiBackend);
  out.insert(QStringLiteral("clientVideoDecoupledPresentEffective"),
             g.videoDecoupledPresentEffective);
  out.insert(QStringLiteral("clientH264DecodeFrameSummaryEveryEnv"), g.h264DecodeFrameSummaryEveryEnvRaw);
  out.insert(QStringLiteral("clientH264DecodeFrameSummaryEveryEffective"), g.decodeFrameSummaryEveryEffective);
  out.insert(QStringLiteral("clientH264StripeDiagEnv"), g.h264StripeDiagEnv);
  out.insert(QStringLiteral("clientCpuPresentFormatStrictEnv"), g.cpuPresentFormatStrictEnvRaw);
  out.insert(QStringLiteral("clientCpuPresentFormatStrictEffective"), g.cpuPresentFormatStrictEffective);
  out.insert(QStringLiteral("clientVideoInterlacedPolicyEnv"), g.interlacedPolicyEnvRaw);
  out.insert(QStringLiteral("clientVideoInterlacedPolicyTag"), g.interlacedPolicyTag);
  out.insert(QStringLiteral("clientVideoSwsColorspaceEnv"), g.swsColorspaceEnvRaw);
  out.insert(QStringLiteral("clientVideoSwsColorspaceTag"), g.swsColorspaceTag);
  out.insert(QStringLiteral("clientVideoHealthBracket"), g.formatGlobalBracket());
  out.insert(QStringLiteral("clientDisplaySoftwareRasterAssumed"), displayStackAssumedSoftwareRaster());
  out.insert(QStringLiteral("clientVideoEvidenceFullCrcDiagnostic"),
             VideoFrameEvidence::fullCrcSampleEveryForDiagnostics());
  out.insert(QStringLiteral("clientVideoEvidenceFullCrcMode"), VideoFrameEvidence::fullCrcModeStartupToken());
}

bool displayStackAssumedSoftwareRaster() {
  const QByteArray libgl = qgetenv("LIBGL_ALWAYS_SOFTWARE");
  if (libgl.trimmed() == QByteArrayLiteral("1"))
    return true;
  const QByteArray a = qgetenv("CLIENT_ASSUME_SOFTWARE_GL");
  if (a.isEmpty())
    return false;
  const QString t = QString::fromLatin1(a).trimmed().toLower();
  return t == QLatin1String("1") || t == QLatin1String("true") || t == QLatin1String("yes") ||
         t == QLatin1String("on");
}

bool effectivePreferDmaBufExport() {
  const bool envWants = qEnvironmentVariableIntValue("CLIENT_WEBRTC_HW_EXPORT_DMABUF") != 0;
  if (qEnvironmentVariableIntValue("CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL") != 0)
    return envWants;
  if (!displayStackAssumedSoftwareRaster())
    return envWants;
  static std::once_flag s_logOnce;
  if (envWants) {
    std::call_once(s_logOnce, []() {
      qInfo().noquote()
          << "[Client][VideoHealth][Policy] software_raster_assumed=Y → effective "
             "CLIENT_WEBRTC_HW_EXPORT_DMABUF=0（避免 DMA-BUF→EGL 在 llvmpipe/软件栈花屏或丢帧）。"
             " 若确需尝试：CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL=1";
    });
  }
  return false;
}

bool shouldForceSingleThreadDecodeUnderSoftwareRaster(int envRequestedThreads) {
  if (envRequestedThreads <= 1)
    return false;
  if (qEnvironmentVariableIntValue("CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL") != 0)
    return false;
  if (!displayStackAssumedSoftwareRaster())
    return false;
  static std::once_flag s_logOnce;
  std::call_once(s_logOnce, []() {
    qInfo().noquote()
        << "[Client][VideoHealth][Policy] software_raster_assumed=Y → libavcodec slice "
           "thread_count 上限为 1（多 slice×多线程条状风险）。放行："
           "CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL=1";
  });
  return true;
}

bool cpuPresentFormatStrict() {
  return cpuPresentFormatStrictFromEnvRaw(qgetenv("CLIENT_VIDEO_CPU_PRESENT_FORMAT_STRICT"));
}

void debugResetPolicyCacheForTest() {
  std::lock_guard<std::mutex> lock(g_policyMutex);
  g_policyInit.store(false, std::memory_order_release);
}

}  // namespace ClientVideoStreamHealth
