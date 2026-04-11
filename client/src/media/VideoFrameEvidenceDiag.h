#ifndef VIDEO_FRAME_EVIDENCE_DIAG_H
#define VIDEO_FRAME_EVIDENCE_DIAG_H

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QtGlobal>

/**
 * 端到端视频证据链（条状/花屏/错位排障）
 *
 * **排障首选（与 rowHash 互补）**：`CLIENT_VIDEO_SAVE_FRAME=png` 落盘解码后 RGBA（Scene Graph 前），
 * 文件名含 `_f<frameId>`；在日志中按 **同一 stream、同一 frameId/fid** 对齐本模块打印的各 stage 与
 * `[H264][FrameDump]`。先看清 PNG 是否已坏，再判断是解码/sws 还是纹理/合成。
 *
 * 开启：
 *   CLIENT_VIDEO_EVIDENCE_CHAIN=1  — 强制开启
 *   CLIENT_VIDEO_EVIDENCE_CHAIN=0|false|off — 强制关闭
 *   未设置时：Debug 构建（未定义 NDEBUG）默认开启；Release 默认关闭
 *
 * 解码侧周期性摘要（与 [Client][VideoHealth][Global] 中 dfSum / bracket 对齐）：
 *   CLIENT_H264_DECODE_FRAME_SUMMARY_EVERY  未设置默认 60 — 每 N 帧一条 [H264][…][FrameSummary]（srcFmt、
 * linesize、expSlices、libavThr）；0=关闭
 *
 * 可选采样（减日志量 / 拉长观察窗）：
 *   CLIENT_VIDEO_EVIDENCE_EARLY_MAX   Debug 默认 24 / Release 默认 10  — 前 N 帧每帧打一条（未设
 * env 时） CLIENT_VIDEO_EVIDENCE_INTERVAL    默认 60  — 之后每 N 帧一条；设为 0 表示仅打前
 * EARLY_MAX 帧
 *
 * rowHash 采样模式：
 *   CLIENT_VIDEO_EVIDENCE_STRIPE_ROWS 默认 1  — 1=首行+次行+中间行+末行（更易抓整行错位/条状）；
 *                                          0=仅首两行（与最早实现数值兼容，灵敏度较低）
 *   日志字段 rowSample= 即本模式（旧字段名 stripe 易误解为「已检测到条带」）。
 *
 * 一键取证（推荐排障时开启）：
 *   CLIENT_VIDEO_FORENSICS=1 — 等价于 FULL_CRC 打开 + 下游 RemoteVideoSurface 提高 TexSize/SG 日志量
 *
 * 面板绑定 / DPR / 限频 PNG（RemoteVideoSurface::commitCpuTextureFrame + WebRtcClient::bindVideoSurface）：
 *   CLIENT_VIDEO_DEBUG_PNG_DIR — 非空则每路按 CLIENT_VIDEO_DEBUG_PNG_INTERVAL_MS（默认 30000）落盘 PNG
 *   CLIENT_VIDEO_DEBUG_GEOM=1 — 除首帧 GeomOnce 外每 60s 打几何+DPR
 *
 * 四路视频「环节级」详细 trace（条状/花屏定界，**按 stream 采样**，不混用全局计数）：
 *   CLIENT_VIDEO_PIPELINE_TRACE=1 — 在 RTP 入队后关键路径上追加 [Client][VideoE2E] 行（含 stride/pitch/fourcc）；
 *   与 CLIENT_VIDEO_EVIDENCE_CHAIN 可叠加；未开本开关时仍可用 EVIDENCE_CHAIN 看像素指纹。
 *   CLIENT_VIDEO_PIPELINE_TRACE_EARLY  默认 48 — 前 N 帧每帧打环节摘要（每路独立 frameId）；
 *   CLIENT_VIDEO_PIPELINE_TRACE_INTERVAL 默认 30 — 之后每 N 帧一条；0=仅 EARLY。
 *
 * 全缓冲 CRC（排除稀疏采样漏检；CPU 开销大）：
 *   CLIENT_VIDEO_EVIDENCE_FULL_CRC 非 0 — 每条证据行都算 fullCrc（IEEE CRC32，整幅 img.sizeInBytes()）
 *   或由 CLIENT_VIDEO_FORENSICS=1 自动启用（等价「全开」）
 *   CLIENT_VIDEO_EVIDENCE_FULL_CRC_EVERY — 未设置且证据链开启时默认 60：在 shouldLogVideoStage 为真的帧上，
 *   对前 EARLY_MAX 帧每帧算 CRC，之后每 N 帧算一次（与 rowHash 互补，减轻漏检）；显式设为 0 可关闭默认采样。
 *
 * 呈现路径完整性（V2，默认开；与 VideoFrameFingerprintCache + RemoteVideoSurface 联动）：
 *   CLIENT_VIDEO_PRESENT_INTEGRITY_CHECK=0|false|off — 关闭纹理尺寸 / 解码指纹 vs SG 对比与 EventBus 告警
 *
 * 同一 frameId 下对比各 stage 的 w/h、fmt、bpl、rowHash：
 * - 若 DECODE_OUT == MAIN_PRESENT == RS_APPLY == SG_UPLOAD → 像素在客户端未损坏，疑 UI
 * 遮挡/合成/DPR
 * - 若 DECODE_OUT 与 MAIN_PRESENT 不一致 → 解码后至主线程呈现之间（队列/合帧/错误槽）
 * - 若 MAIN_PRESENT 与 RS_APPLY 不一致 → applyFrame 前拷贝/移动异常（极少）
 * - 若 RS_APPLY 与 SG_UPLOAD 不一致 → updatePaintNode 取帧与 apply 不同步或并发可见性（疑
 * mutex/时序）
 * - rowPad!=0 为正常行填充；minBpl<0 表示非常见格式未算 bpp
 */
namespace VideoFrameEvidence {

/** Debug 构建且未显式关证据链时，前 N 帧默认多采一些，便于一次跑 log 定界 */
inline int defaultEvidenceEarlyMax() {
#ifndef NDEBUG
  return 24;
#else
  return 10;
#endif
}

inline bool chainEnabled() {
  const QByteArray raw = qgetenv("CLIENT_VIDEO_EVIDENCE_CHAIN");
  if (!raw.isEmpty()) {
    const QByteArray tl = raw.trimmed().toLower();
    if (tl == "0" || tl == "false" || tl == "off" || tl == "no")
      return false;
    bool ok = false;
    const int v = raw.trimmed().toInt(&ok);
    if (ok)
      return v != 0;
    return true;
  }
#ifndef NDEBUG
  return true;
#else
  return false;
#endif
}

inline int evidenceEnvInt(const char *name, int defaultValue) {
  const QByteArray raw = qgetenv(name);
  if (raw.isEmpty())
    return defaultValue;
  bool ok = false;
  const int v = raw.toInt(&ok);
  return ok ? v : defaultValue;
}

inline bool stripeRowsExtended() {
  return evidenceEnvInt("CLIENT_VIDEO_EVIDENCE_STRIPE_ROWS", 1) != 0;
}

inline bool videoForensicsPackEnabled() {
  const QByteArray raw = qgetenv("CLIENT_VIDEO_FORENSICS");
  if (raw.isEmpty())
    return false;
  const QByteArray tl = raw.trimmed().toLower();
  if (tl == "0" || tl == "false" || tl == "off" || tl == "no")
    return false;
  return true;
}

inline bool fullImageCrcEnabled() {
  if (videoForensicsPackEnabled())
    return true;
  return evidenceEnvInt("CLIENT_VIDEO_EVIDENCE_FULL_CRC", 0) != 0;
}

/**
 * 启动日志与采样策略：-1 = 每条证据行都算 fullCrc（FORENSICS 或 CLIENT_VIDEO_EVIDENCE_FULL_CRC）；
 * 0 = 仅当显式 FULL_CRC_EVERY=0 或未开链且无 FULL_CRC 时关闭；
 * N>0 = 在 shouldLogVideoStage 为真时，前 EARLY_MAX 帧每帧 + 之后每 N 帧算 fullCrc。
 */
inline int fullCrcSampleEveryForDiagnostics() {
  if (fullImageCrcEnabled())
    return -1;
  const QByteArray raw = qgetenv("CLIENT_VIDEO_EVIDENCE_FULL_CRC_EVERY");
  if (!raw.isEmpty()) {
    bool ok = false;
    const int v = raw.trimmed().toInt(&ok);
    return ok ? v : 0;
  }
  if (chainEnabled())
    return 60;
  return 0;
}

inline QString fullCrcModeStartupToken() {
  const int d = fullCrcSampleEveryForDiagnostics();
  if (d < 0)
    return QStringLiteral("all_logged_frames");
  if (d == 0)
    return QStringLiteral("off");
  return QStringLiteral("sample_every_%1").arg(d);
}

/** SceneGraph 纹理诊断加量：与 CLIENT_VIDEO_FORENSICS 或显式 TEXTURE_DIAG 联用 */
inline bool videoSurfaceExtraDiagnostics() {
  if (videoForensicsPackEnabled())
    return true;
  const QByteArray raw = qgetenv("CLIENT_VIDEO_TEXTURE_DIAG");
  if (!raw.isEmpty()) {
    const QByteArray tl = raw.trimmed().toLower();
    if (tl == "0" || tl == "false" || tl == "off" || tl == "no")
      return false;
    return true;
  }
#ifndef NDEBUG
  return true;
#else
  return false;
#endif
}

/** IEEE 802.3 CRC32 over raw QImage buffer（含行填充），与 rowHash 互补。 */
inline quint32 crc32IeeeOverImageBytes(const QImage &img) {
  if (img.isNull() || !img.constBits())
    return 0u;
  const qsizetype len = img.sizeInBytes();
  if (len <= 0)
    return 0u;
  const uchar *data = img.constBits();
  quint32 crc = 0xFFFFFFFFu;
  for (qsizetype i = 0; i < len; ++i) {
    crc ^= static_cast<quint32>(data[i]);
    for (int k = 0; k < 8; ++k)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

inline void mixRowPrefixFnv(uint32_t &crc, const uchar *row, int bytesInRow, int maxPrefix = 64) {
  if (!row || bytesInRow <= 0)
    return;
  const int n = qMin(maxPrefix, bytesInRow);
  for (int i = 0; i < n; ++i) {
    crc ^= row[i];
    crc *= 16777619u;
  }
}

inline uint32_t rowHashSample(const QImage &img) {
  uint32_t crc = 2166136261u;
  if (img.isNull())
    return crc;
  const int w = img.width();
  const int h = img.height();
  const int bpl = img.bytesPerLine();
  const uchar *bits = img.constBits();
  if (!bits || w <= 0 || h <= 0 || bpl <= 0)
    return crc;

  int bpp = 0;
  switch (img.format()) {
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_RGBA8888:
      bpp = 4;
      break;
    case QImage::Format_RGB888:
      bpp = 3;
      break;
    default:
      bpp = 1;
      break;
  }
  const int rowUse = qMin(bpl, qMax(1, w * bpp));

  if (!stripeRowsExtended()) {
    mixRowPrefixFnv(crc, bits, rowUse);
    if (h > 1)
      mixRowPrefixFnv(crc, bits + bpl, rowUse);
    return crc;
  }

  // 多行采样：条状/行错位在中间或底部时，仅采前两行易漏检
  mixRowPrefixFnv(crc, bits, rowUse);
  if (h > 1)
    mixRowPrefixFnv(crc, bits + bpl, rowUse);
  if (h > 2) {
    const int mid = h / 2;
    if (mid != 0 && mid != 1 && mid != h - 1)
      mixRowPrefixFnv(crc, bits + mid * bpl, rowUse);
  }
  if (h > 1) {
    const int last = h - 1;
    if (last != 0 && last != 1)
      mixRowPrefixFnv(crc, bits + last * bpl, rowUse);
  }
  return crc;
}

inline bool wantsFullImageCrcForFrame(quint64 frameId);

inline QString diagLine(const char *stage, const QString &streamOrSurfaceId, quint64 frameId,
                        const QImage &img, qreal brW = -1., qreal brH = -1.) {
  if (img.isNull()) {
    return QStringLiteral("[Client][VideoEvidence] stage=%1 id=%2 fid=%3 NULL br=%4x%5")
        .arg(QLatin1String(stage))
        .arg(streamOrSurfaceId)
        .arg(frameId)
        .arg(brW)
        .arg(brH);
  }

  const int w = img.width();
  const int h = img.height();
  const int fmt = static_cast<int>(img.format());
  const int bpl = img.bytesPerLine();
  const qint64 sz = static_cast<qint64>(img.sizeInBytes());

  int bpp = 0;
  switch (img.format()) {
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_RGBA8888:
      bpp = 4;
      break;
    case QImage::Format_RGB888:
      bpp = 3;
      break;
    default:
      bpp = 0;
      break;
  }
  const int minBpl = (bpp > 0) ? w * bpp : -1;
  const int rowPad = (bpp > 0) ? (bpl - w * bpp) : -9999;
  const uint32_t rh = rowHashSample(img);
  const int rowSampleMode = stripeRowsExtended() ? 1 : 0;

  QString fullCrcPart;
  if (wantsFullImageCrcForFrame(frameId)) {
    const quint32 fc = crc32IeeeOverImageBytes(img);
    fullCrcPart =
        QStringLiteral(" fullCrc=0x%1").arg(fc, 8, 16, QLatin1Char('0'));
  }

  QString br;
  if (brW >= 0. && brH >= 0.)
    br = QStringLiteral(" br=%1x%2").arg(brW).arg(brH);
  else
    br = QString();

  return QStringLiteral(
             "[Client][VideoEvidence] stage=%1 id=%2 fid=%3 w=%4 h=%5 fmt=%6 bpl=%7 minBpl=%8 "
             "rowPad=%9 bytes=%10 rowSample=%11 rowHash=0x%12%13%14")
      .arg(QLatin1String(stage))
      .arg(streamOrSurfaceId)
      .arg(frameId)
      .arg(w)
      .arg(h)
      .arg(fmt)
      .arg(bpl)
      .arg(minBpl)
      .arg(rowPad)
      .arg(sz)
      .arg(rowSampleMode)
      .arg(rh, 8, 16, QLatin1Char('0'))
      .arg(br)
      .arg(fullCrcPart);
}

inline bool shouldLogFrame(quint64 frameId) {
  const int earlyMax = evidenceEnvInt("CLIENT_VIDEO_EVIDENCE_EARLY_MAX", defaultEvidenceEarlyMax());
  const int interval = evidenceEnvInt("CLIENT_VIDEO_EVIDENCE_INTERVAL", 60);
  if (frameId <= static_cast<quint64>(earlyMax))
    return true;
  if (interval <= 0)
    return false;
  return (frameId % static_cast<quint64>(interval)) == 0u;
}

/** 环节级 pipeline trace（四路独立 frameId 采样） */
inline bool pipelineTraceEnabled() {
  bool ok = false;
  const int v = qEnvironmentVariableIntValue("CLIENT_VIDEO_PIPELINE_TRACE", &ok);
  return ok && v != 0;
}

inline bool shouldLogPipelineTrace(quint64 frameId) {
  if (!pipelineTraceEnabled())
    return false;
  const int early = evidenceEnvInt("CLIENT_VIDEO_PIPELINE_TRACE_EARLY", 48);
  const int interval = evidenceEnvInt("CLIENT_VIDEO_PIPELINE_TRACE_INTERVAL", 30);
  if (frameId <= static_cast<quint64>(early))
    return true;
  if (interval <= 0)
    return false;
  return (frameId % static_cast<quint64>(interval)) == 0u;
}

/** 证据链像素行 或 pipeline 环节 或取证包：供各 stage 统一判断是否打详细行 */
inline bool shouldLogVideoStage(quint64 frameId) {
  if (videoForensicsPackEnabled())
    return shouldLogFrame(frameId);
  if (chainEnabled() && shouldLogFrame(frameId))
    return true;
  return shouldLogPipelineTrace(frameId);
}

/** 是否在证据链该帧上计算整图 CRC（供 DECODE_OUT 指纹与 VideoEvidence 行一致） */
inline bool wantsFullImageCrcForFrame(quint64 frameId) {
  const int period = fullCrcSampleEveryForDiagnostics();
  if (period < 0)
    return true;
  if (period == 0)
    return false;
  if (!shouldLogVideoStage(frameId))
    return false;
  const int earlyMax = evidenceEnvInt("CLIENT_VIDEO_EVIDENCE_EARLY_MAX", defaultEvidenceEarlyMax());
  if (frameId <= static_cast<quint64>(earlyMax))
    return true;
  return (frameId % static_cast<quint64>(period)) == 0u;
}

}  // namespace VideoFrameEvidence

#endif
