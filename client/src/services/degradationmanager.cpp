#include "degradationmanager.h"

#include "../core/systemstatemachine.h"
#include "../utils/TimeUtils.h"

#include <QDebug>

#include <atomic>

namespace {
std::atomic<int64_t> g_degUnitTestNowMs{0};
std::atomic<bool> g_degUseUnitTestClock{false};
}  // namespace

void DegradationManager::setUnitTestNowMsForTesting(int64_t ms) {
  g_degUnitTestNowMs.store(ms, std::memory_order_relaxed);
  g_degUseUnitTestClock.store(true, std::memory_order_relaxed);
}

void DegradationManager::clearUnitTestClockForTesting() {
  g_degUseUnitTestClock.store(false, std::memory_order_relaxed);
}

static int64_t degradationNowMs() {
  if (g_degUseUnitTestClock.load(std::memory_order_relaxed))
    return g_degUnitTestNowMs.load(std::memory_order_relaxed);
  return TimeUtils::nowMs();
}

DegradationManager::DegradationManager(SystemStateMachine* fsm, QObject* parent)
    : QObject(parent), m_fsm(fsm) {
  connect(&m_checkTimer, &QTimer::timeout, this, &DegradationManager::checkDegradation);
  m_checkTimer.setInterval(m_config.checkIntervalMs);
}

void DegradationManager::setConfig(const DegradationConfig& cfg) {
  m_config = cfg;
  m_checkTimer.setInterval(cfg.checkIntervalMs);
}

bool DegradationManager::initialize() {
  m_currentLevel = DegradationLevel::FULL;
  m_pendingLevel = DegradationLevel::FULL;
  m_pendingLevelSince = 0;
  qInfo() << "[Client][DegradationManager] initialized";
  return true;
}

void DegradationManager::start() { m_checkTimer.start(); }

void DegradationManager::stop() { m_checkTimer.stop(); }

void DegradationManager::updateNetworkQuality(const NetworkQuality& quality) {
  m_currentScore = quality.score;
}

void DegradationManager::checkDegradation() {
  const DegradationLevel target = calculateTargetLevel(m_currentScore);

  if (target == m_currentLevel) {
    m_pendingLevel = target;
    m_pendingLevelSince = 0;
    return;
  }

  const int64_t now = degradationNowMs();

  if (target != m_pendingLevel) {
    m_pendingLevel = target;
    m_pendingLevelSince = now;
    return;
  }

  // 检查滞后
  const bool isDowngrade = static_cast<uint8_t>(target) > static_cast<uint8_t>(m_currentLevel);
  const bool isUpgrade = !isDowngrade;
  const int64_t elapsed = now - m_pendingLevelSince;

  // 快降级（任何超时都立即执行）；慢升级（等待 hysteresisMs）
  if (isDowngrade || elapsed >= m_config.hysteresisMs) {
    applyLevel(target);
  }
}

DegradationManager::DegradationLevel DegradationMapping::targetLevelFromNetworkScore(
    bool restrictToFullWhenIdle, double score,
    const DegradationManager::DegradationConfig& config) {
  // 未进入 DRIVING/DEGRADED 时，mqtt/video 未连接会使 NetworkQuality
  // score=0，这是常态而非「链路灾难」。 若仍按 score 映射到 SAFETY_STOP，会误触发
  // FSM::NETWORK_DEGRADE（IDLE 下无合法转移），并干扰登录/选车。
  if (restrictToFullWhenIdle)
    return DegradationManager::DegradationLevel::FULL;

  if (score >= config.level1ThresholdScore)
    return DegradationManager::DegradationLevel::FULL;
  if (score >= config.level2ThresholdScore)
    return DegradationManager::DegradationLevel::HIGH;
  if (score >= config.level3ThresholdScore)
    return DegradationManager::DegradationLevel::MEDIUM;
  if (score >= config.level4ThresholdScore)
    return DegradationManager::DegradationLevel::LOW;
  if (score >= config.level5ThresholdScore)
    return DegradationManager::DegradationLevel::MINIMAL;
  return DegradationManager::DegradationLevel::SAFETY_STOP;
}

DegradationManager::DegradationLevel DegradationManager::calculateTargetLevel(double score) const {
  const bool restrictToFull = m_fsm && !m_fsm->isDriveActive();
  return DegradationMapping::targetLevelFromNetworkScore(restrictToFull, score, m_config);
}

void DegradationManager::applyLevel(DegradationLevel level) {
  const DegradationLevel oldLevel = m_currentLevel;
  m_currentLevel = level;
  m_pendingLevel = level;
  m_pendingLevelSince = 0;

  const auto policy = policyForLevel(level);

  qWarning() << "[Client][DegradationManager] level change" << policyForLevel(oldLevel).name << "->"
             << policy.name << "score=" << m_currentScore;

  emit levelChanged(level, oldLevel);
  emit bitrateChanged(static_cast<uint32_t>(policy.maxBitrateKbps));
  emit maxSpeedChanged(policy.maxSpeedKmh);
  emit auxiliaryCamerasEnabled(policy.enableAuxCameras);

  if (level == DegradationLevel::SAFETY_STOP) {
    qCritical() << "[Client][DegradationManager] SAFETY STOP level reached";
    emit safetyStopRequired();
    if (m_fsm)
      m_fsm->fire(SystemStateMachine::Trigger::NETWORK_DEGRADE);
  } else if (static_cast<uint8_t>(level) > static_cast<uint8_t>(DegradationLevel::HIGH)) {
    if (m_fsm)
      m_fsm->fire(SystemStateMachine::Trigger::NETWORK_DEGRADE);
  } else {
    if (m_fsm && m_fsm->stateEnum() == SystemStateMachine::SystemState::DEGRADED) {
      m_fsm->fire(SystemStateMachine::Trigger::NETWORK_RECOVER);
    }
  }
}

DegradationManager::LevelPolicy DegradationManager::policyForLevel(DegradationLevel level) {
  switch (level) {
    case DegradationLevel::FULL:
      return {level, "FULL", 20000, 10, VideoResolution::R1080P, true, true, true, 80.0};
    case DegradationLevel::HIGH:
      return {level, "HIGH", 15000, 10, VideoResolution::R1080P, true, true, true, 80.0};
    case DegradationLevel::MEDIUM:
      return {level, "MEDIUM", 8000, 10, VideoResolution::R720P, false, true, true, 60.0};
    case DegradationLevel::LOW:
      return {level, "LOW", 4000, 10, VideoResolution::R480P, false, false, false, 40.0};
    case DegradationLevel::MINIMAL:
      return {level, "MINIMAL", 2000, 10, VideoResolution::R360P, false, false, false, 20.0};
    case DegradationLevel::SAFETY_STOP:
      return {level, "SAFETY_STOP", 500, 10, VideoResolution::R360P, false, false, false, 0.0};
  }
  return {DegradationLevel::FULL,
          "UNKNOWN",
          20000,
          60,
          VideoResolution::R1080P,
          true,
          true,
          true,
          80.0};
}

DegradationManager::LevelPolicy DegradationManager::currentPolicy() const {
  return policyForLevel(m_currentLevel);
}
