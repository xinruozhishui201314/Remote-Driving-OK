#include "RemoteVideoSurface.h"

#include "Nv12DmaBufQsg.h"

#include "core/eventbus.h"
#include "core/metricscollector.h"
#include "media/ClientVideoDiagCache.h"
#include "media/ClientVideoStreamHealth.h"
#include "media/VideoFrameEvidenceDiag.h"
#include "media/VideoFrameFingerprintCache.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QStringList>

#include <atomic>
#include <exception>

namespace {
// 与解码侧常见上限对齐；超大图避免驱动/createTexture 异常或 OOM
constexpr int kMaxTextureEdgePx = 8192;

/** 配置/上下文类错误：立即切 CPU，不等待连续失败 */
bool dmaBufSgFailureIsPermanent(const QString& r) {
  static const QStringList kPerm = {
      QStringLiteral("no_qsb"),
      QStringLiteral("no_matching_gl_context"),
      QStringLiteral("egl_gl_proc_missing"),
      QStringLiteral("egl_no_current_display"),
      QStringLiteral("bad_args"),
      QStringLiteral("not_dma_or_planes"),
  };
  return kPerm.contains(r);
}

/** 有上一帧可显示时，累计多少次导入失败后再切 CPU（默认 5，范围 1..120） */
int dmaBufSgStreakBeforeCpuFallback() {
  bool envOk = false;
  const int v = qEnvironmentVariableIntValue("CLIENT_DMABUF_SG_FAIL_STREAK_BEFORE_CPU", &envOk);
  if (!envOk || v <= 0)
    return 5;
  return qBound(1, v, 120);
}

/**
 * Debug 构建默认详细记录 createTextureFromImage 失败；Release 需 CLIENT_VIDEO_TEXTURE_DIAG=1。
 * 显式 CLIENT_VIDEO_TEXTURE_DIAG=0|false|off 关闭详细日志（仍可有少量短日志见下方 atomic 上限）。
 */
bool textureCreateDiagVerbose() {
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

void logCreateTextureFromImageFailure(const char *ctx, const QImage &img, const QQuickWindow *win,
                                      const char *exceptionWhat = nullptr) {
  if (textureCreateDiagVerbose() || VideoFrameEvidence::videoForensicsPackEnabled()) {
    static std::atomic<int> s_verboseLogs{0};
    if (s_verboseLogs.fetch_add(1, std::memory_order_relaxed) >= 32)
      return;
    QStringList p;
    p << QStringLiteral("ctx=%1").arg(QLatin1String(ctx));
    p << QStringLiteral("size=%1x%2").arg(img.width()).arg(img.height());
    p << QStringLiteral("fmt=%1").arg(static_cast<int>(img.format()));
    p << QStringLiteral("bpl=%1").arg(img.bytesPerLine());
    p << QStringLiteral("depth=%1").arg(img.depth());
    p << QStringLiteral("bytes=%1").arg(img.sizeInBytes());
    p << QStringLiteral("bitsOk=%1").arg(img.constBits() != nullptr);
    if (win)
      p << QStringLiteral("QQuickWindow::graphicsApi=%1").arg(static_cast<int>(win->graphicsApi()));
    p << QStringLiteral("QSG_RHI_BACKEND=%1")
             .arg(QString::fromLocal8Bit(qgetenv("QSG_RHI_BACKEND")));
    p << QStringLiteral("LIBGL_ALWAYS_SOFTWARE=%1")
             .arg(QString::fromLocal8Bit(qgetenv("LIBGL_ALWAYS_SOFTWARE")));
    p << QStringLiteral("CLIENT_ASSUME_SOFTWARE_GL=%1")
             .arg(QString::fromLocal8Bit(qgetenv("CLIENT_ASSUME_SOFTWARE_GL")));
    if (exceptionWhat)
      p << QStringLiteral("exception=%1").arg(QLatin1String(exceptionWhat));
    qWarning().noquote() << "[Client][UI][RemoteVideoSurface][TextureDiag]"
                         << p.join(QLatin1Char(' '));
    return;
  }
  static std::atomic<int> s_shortLogs{0};
  if (s_shortLogs.fetch_add(1, std::memory_order_relaxed) >= 12)
    return;
  qWarning() << "[Client][UI][RemoteVideoSurface] createTextureFromImage" << ctx
             << "img=" << img.size() << " fmt=" << img.format() << " bpl=" << img.bytesPerLine()
             << (win ? QStringLiteral(" graphicsApi=%1").arg(static_cast<int>(win->graphicsApi()))
                     : QString());
}

bool imageUsableForTexture(const QImage &img) {
  if (img.isNull())
    return false;
  const int w = img.width();
  const int h = img.height();
  if (w <= 0 || h <= 0)
    return false;
  if (w > kMaxTextureEdgePx || h > kMaxTextureEdgePx)
    return false;
  return true;
}

bool presentIntegrityCheckEnabled() {
  const QByteArray v = qgetenv("CLIENT_VIDEO_PRESENT_INTEGRITY_CHECK");
  if (v.isEmpty())
    return true;
  const QByteArray tl = v.trimmed().toLower();
  if (tl == "0" || tl == "false" || tl == "off" || tl == "no")
    return false;
  return true;
}

/** CLIENT_VIDEO_FORENSICS / TEXTURE_DIAG / Debug：提高 [SG]/[TexSize] 打印量 */
bool remoteSurfaceExpandedUploadLog() {
  return VideoFrameEvidence::videoSurfaceExtraDiagnostics();
}

QString metricCodeSuffix(const QString &code) {
  QString s;
  s.reserve(code.size());
  for (QChar c : code) {
    const bool alnum = (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
                       (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                       (c >= QLatin1Char('0') && c <= QLatin1Char('9'));
    s += alnum ? c : QLatin1Char('_');
  }
  return s.isEmpty() ? QStringLiteral("UNKNOWN") : s.mid(0, 96);
}

bool allowPresentIntegrityAlert(quintptr surfaceKey, const QString &code, int minGapMs = 4500) {
  static QMutex mtx;
  static QHash<QString, qint64> lastEmitMs;
  QMutexLocker lock(&mtx);
  const QString k =
      QStringLiteral("%1|%2").arg(QString::number(surfaceKey, 16), code);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 prev = lastEmitMs.value(k, 0);
  if (now - prev < minGapMs)
    return false;
  lastEmitMs.insert(k, now);
  return true;
}

void publishPresentIntegrityIfAllowed(quintptr surfaceKey, const QString &streamTag,
                                    const QString &surfaceId, const QString &code,
                                    const QString &detail, bool suspectGpu) {
  if (!allowPresentIntegrityAlert(surfaceKey, code))
    return;
  VideoPresentIntegrityEvent ev;
  ev.stream = streamTag;
  ev.surfaceId = surfaceId;
  ev.code = code;
  ev.detail = detail;
  ev.suspectGpuCompositor = suspectGpu;
  EventBus::instance().publish(ev);
  MetricsCollector::instance().increment(
      QStringLiteral("client.video.present_integrity_alert_total"));
  MetricsCollector::instance().increment(
      QStringLiteral("client.video.present_integrity_alert_by_code_%1").arg(metricCodeSuffix(code)));
  qWarning().noquote() << "[Client][UI][RemoteVideoSurface][PresentIntegrity] code=" << code
                       << " stream=" << streamTag << " surface=" << surfaceId << " gpuSuspect=" << suspectGpu
                       << " detail=" << detail;
}

/**
 * CPU QImage→QSG 纹理契约：与 H264Decoder 软解 sws→RGBA8888 + Qt RHI GL_RGBA 对齐；
 * 默认拒绝其它 QImage::Format，避免历史 BGR32/GL_BGRA 在软件 GL 上的 stride 条带问题。
 */
bool normalizeImageForCpuTexture(QImage &img, const QString &streamTag, quint64 frameId,
                                 const char *ctxTag) {
  if (img.format() == QImage::Format_RGBA8888) {
    const int w = img.width();
    if (w > 0 && img.bytesPerLine() < w * 4) {
      static std::atomic<int> s_strideReject{0};
      const int n = s_strideReject.fetch_add(1, std::memory_order_relaxed);
      if (n < 24) {
        qCritical().noquote()
            << "[Client][UI][RemoteVideoSurface][PresentContract][STRIDE_REJECT] ctx="
            << QLatin1String(ctxTag) << " stream=" << streamTag << " frameId=" << frameId
            << " bpl=" << img.bytesPerLine() << " w=" << w << " needBpl>=" << (w * 4)
            << " " << ClientVideoDiagCache::videoPipelineEnvFingerprint();
      }
      MetricsCollector::instance().increment(
          QStringLiteral("client.video.cpu_present_stride_reject_total"));
      return false;
    }
    return true;
  }

  const bool strict = ClientVideoStreamHealth::cpuPresentFormatStrict();
  if (strict) {
    static std::atomic<int> s_rejectLogs{0};
    const int n = s_rejectLogs.fetch_add(1, std::memory_order_relaxed);
    if (n < 48) {
      qCritical().noquote()
          << "[Client][UI][RemoteVideoSurface][PresentContract][REJECT] ctx=" << QLatin1String(ctxTag)
          << " stream=" << streamTag << " frameId=" << frameId
          << " qimgFmt=" << static_cast<int>(img.format()) << " depth=" << img.depth()
          << " bpl=" << img.bytesPerLine() << " size=" << img.size()
          << " strictCpuFmt=1 ref=QImage::Format_RGBA8888+QQuickWindow::createTextureFromImage"
          << " " << ClientVideoDiagCache::videoPipelineEnvFingerprint()
          << " ★临时放行：CLIENT_VIDEO_CPU_PRESENT_FORMAT_STRICT=0（自动 convert，可能掩盖上游 bug）";
    }
    MetricsCollector::instance().increment(
        QStringLiteral("client.video.cpu_present_format_reject_total"));
    return false;
  }

  QImage c = img.convertToFormat(QImage::Format_RGBA8888);
  if (c.isNull()) {
    static std::atomic<int> s_fail{0};
    if (s_fail.fetch_add(1, std::memory_order_relaxed) < 12) {
      qCritical() << "[Client][UI][RemoteVideoSurface][PresentContract][CONVERT_FAIL] ctx="
                  << ctxTag << " stream=" << streamTag << " fromFmt=" << static_cast<int>(img.format());
    }
    return false;
  }
  static std::atomic<int> s_convLogged{0};
  if (s_convLogged.fetch_add(1, std::memory_order_relaxed) < 8) {
    qWarning().noquote()
        << "[Client][UI][RemoteVideoSurface][PresentContract][CONVERT] ctx=" << QLatin1String(ctxTag)
        << " stream=" << streamTag << " fromFmt=" << static_cast<int>(img.format())
        << " → Format_RGBA8888 ★ 上游应改为仅输出 RGBA8888";
  }
  img = std::move(c);
  return true;
}

/**
 * CLIENT_VIDEO_DEBUG_PNG_DIR：非空则限频落盘 PNG（主线程 commitCpuTextureFrame，避免在 SG 渲染线程写盘）。
 * CLIENT_VIDEO_DEBUG_PNG_INTERVAL_MS：默认 30000；同 surface+stream 独立节流。
 * CLIENT_VIDEO_DEBUG_GEOM=1：除首帧 GeomOnce 外，每路每 60s 再打一条几何+DPR（仍主线程）。
 */
QString sanitizeVideoDebugToken(const QString &s) {
  QString r = s;
  r.replace(QLatin1Char('/'), QLatin1Char('_'));
  r.replace(QLatin1Char('\\'), QLatin1Char('_'));
  r.replace(QLatin1Char(':'), QLatin1Char('_'));
  r.replace(QLatin1Char(' '), QLatin1Char('_'));
  if (r.isEmpty())
    return QStringLiteral("x");
  return r.mid(0, 120);
}

bool takeClientVideoDebugPngSlot(quintptr surfaceKey, const QString &streamTag, int intervalMs) {
  static QMutex mtx;
  static QHash<QString, qint64> lastMs;
  QMutexLocker lk(&mtx);
  const QString key = QStringLiteral("%1|%2")
                          .arg(QString::number(surfaceKey, 16),
                               streamTag.isEmpty() ? QStringLiteral("-") : streamTag);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - lastMs.value(key, 0) < intervalMs)
    return false;
  lastMs.insert(key, now);
  return true;
}

void writeClientVideoDebugPng(const QImage &img, const QString &streamTag, const QString &panelLabel,
                             quint64 frameId, quintptr surfaceKey, const QString &dir) {
  if (dir.isEmpty() || img.isNull())
    return;
  QDir d(dir);
  if (!d.exists() && !QDir().mkpath(dir)) {
    static std::atomic<int> s_mkfail{0};
    if (s_mkfail.fetch_add(1, std::memory_order_relaxed) < 6)
      qWarning() << "[Client][UI][VideoDebugPng] mkdir failed dir=" << dir;
    return;
  }
  const QString safeStream = sanitizeVideoDebugToken(streamTag.isEmpty() ? QStringLiteral("unknown") : streamTag);
  const QString safePanel = sanitizeVideoDebugToken(panelLabel.isEmpty() ? QStringLiteral("_") : panelLabel);
  const QString fn = QStringLiteral("%1/%2_%3_f%4_surf%5.png")
                         .arg(dir, safeStream, safePanel, QString::number(frameId),
                              QString::number(surfaceKey, 16));
  if (!img.save(fn)) {
    static std::atomic<int> s_saveFail{0};
    if (s_saveFail.fetch_add(1, std::memory_order_relaxed) < 12)
      qWarning() << "[Client][UI][VideoDebugPng] save failed path=" << fn;
    return;
  }
  qInfo().noquote() << "[Client][UI][VideoDebugPng] wrote" << fn << " wxh=" << img.width() << "x" << img.height()
                    << " bpl=" << img.bytesPerLine();
}

bool takeClientVideoGeomPeriodicSlot(quintptr surfaceKey, int intervalMs) {
  static QMutex mtx;
  static QHash<quintptr, qint64> lastMs;
  QMutexLocker lk(&mtx);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - lastMs.value(surfaceKey, 0) < intervalMs)
    return false;
  lastMs.insert(surfaceKey, now);
  return true;
}
}  // namespace

void RemoteVideoSurface::commitCpuTextureFrame(QImage &&image, quint64 frameId,
                                               const QString &streamTag) {
  try {
    {
      QMutexLocker lock(&m_mutex);
      if (!streamTag.isEmpty())
        m_streamTag = streamTag;
      m_presentMode = RemotePresentMode::CpuTexture;
      m_dmaDisplay.reset();
      m_frame = std::move(image);
      m_lastFrameId = frameId;
      m_hasPendingFrame = true;
      m_lastSize = m_frame.size();
      if (VideoFrameEvidence::shouldLogVideoStage(frameId)) {
        const QString sid = QStringLiteral("surf=0x%1").arg(quintptr(this), 0, 16);
        qInfo().noquote() << VideoFrameEvidence::diagLine("RS_APPLY", sid, frameId, m_frame);
      }
    }
    emit frameGeometryChanged();
    update();

    // ── 主线程：面板/流/DPR/几何/限频 PNG（与 Qt Quick 官方「纹理在 updatePaintNode 创建」解耦） ──
    const quintptr surfKey = reinterpret_cast<quintptr>(this);
    qreal dpr = 1.0;
    if (QQuickWindow *w = window())
      dpr = w->effectiveDevicePixelRatio();
    QString st;
    QString panel;
    {
      QMutexLocker lock(&m_mutex);
      st = m_streamTag;
      panel = m_panelLabel;
    }
    if (m_lastCommittedDpr < 0.0 || qAbs(dpr - m_lastCommittedDpr) > 0.0001) {
      m_lastCommittedDpr = dpr;
      qInfo().noquote()
          << QStringLiteral(
                 "[Client][UI][RemoteVideoSurface][DPR] surf=0x%1 panel=\"%2\" stream=%3 dpr=%4 "
                 "itemLogical=%5x%6 ★ Qt devicePixelRatio，条带/缩放对照 doc.qt.io/qt-6/highdpi.html")
              .arg(surfKey, 0, 16)
              .arg(panel)
              .arg(st)
              .arg(dpr, 0, 'f', 3)
              .arg(width())
              .arg(height());
    }
    if (!m_itemGeomLoggedOnce.exchange(true)) {
      const QRectF br = boundingRect();
      qInfo().noquote()
          << QStringLiteral("[Client][UI][RemoteVideoSurface][GeomOnce] surf=0x%1 panel=\"%2\" stream=%3 "
                            "itemLogical=%4x%5 boundingRect=%6x%7 dpr=%8 fillMode=%9")
              .arg(surfKey, 0, 16)
              .arg(panel)
              .arg(st)
              .arg(width())
              .arg(height())
              .arg(br.width(), 0, 'f', 1)
              .arg(br.height(), 0, 'f', 1)
              .arg(dpr, 0, 'f', 3)
              .arg(m_fillMode);
    } else if (qEnvironmentVariableIntValue("CLIENT_VIDEO_DEBUG_GEOM") != 0 &&
               takeClientVideoGeomPeriodicSlot(surfKey, 60000)) {
      const QRectF br = boundingRect();
      qInfo().noquote()
          << QStringLiteral("[Client][UI][RemoteVideoSurface][GeomPeriodic] surf=0x%1 panel=\"%2\" stream=%3 "
                            "itemLogical=%4x%5 boundingRect=%6x%7 dpr=%8 fillMode=%9")
              .arg(surfKey, 0, 16)
              .arg(panel)
              .arg(st)
              .arg(width())
              .arg(height())
              .arg(br.width(), 0, 'f', 1)
              .arg(br.height(), 0, 'f', 1)
              .arg(dpr, 0, 'f', 3)
              .arg(m_fillMode);
    }

    const QString pngDir = QString::fromUtf8(qgetenv("CLIENT_VIDEO_DEBUG_PNG_DIR")).trimmed();
    if (!pngDir.isEmpty()) {
      bool ivOk = false;
      int intervalMs = qEnvironmentVariableIntValue("CLIENT_VIDEO_DEBUG_PNG_INTERVAL_MS", &ivOk);
      if (!ivOk || intervalMs <= 0)
        intervalMs = 30000;
      if (takeClientVideoDebugPngSlot(surfKey, st, intervalMs)) {
        QImage pngCopy;
        {
          QMutexLocker lock(&m_mutex);
          pngCopy = m_frame.copy();
        }
        writeClientVideoDebugPng(pngCopy, st, panel, frameId, surfKey, pngDir);
      }
    }
  } catch (const std::exception &e) {
    qCritical() << "[Client][UI][RemoteVideoSurface] commitCpuTextureFrame std::exception frameId="
                << frameId << " what=" << e.what();
  } catch (...) {
    qCritical() << "[Client][UI][RemoteVideoSurface] commitCpuTextureFrame unknown exception frameId="
                << frameId;
  }
}

RemoteVideoSurface::RemoteVideoSurface(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setAntialiasing(false);
}

void RemoteVideoSurface::setFillMode(int mode) {
  if (m_fillMode == mode)
    return;
  m_fillMode = mode;
  emit fillModeChanged();
  update();
}

void RemoteVideoSurface::setPanelLabel(const QString &label) {
  if (m_panelLabel == label)
    return;
  m_panelLabel = label;
  emit panelLabelChanged();
}

void RemoteVideoSurface::applyDmaBufFrame(SharedDmaBufFrame handle, quint64 frameId,
                                          const QString &streamTag) {
  if (!handle || handle->frame.memoryType != VideoFrame::MemoryType::DMA_BUF) {
    static std::atomic<int> s_bad{0};
    if (s_bad.fetch_add(1, std::memory_order_relaxed) < 6) {
      qWarning() << "[Client][UI][RemoteVideoSurface] applyDmaBufFrame: skip invalid handle/type "
                    "frameId="
                 << frameId;
    }
    return;
  }
  const int w = static_cast<int>(handle->frame.width);
  const int h = static_cast<int>(handle->frame.height);
  if (w <= 0 || h <= 0 || w > kMaxTextureEdgePx || h > kMaxTextureEdgePx) {
    static std::atomic<int> s_geom{0};
    if (s_geom.fetch_add(1, std::memory_order_relaxed) < 6) {
      qWarning() << "[Client][UI][RemoteVideoSurface] applyDmaBufFrame: bad size wxh=" << w << "x" << h;
    }
    return;
  }

  if (VideoFrameEvidence::shouldLogPipelineTrace(frameId)) {
    const auto &df = handle->frame.dmaBuf;
    qInfo().noquote()
        << QStringLiteral(
               "[Client][VideoE2E][RS][applyDmaBuf] stream=%1 fid=%2 wxh=%3x%4 fourcc=0x%5 "
               "planes=%6 pitchY=%7 pitchUV=%8 offY=%9 offUV=%10 fd0=%11 fd1=%12 ★ 提交给 SG 前的 DMA 布局")
            .arg(streamTag.isEmpty() ? QStringLiteral("-") : streamTag)
            .arg(frameId)
            .arg(w)
            .arg(h)
            .arg(handle->frame.dmaBuf.drmFourcc, 0, 16)
            .arg(df.nbPlanes)
            .arg(df.pitches[0])
            .arg(df.pitches[1])
            .arg(df.offsets[0])
            .arg(df.offsets[1])
            .arg(df.fds[0])
            .arg(df.fds[1]);
  }

  try {
    {
      QMutexLocker lock(&m_mutex);
      if (!streamTag.isEmpty())
        m_streamTag = streamTag;
      m_dmaBufFallbackEmitted.store(false, std::memory_order_release);
      m_dmaSgFailStreak.store(0, std::memory_order_release);
      m_presentMode = RemotePresentMode::Nv12DmaBuf;
      m_dmaDisplay = std::move(handle);
      m_frame = QImage();
      m_hasPendingFrame = false;
      m_lastFrameId = frameId;
      m_lastSize = QSize(w, h);
    }
    emit frameGeometryChanged();
    update();
  } catch (const std::exception &e) {
    qCritical() << "[Client][UI][RemoteVideoSurface] applyDmaBufFrame std::exception frameId="
                << frameId << " what=" << e.what();
  } catch (...) {
    qCritical() << "[Client][UI][RemoteVideoSurface] applyDmaBufFrame unknown exception frameId="
                << frameId;
  }
}

void RemoteVideoSurface::applyFrame(QImage image, quint64 frameId, const QString &streamTag) {
  if (!imageUsableForTexture(image)) {
    static std::atomic<int> s_badGeomLogs{0};
    if (s_badGeomLogs.fetch_add(1, std::memory_order_relaxed) < 8) {
      qWarning() << "[Client][UI][RemoteVideoSurface] applyFrame: skip invalid/oversized image"
                 << " size=" << image.size() << " frameId=" << frameId;
    }
    return;
  }

  const QString st = streamTag.isEmpty() ? QStringLiteral("-") : streamTag;
  if (!normalizeImageForCpuTexture(image, st, frameId, "applyFrame"))
    return;

  if (auto cpu = CpuVideoRgba8888Frame::tryAdopt(std::move(image))) {
    commitCpuTextureFrame(std::move(cpu->image), frameId, streamTag);
    return;
  }
  static std::atomic<int> s_postNormFail{0};
  if (s_postNormFail.fetch_add(1, std::memory_order_relaxed) < 12) {
    qCritical() << "[Client][UI][RemoteVideoSurface][PresentContract] applyFrame: post-normalize "
                     "tryAdopt failed (stride/format) frameId="
                  << frameId << " stream=" << st;
  }
}

void RemoteVideoSurface::applyCpuRgba8888Frame(CpuVideoRgba8888Frame cpuFrame, quint64 frameId,
                                               const QString &streamTag) {
  QImage &im = cpuFrame.image;
  if (!imageUsableForTexture(im)) {
    static std::atomic<int> s_bad{0};
    if (s_bad.fetch_add(1, std::memory_order_relaxed) < 8) {
      qWarning() << "[Client][UI][RemoteVideoSurface] applyCpuRgba8888Frame: bad geometry frameId="
                 << frameId << " size=" << im.size();
    }
    return;
  }
  if (im.format() != QImage::Format_RGBA8888 || im.bytesPerLine() < im.width() * 4) {
    static std::atomic<int> s_contract{0};
    if (s_contract.fetch_add(1, std::memory_order_relaxed) < 16) {
      qCritical() << "[Client][UI][RemoteVideoSurface][PresentContract] applyCpuRgba8888Frame: "
                      "invalid contract frameId="
                   << frameId << " fmt=" << static_cast<int>(im.format()) << " bpl=" << im.bytesPerLine()
                   << " w=" << im.width();
    }
    MetricsCollector::instance().increment(
        QStringLiteral("client.video.cpu_rgba8888_surface_contract_reject_total"));
    return;
  }
  commitCpuTextureFrame(std::move(im), frameId, streamTag);
}

QRectF RemoteVideoSurface::mapSourceSizeToRect(const QSize &sourceSize) const {
  const QRectF br = boundingRect();
  if (!br.isValid() || br.width() <= 0 || br.height() <= 0)
    return br;

  const qreal iw = static_cast<qreal>(sourceSize.width());
  const qreal ih = static_cast<qreal>(sourceSize.height());
  if (iw <= 0 || ih <= 0)
    return br;

  if (m_fillMode == 0) {
    return br;
  }
  const qreal sx = br.width() / iw;
  const qreal sy = br.height() / ih;
  const qreal scale = qMax(sx, sy);
  const qreal dw = iw * scale;
  const qreal dh = ih * scale;
  const qreal x = br.x() + (br.width() - dw) * 0.5;
  const qreal y = br.y() + (br.height() - dh) * 0.5;
  return QRectF(x, y, dw, dh);
}

QRectF RemoteVideoSurface::mapImageToRect(const QImage &img) const {
  return mapSourceSizeToRect(img.size());
}

QSGNode *RemoteVideoSurface::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
  RemotePresentMode mode;
  SharedDmaBufFrame dmaSnap;
  QString streamSnap;
  {
    QMutexLocker lk(&m_mutex);
    mode = m_presentMode;
    dmaSnap = m_dmaDisplay;
    streamSnap = m_streamTag;
  }

  if (mode == RemotePresentMode::Nv12DmaBuf && dmaSnap) {
    QQuickWindow *win = window();
    if (!win) {
      delete oldNode;
      return nullptr;
    }
    static std::atomic<int> s_capOnce{0};
    if (s_capOnce.fetch_add(1, std::memory_order_relaxed) == 0)
      nv12DmaBufLogCapabilityOnce(win);
    const QSize srcSz(static_cast<int>(dmaSnap->frame.width), static_cast<int>(dmaSnap->frame.height));
    const QRectF mapRect = mapSourceSizeToRect(srcSz);
    bool ok = false;
    QString failReason;
    quint64 dmaFrameId = 0;
    {
      QMutexLocker lk(&m_mutex);
      dmaFrameId = m_lastFrameId;
    }
    QSGNode *n = nv12DmaBufBuildOrUpdateNode(win, oldNode, dmaSnap, mapRect, streamSnap,
                                             reinterpret_cast<quintptr>(this), &ok, &failReason,
                                             dmaFrameId);
    if (ok) {
      m_dmaSgFailStreak.store(0, std::memory_order_release);
      return n;
    }

    const QString r = failReason.isEmpty() ? QStringLiteral("dma_sg_unknown") : failReason;
    const int prevStreak = m_dmaSgFailStreak.fetch_add(1, std::memory_order_acq_rel);
    const int streak = prevStreak + 1;

    bool shouldCpuFallback = false;
    if (dmaBufSgFailureIsPermanent(r))
      shouldCpuFallback = true;
    else if (!n) {
      // 无上一帧可挂（黑屏）：允许 2 次连续失败后切 CPU，避免长时间无画面
      shouldCpuFallback = (streak >= 2);
    } else {
      shouldCpuFallback = (streak >= dmaBufSgStreakBeforeCpuFallback());
    }

    if (shouldCpuFallback) {
      {
        QMutexLocker lk(&m_mutex);
        m_presentMode = RemotePresentMode::CpuTexture;
        m_dmaDisplay.reset();
      }
      const bool already = m_dmaBufFallbackEmitted.exchange(true, std::memory_order_acq_rel);
      if (!already) {
        qWarning() << "[Client][UI][RemoteVideoSurface][DmaBufSG] CPU RGBA 回退 surf=0x" << Qt::hex
                   << reinterpret_cast<quintptr>(this) << Qt::dec << " err=" << r
                   << " permanent=" << dmaBufSgFailureIsPermanent(r) << " streak=" << streak
                   << " hadLastGoodNode=" << (n != nullptr)
                   << " ★ 已通知解码关闭 PRIME；请求关键帧以利画面稳定";
        emit dmaBufSceneGraphFailed(r);
      }
      update();
    }
    return n;
  }

  QSGSimpleTextureNode *node = dynamic_cast<QSGSimpleTextureNode *>(oldNode);
  // 本帧内若 new 了 node 却未交给 Qt（最终 return nullptr），必须由本函数 delete，否则泄漏。
  const bool createdHere = !oldNode || !node;
  if (oldNode && !node) {
    delete oldNode;
    oldNode = nullptr;
  }
  if (!node) {
    node = new QSGSimpleTextureNode();
    // ownsTexture=true：纹理生命周期仅由 setTexture 管理，禁止手动 delete node->texture()
    node->setOwnsTexture(true);
    node->setFiltering(QSGTexture::Linear);
  }

  // 无有效 QSGTexture 时不得把 QSGSimpleTextureNode 挂进场景图（Qt6 RHI 下 setTexture(nullptr)
  // 等会崩）。
  auto abandonWithoutTexture = [&]() -> QSGNode * {
    if (createdHere) {
      delete node;
      return nullptr;
    }
    return nullptr;  // Qt 删除并摘除 oldNode
  };

  // 已有纹理时仅改几何；否则释放节点（不调用 setTexture(nullptr)）。
  auto keepOrAbandonWithoutNewUpload = [&](const QRectF &rectIfKeep) -> QSGNode * {
    if (node->texture()) {
      node->setRect(rectIfKeep);
      return node;
    }
    return abandonWithoutTexture();
  };

  QImage img;
  bool hasNew = false;
  {
    QMutexLocker lock(&m_mutex);
    hasNew = m_hasPendingFrame;
    if (hasNew) {
      img = m_frame;
      m_hasPendingFrame = false;
    } else if (m_frame.isNull()) {
      return abandonWithoutTexture();
    } else if (node->texture()) {
      // 无新帧且已有纹理：仅更新几何，避免每帧重复 upload
      node->setRect(mapImageToRect(m_frame));
      return node;
    } else {
      img = m_frame;
    }
  }

  if (!imageUsableForTexture(img)) {
    return keepOrAbandonWithoutNewUpload(boundingRect());
  }

  {
    const QString st = streamSnap.isEmpty() ? QStringLiteral("-") : streamSnap;
    quint64 fid = 0;
    {
      QMutexLocker lk(&m_mutex);
      fid = m_lastFrameId;
    }
    if (!normalizeImageForCpuTexture(img, st, fid, "updatePaintNode")) {
      QMutexLocker lk(&m_mutex);
      m_frame = QImage();
      m_hasPendingFrame = false;
      return abandonWithoutTexture();
    }
  }

  // 仅在新纹理上传路径打 SG 证据（避免每帧仅改 setRect 时刷屏）
  const bool newBitmapUpload = hasNew || !node->texture();
  QQuickWindow *win = window();
  if (newBitmapUpload) {
    quint64 fid = 0;
    {
      QMutexLocker lock(&m_mutex);
      fid = m_lastFrameId;
    }
    if (VideoFrameEvidence::shouldLogVideoStage(fid)) {
      const QString sid = QStringLiteral("surf=0x%1").arg(quintptr(this), 0, 16);
      const QRectF br = boundingRect();
      qInfo().noquote() << VideoFrameEvidence::diagLine("SG_UPLOAD", sid, fid, img, br.width(),
                                                        br.height());
    }
  }

  // ★ 诊断日志：按 stream+frameId 采样（CLIENT_VIDEO_PIPELINE_TRACE）+ 全局前 N 次上传，
  // 避免四路共用一个计数导致「看不出哪一路」。
  if (newBitmapUpload) {
    quint64 fidLog = 0;
    {
      QMutexLocker lk(&m_mutex);
      fidLog = m_lastFrameId;
    }
    const QString st = streamSnap.isEmpty() ? QStringLiteral("-") : streamSnap;
    const bool traceSample = VideoFrameEvidence::shouldLogVideoStage(fidLog);
    static std::atomic<int> s_uploadLogN{0};
    const int logN = s_uploadLogN.fetch_add(1, std::memory_order_relaxed);
    const int kSgLogCap = remoteSurfaceExpandedUploadLog() ? 160 : 20;
    if (traceSample || logN < kSgLogCap || (logN % 300) == 0) {
      const QRectF br = boundingRect();
      const QRectF mapRect = mapImageToRect(img);
      QString pixStr;
      if (img.width() >= 4 && img.height() >= 4) {
        const uint8_t *rowTL = img.constScanLine(0);
        const uint8_t *rowCtr = img.constScanLine(img.height() / 2);
        const uint8_t *rowLast = img.constScanLine(img.height() - 1);
        const int cx = img.width() / 2;
        pixStr = QStringLiteral(
                     "px[0,0]=(R%1,G%2,B%3) px[mid]=(R%4,G%5,B%6) px[last]=(R%7,G%8,B%9)")
                     .arg(rowTL[0])
                     .arg(rowTL[1])
                     .arg(rowTL[2])
                     .arg(rowCtr[cx * 4])
                     .arg(rowCtr[cx * 4 + 1])
                     .arg(rowCtr[cx * 4 + 2])
                     .arg(rowLast[cx * 4])
                     .arg(rowLast[cx * 4 + 1])
                     .arg(rowLast[cx * 4 + 2]);
      }
      qInfo().noquote() << QStringLiteral(
                               "[Client][VideoE2E][RS][SG][CPU] stream=%1 fid=%2 uploadSeq=%3"
                               " surf=0x%4 img=%5x%6 fmt=%7 bpl=%8 minBpl=%9 rowPad=%10"
                               " br=(%11,%12 %13x%14) mapRect=(%15,%16 %17x%18) %19 %20"
                               " ★ bpl<4*w 或 rowPad 异常易致条状；四路对照同 fid 的 DECODE_OUT rowHash")
                               .arg(st)
                               .arg(fidLog)
                               .arg(logN)
                               .arg(quintptr(this), 0, 16)
                               .arg(img.width())
                               .arg(img.height())
                               .arg(static_cast<int>(img.format()))
                               .arg(img.bytesPerLine())
                               .arg(img.width() * 4)
                               .arg(img.bytesPerLine() - img.width() * 4)
                               .arg(br.x(), 0, 'f', 1)
                               .arg(br.y(), 0, 'f', 1)
                               .arg(br.width(), 0, 'f', 1)
                               .arg(br.height(), 0, 'f', 1)
                               .arg(mapRect.x(), 0, 'f', 1)
                               .arg(mapRect.y(), 0, 'f', 1)
                               .arg(mapRect.width(), 0, 'f', 1)
                               .arg(mapRect.height(), 0, 'f', 1)
                               .arg(ClientVideoDiagCache::renderStackSummaryLine(win))
                               .arg(pixStr);
    }
  }

  if (!win) {
    return keepOrAbandonWithoutNewUpload(boundingRect());
  }

  // Qt 文档：createTextureFromImage 须在渲染线程的 updatePaintNode 中调用（threaded render loop
  // 下为渲染线程）
  QSGTexture *tex = nullptr;
  try {
    tex = win->createTextureFromImage(img, QQuickWindow::TextureIsOpaque);
  } catch (const std::exception &e) {
    logCreateTextureFromImageFailure("std::exception", img, win, e.what());
    return keepOrAbandonWithoutNewUpload(mapImageToRect(img));
  } catch (...) {
    logCreateTextureFromImageFailure("unknown_exception", img, win, nullptr);
    return keepOrAbandonWithoutNewUpload(mapImageToRect(img));
  }
  if (!tex) {
    logCreateTextureFromImageFailure("returned_null", img, win, nullptr);
    // ★ 统计纹理失败次数：持续失败=GL环境问题；偶发失败=可接受
    static std::atomic<int> s_texFailCount{0};
    const int failN = s_texFailCount.fetch_add(1, std::memory_order_relaxed);
    if (failN < 10 || (failN % 100) == 0) {
      qWarning() << "[Client][UI][RemoteVideoSurface][TexFail] createTextureFromImage=null 累计"
                 << (failN + 1) << "次 imgFmt=" << static_cast<int>(img.format())
                 << " imgSize=" << img.size()
                 << " graphicsApi=" << (win ? static_cast<int>(win->graphicsApi()) : -1)
                 << " ★ 若持续失败：检查 QSG_RHI_BACKEND / LIBGL_ALWAYS_SOFTWARE / Mesa版本";
    }
    return keepOrAbandonWithoutNewUpload(mapImageToRect(img));
  }
  // ★ 纹理创建成功：核对 QSGTexture::textureSize 与 QImage 逻辑尺寸（Qt 文档：OpenGL 路径内部格式
  // GL_RGBA；尺寸不一致 → 采样/UV 异常风险）
  {
    static std::atomic<int> s_texOkCount{0};
    const int okN = s_texOkCount.fetch_add(1, std::memory_order_relaxed);
    const QSize texSz = tex->textureSize();
    const bool sizeMatch = (texSz == img.size());
    QString stTex;
    quint64 fidTex2 = 0;
    {
      QMutexLocker lock(&m_mutex);
      stTex = m_streamTag;
      fidTex2 = m_lastFrameId;
    }
    const int kTexLogCap = remoteSurfaceExpandedUploadLog() ? 120 : 5;
    const bool traceTex = VideoFrameEvidence::shouldLogVideoStage(fidTex2);
    if (okN < kTexLogCap || !sizeMatch || (okN % 300) == 0 || traceTex) {
      qInfo().noquote()
          << QStringLiteral("[Client][VideoE2E][RS][TexSize] stream=%1 fid=%2 texOk#%3 "
                            "img=%4x%5 textureSize=%6x%7 match=%8 fmt=%9 bpl=%10 %11 ★ "
                            "match=false→条带/拉伸查 RHI/驱动")
              .arg(stTex.isEmpty() ? QStringLiteral("-") : stTex)
              .arg(fidTex2)
              .arg(okN + 1)
              .arg(img.width())
              .arg(img.height())
              .arg(texSz.width())
              .arg(texSz.height())
              .arg(sizeMatch ? QStringLiteral("true") : QStringLiteral("false"))
              .arg(static_cast<int>(img.format()))
              .arg(img.bytesPerLine())
              .arg(ClientVideoDiagCache::renderStackSummaryLine(win));
    }
    if (presentIntegrityCheckEnabled() && newBitmapUpload && !sizeMatch) {
      QString st;
      quint64 fidTex = 0;
      {
        QMutexLocker lock(&m_mutex);
        st = m_streamTag;
        fidTex = m_lastFrameId;
      }
      const QString sid = QStringLiteral("0x%1").arg(quintptr(this), 0, 16);
      const QString det =
          QStringLiteral("img=%1x%2 texture=%3x%4 fid=%5 %6")
              .arg(img.width())
              .arg(img.height())
              .arg(texSz.width())
              .arg(texSz.height())
              .arg(fidTex)
              .arg(ClientVideoDiagCache::renderStackSummaryLine(win));
      publishPresentIntegrityIfAllowed(quintptr(this), st, sid,
                                       QStringLiteral("TEXTURE_SIZE_MISMATCH"), det, true);
    }
  }

  if (presentIntegrityCheckEnabled() && newBitmapUpload) {
    QString st;
    quint64 fid = 0;
    {
      QMutexLocker lock(&m_mutex);
      st = m_streamTag;
      fid = m_lastFrameId;
    }
    if (!st.isEmpty()) {
      VideoFrameFingerprintCache::Fingerprint fp{};
      if (VideoFrameFingerprintCache::instance().peek(st, fid, &fp)) {
        if (fp.width != img.width() || fp.height != img.height()) {
          const QString sid = QStringLiteral("0x%1").arg(quintptr(this), 0, 16);
          const QString det = QStringLiteral("decodeWH=%1x%2 sgWH=%3x%4 fid=%5")
                                  .arg(fp.width)
                                  .arg(fp.height)
                                  .arg(img.width())
                                  .arg(img.height())
                                  .arg(fid);
          publishPresentIntegrityIfAllowed(quintptr(this), st, sid,
                                           QStringLiteral("DECODE_TO_PRESENT_SIZE_MISMATCH"), det,
                                           false);
        } else {
          const quint32 sgRh = VideoFrameEvidence::rowHashSample(img);
          if (sgRh != fp.rowHash) {
            const QString sid = QStringLiteral("0x%1").arg(quintptr(this), 0, 16);
            const QString det =
                QStringLiteral("decodeRowHash=0x%1 sgRowHash=0x%2 fid=%3 w=%4 h=%5")
                    .arg(fp.rowHash, 8, 16, QLatin1Char('0'))
                    .arg(sgRh, 8, 16, QLatin1Char('0'))
                    .arg(fid)
                    .arg(img.width())
                    .arg(img.height());
            publishPresentIntegrityIfAllowed(quintptr(this), st, sid,
                                             QStringLiteral("DECODE_TO_SG_ROWHASH_MISMATCH"), det,
                                             false);
          }
          if (fp.fullCrc != 0u) {
            const quint32 sgCrc = VideoFrameEvidence::crc32IeeeOverImageBytes(img);
            if (sgCrc != fp.fullCrc) {
              const QString sid = QStringLiteral("0x%1").arg(quintptr(this), 0, 16);
              const QString det =
                  QStringLiteral("decodeFullCrc=0x%1 sgFullCrc=0x%2 fid=%3")
                      .arg(fp.fullCrc, 8, 16, QLatin1Char('0'))
                      .arg(sgCrc, 8, 16, QLatin1Char('0'))
                      .arg(fid);
              publishPresentIntegrityIfAllowed(quintptr(this), st, sid,
                                               QStringLiteral("DECODE_TO_SG_CRC_MISMATCH"), det,
                                               false);
            }
          }
        }
      }
    }
  }

  node->setTexture(tex);
  node->setRect(mapImageToRect(img));
  node->markDirty(QSGNode::DirtyMaterial | QSGNode::DirtyGeometry);

  return node;
}
