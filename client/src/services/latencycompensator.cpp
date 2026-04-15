#include "latencycompensator.h"

#include "../utils/TimeUtils.h"

#include <algorithm>
#include <cmath>

LatencyCompensator::LatencyCompensator(double processingDelayMs, double maxPredictionAngleRad)
    : m_processingDelayMs(processingDelayMs),
      m_maxPredictionAngleRad(maxPredictionAngleRad),
      m_history() {}

LatencyCompensator::PredictionResult LatencyCompensator::predict(
    const IInputDevice::InputState& current, double currentRTTMs) {
  const int64_t now = TimeUtils::nowUs();
  m_history.push_back({now, current.steeringAngle});

  const double horizonMs = currentRTTMs / 2.0 + m_processingDelayMs;

  if (m_history.size() < 3) {
    return {current.steeringAngle, horizonMs, 0.3};
  }

  const double predicted = quadraticExtrapolation(horizonMs);
  const double clamped = std::clamp(predicted, current.steeringAngle - m_maxPredictionAngleRad,
                                    current.steeringAngle + m_maxPredictionAngleRad);

  return {clamped, horizonMs, calculateConfidence()};
}

void LatencyCompensator::reset() { m_history.clear(); }

double LatencyCompensator::quadraticExtrapolation(double horizonMs) const {
  const std::size_t n = m_history.size();
  if (n < 3)
    return m_history.back().steeringAngle;

  // Use last 3 samples for quadratic fit
  const std::size_t i0 = n - 3, i1 = n - 2, i2 = n - 1;
  const double t0 = static_cast<double>(m_history[i0].timestampUs) / 1000.0;  // ms
  const double t1 = static_cast<double>(m_history[i1].timestampUs) / 1000.0;
  const double t2 = static_cast<double>(m_history[i2].timestampUs) / 1000.0;
  const double y0 = m_history[i0].steeringAngle;
  const double y1 = m_history[i1].steeringAngle;
  const double y2 = m_history[i2].steeringAngle;

  // Lagrange quadratic interpolation/extrapolation
  const double tTarget = t2 + horizonMs;

  double result = 0.0;
  const double pts[3] = {t0, t1, t2};
  const double vals[3] = {y0, y1, y2};
  for (int j = 0; j < 3; ++j) {
    double term = vals[j];
    for (int k = 0; k < 3; ++k) {
      if (k != j) {
        const double denom = pts[j] - pts[k];
        if (std::abs(denom) < 1e-9)
          return m_history.back().steeringAngle;
        term *= (tTarget - pts[k]) / denom;
      }
    }
    result += term;
  }
  return result;
}

double LatencyCompensator::calculateConfidence() const {
  if (m_history.size() < 5)
    return 0.4;

  // Confidence based on variance of recent steering rates
  double sumDiff = 0.0;
  const std::size_t n = std::min(m_history.size(), std::size_t{5});
  for (std::size_t i = m_history.size() - n + 1; i < m_history.size(); ++i) {
    sumDiff += std::abs(m_history[i].steeringAngle - m_history[i - 1].steeringAngle);
  }
  // Low variance = high confidence
  const double avgVariance = sumDiff / (n - 1);
  return std::max(0.2, 1.0 - avgVariance * 5.0);
}
