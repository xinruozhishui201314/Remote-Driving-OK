#ifndef VIDEOFRAMEPRESENTWORKER_H
#define VIDEOFRAMEPRESENTWORKER_H

#include <QImage>
#include <QObject>
#include <QTimer>

#include <atomic>

/**
 * @brief 运行在独立 QThread 上的呈现调度：合帧、限频，再 QueuedConnection 到主线程输出。
 *
 * 与 WebRtcClient 解耦：解码线程 frameReady → QueuedConnection 到本 worker（呈现线程），
 * 避免在 GUI 线程上执行合帧与限频定时器。
 */
class VideoFramePresentWorker : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(VideoFramePresentWorker)

 public:
  explicit VideoFramePresentWorker(QObject *parent = nullptr);

  void setStreamTag(const QString &tag) { m_streamTag = tag; }

 public slots:
  void ingestDecoderFrame(QImage image, quint64 frameId);
  /** 断流/重连时清空状态（在呈现线程调用） */
  void resetState();
  /** 仅在 worker 线程调用：移回 QCoreApplication::thread()，供主线程安全 delete（WebRtcClient::stopPresentPipeline） */
  void moveToApplicationThread();

 private slots:
  void flushCoalesced();

 signals:
  /** 每路解码帧进入呈现 worker（用于统计 vse / 深度，与主线程 Queued 解耦） */
  void frameIngressed();
  void coalescedDropOccurred();
  void rateLimitSkipped();
  void flushCoalescedTick();
  /** 已合帧/限频，需在主线程交给 QVideoSink 或 RemoteVideoSurface */
  void presentFrameReady(QImage image, quint64 frameId);

 private:
  void queueFlushToSelf();
  void emitToMainThread(const QImage &img, quint64 fid);

  QString m_streamTag;
  QImage m_coalescedImage;
  quint64 m_coalescedFrameId = 0;
  std::atomic<quint64> m_coalescedEpoch{0};
  std::atomic<bool> m_flushQueued{false};
  QTimer *m_rateTimer = nullptr;
  qint64 m_lastPresentWallMs = 0;
};

#endif
