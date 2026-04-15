#pragma once
#include "../../core/performancemonitor.h"
#include "../../utils/LockFreeQueue.h"
#include "FramePool.h"
#include "IHardwareDecoder.h"

#include <QMutex>
#include <QObject>
#include <QThread>

#include <memory>

/**
 * 媒体管线（《客户端架构设计》§3.1.2）。
 * 2025/2026 规范要求：全链路延迟打桩。
 */
class MediaPipeline : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(MediaPipeline)

 public:
  struct PipelineConfig {
    std::size_t framePoolSize = 16;
    uint32_t maxWidth = 1920;
    uint32_t maxHeight = 1080;
    QString codec = "H264";
    VideoFrame::MemoryType gpuMemoryType = VideoFrame::MemoryType::CPU_MEMORY;
    uint32_t cameraId = 0;
    PerformanceMonitor* perfMonitor = nullptr;
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
    int64_t captureTimestamp = 0;
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
