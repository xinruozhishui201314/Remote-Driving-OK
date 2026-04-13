#include "AdaptiveBitrate.h"

#include "../../utils/TimeUtils.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <numeric>

AdaptiveBitrateController::AdaptiveBitrateController(QObject* parent) : QObject(parent) {}

BitrateDecision AdaptiveBitrateController::evaluate(const NetworkMetrics& metrics) {
  // 1. Delay-based estimate (Trendline filter)
  double delayGradient = calculateDelayGradient(metrics);
  uint32_t delayEstimate = estimateFromDelay(delayGradient);

  // 2. Loss-based estimate
  uint32_t lossEstimate = estimateFromLoss(metrics.packetLossRate);

  // 3. Minimum of both
  uint32_t rawEstimate = std::min(delayEstimate, lossEstimate);

  // 4. Smooth
  uint32_t smoothed = applySmoothing(rawEstimate);

  // 5. Clamp
  smoothed = std::clamp(smoothed, m_minBitrateKbps, m_maxBitrateKbps);

  if (smoothed != m_currentBitrateKbps) {
    qDebug() << "[Client][ABR] bitrate change" << m_currentBitrateKbps << "->" << smoothed << "kbps"
             << "delay_grad=" << delayGradient << "loss=" << metrics.packetLossRate;
    m_currentBitrateKbps = smoothed;
    emit bitrateChanged(smoothed);
  }

  return makeBitrateDecision(smoothed, metrics);
}

double AdaptiveBitrateController::calculateDelayGradient(const NetworkMetrics& metrics) {
  m_delayHistory.push_back({metrics.timestamp, metrics.oneWayDelayMs});

  if (m_delayHistory.size() < kMinSamplesForGradient)
    return 0.0;

  // Linear regression on recent samples
  const std::size_t n = m_delayHistory.size();
  double sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
  for (std::size_t i = 0; i < n; ++i) {
    double x = static_cast<double>(m_delayHistory[i].timestamp);
    double y = m_delayHistory[i].delayMs;
    sumX += x;
    sumY += y;
    sumXY += x * y;
    sumXX += x * x;
  }
  const double denom = static_cast<double>(n) * sumXX - sumX * sumX;
  if (std::abs(denom) < 1e-9)
    return 0.0;
  return (static_cast<double>(n) * sumXY - sumX * sumY) / denom;
}

uint32_t AdaptiveBitrateController::estimateFromDelay(double gradient) {
  SignalState state;
  if (gradient > 0.01)
    state = SignalState::Overuse;
  else if (gradient < -0.01)
    state = SignalState::Underuse;
  else
    state = SignalState::Normal;

  return applyAIMD(m_currentBitrateKbps, state);
}

uint32_t AdaptiveBitrateController::estimateFromLoss(double lossRate) {
  if (lossRate < 0.02) {
    // < 2% loss: increase
    return static_cast<uint32_t>(m_currentBitrateKbps * 1.05);
  } else if (lossRate < 0.10) {
    // 2-10% loss: hold
    return m_currentBitrateKbps;
  } else {
    // > 10% loss: decrease 15%
    return static_cast<uint32_t>(m_currentBitrateKbps * 0.85);
  }
}

uint32_t AdaptiveBitrateController::applySmoothing(uint32_t estimate) {
  // Exponential moving average
  return static_cast<uint32_t>(m_currentBitrateKbps * 0.7 + estimate * 0.3);
}

uint32_t AdaptiveBitrateController::applyAIMD(uint32_t current, SignalState state) {
  switch (state) {
    case SignalState::Overuse:
      return static_cast<uint32_t>(current * 0.85);
    case SignalState::Normal:
      return current + kAdditiveIncreaseKbps;
    case SignalState::Underuse:
      return current;
  }
  return current;
}

BitrateDecision AdaptiveBitrateController::makeBitrateDecision(uint32_t bitrate,
                                                               const NetworkMetrics& metrics) {
  BitrateDecision d;
  d.targetBitrateKbps = bitrate;

  if (bitrate >= 15000) {
    d.resolution = VideoResolution::R4K;
    d.targetFps = 60;
    d.quality = QualityLevel::Ultra;
  } else if (bitrate >= 8000) {
    d.resolution = VideoResolution::R1080P;
    d.targetFps = 60;
    d.quality = QualityLevel::High;
  } else if (bitrate >= 4000) {
    d.resolution = VideoResolution::R720P;
    d.targetFps = 10;
    d.quality = QualityLevel::Medium;
  } else if (bitrate >= 1500) {
    d.resolution = VideoResolution::R480P;
    d.targetFps = 10;
    d.quality = QualityLevel::Low;
  } else {
    d.resolution = VideoResolution::R360P;
    d.targetFps = 15;
    d.quality = QualityLevel::Minimal;
  }

  d.reason = QString("bitrate=%1kbps loss=%2% rtt=%3ms")
                 .arg(bitrate)
                 .arg(metrics.packetLossRate * 100, 0, 'f', 1)
                 .arg(metrics.oneWayDelayMs * 2, 0, 'f', 0);
  return d;
}
