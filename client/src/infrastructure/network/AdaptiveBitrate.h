#pragma once
#include "../../../src/utils/CircularBuffer.h"

#include <QObject>

#include <cstdint>

/**
 * 自适应码率控制器 - 基于 Google GCC 算法改进（《客户端架构设计》§3.1.1）。
 * Delay-based（延迟梯度）+ Loss-based（丢包率）+ AIMD。
 */

struct NetworkMetrics {
  double oneWayDelayMs = 0;
  double packetLossRate = 0;
  double bandwidthKbps = 0;
  int64_t timestamp = 0;
};

enum class VideoResolution { R360P, R480P, R720P, R1080P, R4K };
enum class QualityLevel { Minimal, Low, Medium, High, Ultra };
enum class SignalState { Underuse, Normal, Overuse };

struct BitrateDecision {
  uint32_t targetBitrateKbps = 5000;
  uint32_t targetFps = 30;
  VideoResolution resolution = VideoResolution::R720P;
  QualityLevel quality = QualityLevel::Medium;
  QString reason;
};

class AdaptiveBitrateController : public QObject {
  Q_OBJECT

 public:
  explicit AdaptiveBitrateController(QObject* parent = nullptr);

  BitrateDecision evaluate(const NetworkMetrics& metrics);

  void setMinBitrate(uint32_t kbps) { m_minBitrateKbps = kbps; }
  void setMaxBitrate(uint32_t kbps) { m_maxBitrateKbps = kbps; }
  uint32_t currentBitrate() const { return m_currentBitrateKbps; }

 signals:
  void bitrateChanged(uint32_t targetKbps);

 private:
  double calculateDelayGradient(const NetworkMetrics& metrics);
  uint32_t estimateFromDelay(double delayGradient);
  uint32_t estimateFromLoss(double lossRate);
  uint32_t applySmoothing(uint32_t estimate);
  uint32_t applyAIMD(uint32_t current, SignalState state);
  BitrateDecision makeBitrateDecision(uint32_t bitrate, const NetworkMetrics& metrics);

  struct DelayPoint {
    int64_t timestamp;
    double delayMs;
  };

  CircularBuffer<DelayPoint, 200> m_delayHistory;
  uint32_t m_currentBitrateKbps = 5000;
  uint32_t m_minBitrateKbps = 500;
  uint32_t m_maxBitrateKbps = 20000;

  static constexpr uint32_t kAdditiveIncreaseKbps = 100;
  static constexpr std::size_t kMinSamplesForGradient = 20;
};
