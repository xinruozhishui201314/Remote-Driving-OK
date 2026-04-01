#include "degradationmanager.h"
#include "../core/systemstatemachine.h"
#include "../utils/TimeUtils.h"
#include <QDebug>

DegradationManager::DegradationManager(SystemStateMachine* fsm, QObject* parent)
    : QObject(parent)
    , m_fsm(fsm)
{
    connect(&m_checkTimer, &QTimer::timeout,
            this, &DegradationManager::checkDegradation);
    m_checkTimer.setInterval(m_config.checkIntervalMs);
}

void DegradationManager::setConfig(const DegradationConfig& cfg)
{
    m_config = cfg;
    m_checkTimer.setInterval(cfg.checkIntervalMs);
}

bool DegradationManager::initialize()
{
    m_currentLevel    = DegradationLevel::FULL;
    m_pendingLevel    = DegradationLevel::FULL;
    m_pendingLevelSince = 0;
    qInfo() << "[Client][DegradationManager] initialized";
    return true;
}

void DegradationManager::start()
{
    m_checkTimer.start();
}

void DegradationManager::stop()
{
    m_checkTimer.stop();
}

void DegradationManager::updateNetworkQuality(const NetworkQuality& quality)
{
    m_currentScore = quality.score;
}

void DegradationManager::checkDegradation()
{
    const DegradationLevel target = calculateTargetLevel(m_currentScore);

    if (target == m_currentLevel) {
        m_pendingLevel = target;
        m_pendingLevelSince = 0;
        return;
    }

    const int64_t now = TimeUtils::nowMs();

    if (target != m_pendingLevel) {
        m_pendingLevel = target;
        m_pendingLevelSince = now;
        return;
    }

    // 检查滞后
    const bool isDowngrade = static_cast<uint8_t>(target) > static_cast<uint8_t>(m_currentLevel);
    const bool isUpgrade   = !isDowngrade;
    const int64_t elapsed  = now - m_pendingLevelSince;

    // 快降级（任何超时都立即执行）；慢升级（等待 hysteresisMs）
    if (isDowngrade || elapsed >= m_config.hysteresisMs) {
        applyLevel(target);
    }
}

DegradationManager::DegradationLevel DegradationManager::calculateTargetLevel(double score) const
{
    if (score >= m_config.level1ThresholdScore) return DegradationLevel::FULL;
    if (score >= m_config.level2ThresholdScore) return DegradationLevel::HIGH;
    if (score >= m_config.level3ThresholdScore) return DegradationLevel::MEDIUM;
    if (score >= m_config.level4ThresholdScore) return DegradationLevel::LOW;
    if (score >= m_config.level5ThresholdScore) return DegradationLevel::MINIMAL;
    return DegradationLevel::SAFETY_STOP;
}

void DegradationManager::applyLevel(DegradationLevel level)
{
    const DegradationLevel oldLevel = m_currentLevel;
    m_currentLevel   = level;
    m_pendingLevel   = level;
    m_pendingLevelSince = 0;

    const auto policy = policyForLevel(level);

    qWarning() << "[Client][DegradationManager] level change"
               << policyForLevel(oldLevel).name << "->" << policy.name
               << "score=" << m_currentScore;

    emit levelChanged(level, oldLevel);
    emit bitrateChanged(static_cast<uint32_t>(policy.maxBitrateKbps));
    emit maxSpeedChanged(policy.maxSpeedKmh);
    emit auxiliaryCamerasEnabled(policy.enableAuxCameras);

    if (level == DegradationLevel::SAFETY_STOP) {
        qCritical() << "[Client][DegradationManager] SAFETY STOP level reached";
        emit safetyStopRequired();
        if (m_fsm) m_fsm->fire(SystemStateMachine::Trigger::NETWORK_DEGRADE);
    } else if (static_cast<uint8_t>(level) > static_cast<uint8_t>(DegradationLevel::HIGH)) {
        if (m_fsm) m_fsm->fire(SystemStateMachine::Trigger::NETWORK_DEGRADE);
    } else {
        if (m_fsm && m_fsm->stateEnum() == SystemStateMachine::SystemState::DEGRADED) {
            m_fsm->fire(SystemStateMachine::Trigger::NETWORK_RECOVER);
        }
    }
}

DegradationManager::LevelPolicy DegradationManager::policyForLevel(DegradationLevel level)
{
    switch (level) {
    case DegradationLevel::FULL:
        return {level, "FULL", 20000, 60, VideoResolution::R1080P,  true,  true,  true,  80.0};
    case DegradationLevel::HIGH:
        return {level, "HIGH", 15000, 60, VideoResolution::R1080P,  true,  true,  true,  80.0};
    case DegradationLevel::MEDIUM:
        return {level, "MEDIUM", 8000, 30, VideoResolution::R720P,  false, true,  true,  60.0};
    case DegradationLevel::LOW:
        return {level, "LOW",   4000, 25, VideoResolution::R480P,   false, false, false, 40.0};
    case DegradationLevel::MINIMAL:
        return {level, "MINIMAL", 2000, 15, VideoResolution::R360P, false, false, false, 20.0};
    case DegradationLevel::SAFETY_STOP:
        return {level, "SAFETY_STOP", 500, 10, VideoResolution::R360P, false, false, false, 0.0};
    }
    return {DegradationLevel::FULL, "UNKNOWN", 20000, 60, VideoResolution::R1080P, true, true, true, 80.0};
}

DegradationManager::LevelPolicy DegradationManager::currentPolicy() const
{
    return policyForLevel(m_currentLevel);
}
