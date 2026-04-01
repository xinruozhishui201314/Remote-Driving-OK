#pragma once
#include <cstdint>
#include "../utils/CircularBuffer.h"
#include "../infrastructure/hardware/IInputDevice.h"

/**
 * 延迟补偿预测器（《客户端架构设计》§3.3.1）。
 * 基于历史输入趋势做二次多项式外推，预测 RTT/2 之后的控制状态。
 */
class LatencyCompensator {
public:
    struct PredictionResult {
        double predictedSteeringAngle = 0.0;
        double predictionHorizonMs    = 0.0;
        double confidence             = 0.5;
    };

    explicit LatencyCompensator(double processingDelayMs = 5.0,
                                 double maxPredictionAngleRad = 0.15);

    PredictionResult predict(const IInputDevice::InputState& current,
                              double currentRTTMs);

    void reset();

private:
    double quadraticExtrapolation(double horizonMs) const;
    double calculateConfidence() const;

    struct InputPoint {
        int64_t timestampUs;
        double  steeringAngle;
    };

    CircularBuffer<InputPoint, 50> m_history;
    double m_processingDelayMs;
    double m_maxPredictionAngleRad;
};
