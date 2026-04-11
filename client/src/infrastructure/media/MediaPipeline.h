#pragma once
#include "../../utils/LockFreeQueue.h"
#include "FramePool.h"
#include "IHardwareDecoder.h"

#include <QMutex>
#include <QObject>
#include <QThread>

#include <memory>

/**
 * 媒体管线（《客户端架构设计》§3.1.2）。
 * 零拷贝管线：网络 → RTP解封装 → 解码（独立线程）→ 帧池 → 渲染。
 *
 * 数据流：
 *   IO线程: onVideoPacketReceived() → 压入 m_decodeQueue
 *   解码线程: 从队列取包 → IHardwareDecoder → 帧池帧 → emit frameReady
 *   UI 线程: 接收 frameReady → QVideoSink::setVideoFrame()（经 WebRtcClient）
 *
 * 背压：m_decodeQueue 为 SPSC、容量 256；满时丢帧并累计 framesDropped（见 onVideoPacketReceived）。
 */
class MediaPipeline : public QObject {
  Q_OBJECT

 public:
  struct PipelineConfig {
    std::size_t framePoolSize = 16;
    uint32_t maxWidth = 1920;
    uint32_t maxHeight = 1080;
    QString codec = "H264";
    VideoFrame::MemoryType gpuMemoryType = VideoFrame::MemoryType::CPU_MEMORY;
    uint32_t cameraId = 0;
  };

  struct PipelineStats {
    uint64_t framesDecoded = 0;
    uint64_t framesDropped = 0;
    uint64_t decodeErrors = 0;
    double avgDecodeTimeMs = 0;
    double currentFps = 0;
  };

  explicit MediaPipeline(QObject* parent = nullptr);
  ~MediaPipeline() override;

  bool initialize(const PipelineConfig& config);
  void shutdown();

  // 从网络接收视频包（从 IO 线程或信号槽调用）
  void onVideoPacketReceived(const uint8_t* data, size_t size, int64_t pts = 0);
  void onVideoPacketReceived(const QByteArray& data, int64_t pts = 0);

  // 重新配置（降级/升级时使用）
  void reconfigure(const PipelineConfig& config);

  // 动态调整帧池容量
  void setFramePoolCapacity(int capacity);

  PipelineStats stats() const {
    QMutexLocker lock(&m_statsMutex);
    return m_stats;
  }
  bool isRunning() const { return m_running; }

 signals:
  void frameReady(std::shared_ptr<VideoFrame> frame);
  void pipelineError(const QString& error);
  void statsUpdated(const PipelineStats& stats);

 private:
  struct NALUnit {
    QByteArray data;
    int64_t pts = 0;
  };

  void decodeLoop();
  void updateStats(int64_t decodeUs);

  PipelineConfig m_config;
  std::unique_ptr<IHardwareDecoder> m_decoder;
  std::shared_ptr<FramePool> m_framePool;

  SPSCQueue<NALUnit, 256> m_decodeQueue;

  QThread* m_decodeThread = nullptr;
  std::atomic<bool> m_running{false};
  mutable PipelineStats m_stats{};
  mutable QMutex m_statsMutex{};  // 保护 m_stats 的并发访问
};
