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
 * 条状误报 vs 真损坏（与 PNG 对齐）：
 *   1) 日志搜 [H264][STRIPE_VERDICT]：verdict=fp_top → 多为顶 16 行启发式；suspect → 中/底频段或 shift 命中。
 *   2) 设 CLIENT_VIDEO_STRIPE_ALERT_CAPTURE=1，解码告警帧写入 <SAVE_DIR>/stripe-alerts/star.png，文件名含 verdict 与
 *      sh/t/m/b；用肉眼对照：仅上部平、中下正常 → 误报；整幅错位/条带 → 真损坏。
 *   3) 可选 CLIENT_VIDEO_SAVE_FRAME=png 做常规落盘，与 stripe-alerts 互补。
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

/**
 * ★ 性能优化：将原本在主线程的 QImage 格式规范化（convertToFormat 等）卸载至后台线程。
 * 确保输出为 QImage::Format_RGBA8888 且 stride 对齐 (>=4*w)。
 */
inline bool normalizeImageForCpuTexture(QImage &img) {
  if (img.isNull())
    return false;
  const int w = img.width();
  if (w <= 0)
    return false;
  if (img.format() == QImage::Format_RGBA8888 && img.bytesPerLine() >= w * 4)
    return true;
  
  // 仅在格式或步长不符时触发，这通常是高开销操作，应避免在主线程执行
  img = img.convertToFormat(QImage::Format_RGBA8888);
  return !img.isNull() && img.bytesPerLine() >= w * 4;
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

/**
 * 实时条状检测器：采样首行与次行，对比是否存在微小水平位移（典型 stride 错误表现）
 * 返回值：0=未检测到；>0=检测到的偏移像素数；<0=检测过程中出错或置信度低
 */
inline int detectStripeHorizontalShift(const QImage &img) {
  if (img.isNull() || img.width() < 128 || img.height() < 2)
    return 0;
  if (img.format() != QImage::Format_RGBA8888 && img.format() != QImage::Format_RGB32)
    return 0;

  const int w = img.width();
  const int h = img.height();
  const uint32_t *line0 = reinterpret_cast<const uint32_t *>(img.constScanLine(0));
  const uint32_t *line1 = reinterpret_cast<const uint32_t *>(img.constScanLine(1));
  if (!line0 || !line1)
    return 0;

  // 典型 stride 错位表现为下一行相对于上一行偏移了若干像素（通常是 1 或 -1）
  // 我们在中间区域对比 line0[x] 与 line1[x+offset]
  auto calcPairDiff = [&](const uint32_t* rowA, const uint32_t* rowB, int offset) -> uint64_t {
    uint64_t diff = 0;
    const int startX = 32;
    const int endX = w - 32;
    int count = 0;
    for (int x = startX; x < endX; ++x) {
      if (x + offset < 0 || x + offset >= w) continue;
      uint32_t p0 = rowA[x];
      uint32_t p1 = rowB[x + offset];
      int r = std::abs((int)((p0 >> 16) & 0xFF) - (int)((p1 >> 16) & 0xFF));
      int g = std::abs((int)((p0 >> 8) & 0xFF) - (int)((p1 >> 8) & 0xFF));
      int b = std::abs((int)(p0 & 0xFF) - (int)(p1 & 0xFF));
      diff += (r + g + b);
      count++;
    }
    return count > 0 ? diff / count : 0xFFFFFFFFu;
  };

  // 采样多对相邻行，提高检测置信度
  const int pairs[] = {0, 1, h/4, h/4+1, h/2, h/2+1, 3*h/4, 3*h/4+1};
  int totalShift = 0;
  int detectedCount = 0;

  for (int i = 0; i < 8; i += 2) {
    const uint32_t *lA = reinterpret_cast<const uint32_t *>(img.constScanLine(pairs[i]));
    const uint32_t *lB = reinterpret_cast<const uint32_t *>(img.constScanLine(pairs[i+1]));
    
    uint64_t d0 = calcPairDiff(lA, lB, 0);
    uint64_t dL = calcPairDiff(lA, lB, -1);
    uint64_t dR = calcPairDiff(lA, lB, 1);

    if (d0 > 15 && (dL < d0 / 2 || dR < d0 / 2)) {
      totalShift += (dL < dR) ? -1 : 1;
      detectedCount++;
    }
  }

  if (detectedCount >= 2) {
    return (totalShift > 0) ? 1 : -1;
  }
  return 0;
}

/**
 * 细粒度条状检测：计算多行的 FNV 哈希并对比。
 * 如果出现规律性的哈希重复（如每 2 行哈希相同）或哈希全同（如 sws 未填满），则返回非 0。
 */
inline int detectStripeFineGrained(const QImage &img) {
  if (img.isNull() || img.height() < 16) return 0;
  
  const int bpl = img.bytesPerLine();
  const uchar* bits = img.constBits();
  if (!bits || bpl <= 0) return 0;
  
  // 采样前 16 行
  uint32_t hashes[16];
  for (int i = 0; i < 16; ++i) {
    uint32_t hval = 2166136261u;
    mixRowPrefixFnv(hval, bits + i * bpl, qMin(bpl, 256)); // 采每行前 256 字节
    hashes[i] = hval;
  }
  
  // 检查是否所有行都相同（sws 挂了或内存全 0/全同）
  bool allSame = true;
  for (int i = 1; i < 16; ++i) {
    if (hashes[i] != hashes[0]) {
      allSame = false;
      break;
    }
  }
  if (allSame) return 100; // 特殊值：全同行
  
  // 检查奇偶行相同（典型 2-row stride 错误或 interlacing 处理不当）
  bool alternatingSame = true;
  for (int i = 0; i < 14; i += 2) {
    if (hashes[i] != hashes[i+1]) {
      alternatingSame = false;
      break;
    }
  }
  if (alternatingSame) return 200; // 特殊值：奇偶行相同
  
  return 0;
}

/**
 * 与 detectStripeFineGrained 相同算法，但从 startRow 起连续最多 16 行采样（用于与顶部频段对照，
 * 区分「天空/纯色顶边」误报与整幅损坏）。
 * 若 startRow < 0 或剩余行数 < 16 则返回 0（不判）。
 */
inline int detectStripeFineGrainedFromRow(const QImage &img, int startRow) {
  if (img.isNull() || img.height() < 16)
    return 0;
  const int h = img.height();
  if (startRow < 0 || startRow > h - 16)
    return 0;
  const int bpl = img.bytesPerLine();
  const uchar *bits = img.constBits();
  if (!bits || bpl <= 0)
    return 0;

  uint32_t hashes[16];
  for (int i = 0; i < 16; ++i) {
    uint32_t hval = 2166136261u;
    mixRowPrefixFnv(hval, bits + (startRow + i) * bpl, qMin(bpl, 256));
    hashes[i] = hval;
  }
  bool allSame = true;
  for (int i = 1; i < 16; ++i) {
    if (hashes[i] != hashes[0]) {
      allSame = false;
      break;
    }
  }
  if (allSame)
    return 100;
  bool alternatingSame = true;
  for (int i = 0; i < 14; i += 2) {
    if (hashes[i] != hashes[i + 1]) {
      alternatingSame = false;
      break;
    }
  }
  if (alternatingSame)
    return 200;
  return 0;
}

/** 条状启发式裁决：结合顶/中/底三频段与水平位移，区分误报与疑真损坏 */
enum class StripeHeuristicVerdict : quint8 { Clean = 0, FpTopBand = 1, SuspectCorrupt = 2 };

struct StripeHeuristicReport {
  StripeHeuristicVerdict verdict = StripeHeuristicVerdict::Clean;
  int horizontalShift = 0;
  int fineTop = 0;
  int fineMid = 0;
  int fineBot = 0;
  int midRow = 0;
  int botRow = 0;
};

inline StripeHeuristicReport analyzeStripeHeuristic(const QImage &img) {
  StripeHeuristicReport r;
  r.horizontalShift = detectStripeHorizontalShift(img);
  r.fineTop = detectStripeFineGrained(img);
  const int h = img.height();
  r.midRow = (h >= 16) ? (h / 2) : 0;
  r.botRow = (h >= 16) ? (h - 16) : 0;
  r.fineMid = (h >= 16) ? detectStripeFineGrainedFromRow(img, r.midRow) : 0;
  r.fineBot = (h >= 16) ? detectStripeFineGrainedFromRow(img, r.botRow) : 0;

  if (r.horizontalShift == 0 && r.fineTop == 0 && r.fineMid == 0 && r.fineBot == 0)
    r.verdict = StripeHeuristicVerdict::Clean;
  else if (r.horizontalShift != 0 || r.fineMid != 0 || r.fineBot != 0)
    r.verdict = StripeHeuristicVerdict::SuspectCorrupt;
  else
    r.verdict = StripeHeuristicVerdict::FpTopBand;
  return r;
}

inline QString stripeVerdictTag(StripeHeuristicVerdict v) {
  switch (v) {
    case StripeHeuristicVerdict::Clean:
      return QStringLiteral("clean");
    case StripeHeuristicVerdict::FpTopBand:
      return QStringLiteral("fp_top");
    case StripeHeuristicVerdict::SuspectCorrupt:
      return QStringLiteral("suspect");
  }
  return QStringLiteral("?");
}

inline QString stripeVerdictHintZh(StripeHeuristicVerdict v) {
  switch (v) {
    case StripeHeuristicVerdict::Clean:
      return QStringLiteral("无启发式命中");
    case StripeHeuristicVerdict::FpTopBand:
      return QStringLiteral(
          "多为顶边天空/纯色误报；对照 stripe-alerts 下 PNG 若仅上部平、下部正常即可确认");
    case StripeHeuristicVerdict::SuspectCorrupt:
      return QStringLiteral("疑 stride/解码或整幅异常；stripe-alerts PNG 应全帧检查");
  }
  return {};
}

}  // namespace VideoFrameEvidence

#endif
