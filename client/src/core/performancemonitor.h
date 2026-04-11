#pragma once
#include "../utils/CircularBuffer.h"
#include "../utils/PercentileStats.h"
#include "../utils/TimeUtils.h"

#include <QObject>
#include <QTimer>

/**
 * 运行时性能监控（《客户端架构设计》§7.3）。
 * 收集视频端到端延迟、控制RTT、UI帧时间、解码时间，并在超限时发出警告。
 */
class PerformanceMonitor : public QObject {
  Q_OBJECT

 public:
  struct Metrics {
    struct Latency {
      double videoE2EMs = 0;
      double controlRTTMs = 0;
      double uiFrameTimeMs = 0;
      double decodeTimeMs = 0;
      double renderTimeMs = 0;
    } latency;

    struct Throughput {
      double videoFps = 0;
      double controlHz = 0;
      double networkBandwidthMbps = 0;
    } throughput;

    struct Quality {
      double packetLossRate = 0;
      double jitterMs = 0;
      uint32_t droppedFrames = 0;
      uint32_t decoderErrors = 0;
    } quality;
  };

  explicit PerformanceMonitor(QObject* parent = nullptr);

  void start(int reportIntervalMs = 1000);
  void stop();

  // 数据上报接口（供各模块调用）
  void recordVideoE2E(int64_t latencyUs);
  void recordControlRTT(int64_t rttUs);
  void recordUIFrameTime(int64_t frameTimeUs);
  void recordDecodeTime(int64_t decodeTimeUs);
  void recordVideoFrame();
  void recordControlTick();
  void recordPacketLoss(double rate);
  void recordDroppedFrame();
  void recordDecoderError();

  Metrics currentMetrics() const;

  // RAII 计时器
  class ScopedTimer {
   public:
    ScopedTimer(PerformanceMonitor& mon, std::function<void(int64_t)> recorder)
        : m_mon(mon), m_recorder(std::move(recorder)), m_start(TimeUtils::nowUs()) {}

    ~ScopedTimer() { m_recorder(TimeUtils::nowUs() - m_start); }

   private:
    PerformanceMonitor& m_mon;
    std::function<void(int64_t)> m_recorder;
    int64_t m_start;
  };

 signals:
  void metricsUpdated(const Metrics& metrics);
  void performanceAlert(const QString& message);

 private slots:
  void collectAndReport();

 private:
  QTimer m_reportTimer;
  PercentileStats<1000> m_videoE2EStats;
  PercentileStats<1000> m_controlRTTStats;
  PercentileStats<500> m_uiFrameStats;
  PercentileStats<500> m_decodeStats;
  FPSCounter m_videoFps;
  FPSCounter m_controlHz;
  double m_packetLossRate = 0.0;
  uint32_t m_droppedFrames = 0;
  uint32_t m_decoderErrors = 0;

  static constexpr double kMaxVideoE2EMs = 100.0;
  static constexpr double kMaxUIFrameMs = 16.67;  // 60fps
  static constexpr double kMaxControlRTTMs = 200.0;
};
