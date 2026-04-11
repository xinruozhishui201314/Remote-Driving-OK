#include "VideoFramePresentWorker.h"

#include "media/VideoFrameEvidenceDiag.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QtGlobal>

namespace {
bool videoPresentCoalesceEnabled() {
  const QByteArray v = qgetenv("CLIENT_VIDEO_PRESENT_COALESCE");
  if (v.isEmpty())
    return true;
  const QByteArray vl = v.trimmed().toLower();
  return vl != "0" && vl != "false" && vl != "off";
}

int videoMaxPresentFps() {
  bool ok = false;
  const int n = qEnvironmentVariableIntValue("CLIENT_VIDEO_MAX_PRESENT_FPS", &ok);
  if (!ok)
    return 30;
  return n;
}
}  // namespace

VideoFramePresentWorker::VideoFramePresentWorker(QObject *parent) : QObject(parent) {
  m_rateTimer = new QTimer(this);
  m_rateTimer->setSingleShot(true);
  connect(m_rateTimer, &QTimer::timeout, this, &VideoFramePresentWorker::flushCoalesced);
}

void VideoFramePresentWorker::resetState() {
  m_coalescedImage = QImage();
  m_coalescedFrameId = 0;
  m_coalescedEpoch.store(0, std::memory_order_release);
  m_flushQueued.store(false, std::memory_order_release);
  m_lastPresentWallMs = 0;
  if (m_rateTimer)
    m_rateTimer->stop();
}

void VideoFramePresentWorker::moveToApplicationThread() {
  QThread *app = QCoreApplication::instance()->thread();
  if (thread() != app)
    moveToThread(app);
}

void VideoFramePresentWorker::ingestDecoderFrame(QImage image, quint64 frameId) {
  emit frameIngressed();

  if (image.isNull()) {
    qCritical() << "[Client][PresentWorker] 空帧 stream=" << m_streamTag << " frameId=" << frameId;
    return;
  }

  if (!videoPresentCoalesceEnabled()) {
    emitToMainThread(image, frameId);
    return;
  }

  const bool wasCoalesced = !m_coalescedImage.isNull();
  m_coalescedImage = std::move(image);
  m_coalescedFrameId = frameId;
  m_coalescedEpoch.fetch_add(1, std::memory_order_relaxed);
  if (wasCoalesced)
    emit coalescedDropOccurred();

  queueFlushToSelf();
}

void VideoFramePresentWorker::queueFlushToSelf() {
  bool expected = false;
  if (m_flushQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    QMetaObject::invokeMethod(this, &VideoFramePresentWorker::flushCoalesced, Qt::QueuedConnection);
}

void VideoFramePresentWorker::flushCoalesced() {
  m_flushQueued.store(false, std::memory_order_release);

  if (m_coalescedImage.isNull())
    return;

  const int maxFps = videoMaxPresentFps();
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if (maxFps > 0 && m_lastPresentWallMs > 0) {
    const qint64 minIntervalMs = qMax(qint64(1), (1000 + maxFps / 2) / maxFps);
    const qint64 elapsed = nowMs - m_lastPresentWallMs;
    if (elapsed < minIntervalMs) {
      emit rateLimitSkipped();
      if (!m_rateTimer->isActive())
        m_rateTimer->start(int(minIntervalMs - elapsed));
      return;
    }
  }

  const quint64 epochBefore = m_coalescedEpoch.load(std::memory_order_acquire);
  const QImage img = m_coalescedImage;
  const quint64 fid = m_coalescedFrameId;
  emitToMainThread(img, fid);
  emit flushCoalescedTick();
  m_lastPresentWallMs = QDateTime::currentMSecsSinceEpoch();

  const quint64 epochAfter = m_coalescedEpoch.load(std::memory_order_acquire);
  if (epochAfter != epochBefore)
    queueFlushToSelf();
}

void VideoFramePresentWorker::emitToMainThread(const QImage &img, quint64 fid) {
  if (VideoFrameEvidence::shouldLogVideoStage(fid)) {
    qInfo().noquote() << VideoFrameEvidence::diagLine("PRESENT_TX", m_streamTag, fid, img);
  }
  emit presentFrameReady(img, fid);
}
