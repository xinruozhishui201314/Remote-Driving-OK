#include "safetymonitorservice.h"

#include "../core/eventbus.h"
#include "../core/systemstatemachine.h"
#include "../utils/TimeUtils.h"
#include "../vehiclestatus.h"

#include <QDebug>

#include <atomic>

namespace {
std::atomic<int64_t> g_safetyUnitTestNowMs{0};
std::atomic<bool> g_safetyUseUnitTestClock{false};
}  // namespace

void SafetyMonitorService::setUnitTestNowMsForTesting(int64_t ms) {
  g_safetyUnitTestNowMs.store(ms, std::memory_order_relaxed);
  g_safetyUseUnitTestClock.store(true, std::memory_order_relaxed);
}

void SafetyMonitorService::clearUnitTestClockForTesting() {
  g_safetyUseUnitTestClock.store(false, std::memory_order_relaxed);
}

static int64_t safetyNowMs() {
  if (g_safetyUseUnitTestClock.load(std::memory_order_relaxed))
    return g_safetyUnitTestNowMs.load(std::memory_order_relaxed);
  return TimeUtils::nowMs();
}

SafetyMonitorService::SafetyMonitorService(VehicleStatus* vs, SystemStateMachine* fsm,
                                           QObject* parent)
    : QObject(parent), m_vehicleStatus(vs), m_fsm(fsm) {
  m_safetyTimer.setTimerType(Qt::PreciseTimer);
  m_safetyTimer.setInterval(kSafetyCheckIntervalMs);
  connect(&m_safetyTimer, &QTimer::timeout, this, &SafetyMonitorService::runSafetyChecks);
}

SafetyMonitorService::~SafetyMonitorService() { stop(); }

void SafetyMonitorService::setConfig(const Config& cfg) { m_config = cfg; }

bool SafetyMonitorService::initialize() {
  const int64_t now = safetyNowMs();
  m_lastHeartbeatMs = now;
  m_lastOperatorActivityMs = now;
  m_latencyViolationCount = 0;
  m_missedHeartbeats = 0;
  qInfo() << "[Client][SafetyMonitorService] initialized"
          << "checkHz=" << kSafetyCheckHz << "deadman=" << m_config.deadmanTimeoutMs << "ms"
          << "latencyLimit=" << m_config.maxOneWayLatencyMs << "ms";
  return true;
}

void SafetyMonitorService::start() {
  if (m_started)
    return;
  m_started = true;
  const int64_t now = safetyNowMs();
  m_lastHeartbeatMs = now;
  m_lastOperatorActivityMs = now;
  m_safetyTimer.start();
  qInfo() << "[Client][SafetyMonitorService] started 50Hz safety checks";
}

void SafetyMonitorService::stop() {
  m_safetyTimer.stop();
  m_started = false;
  m_deadmanActive.store(false);
}

void SafetyMonitorService::updateLatency(double oneWayMs, double rttMs) {
  m_currentOneWayMs.store(oneWayMs);
  m_currentRTTMs.store(rttMs);
}

void SafetyMonitorService::onHeartbeatReceived() {
  m_lastHeartbeatMs = safetyNowMs();
  m_missedHeartbeats = 0;
}

void SafetyMonitorService::onOperatorActivity() {
  m_lastOperatorActivityMs = safetyNowMs();
  m_deadmanActive.store(true);
}

// ─── 50Hz 安全检查主循环 ──────────────────────────────────────────────────────

void SafetyMonitorService::runSafetyChecks() {
  checkLatency();
  checkHeartbeat();
  checkOperatorActivity();
  checkDeadman();
}

void SafetyMonitorService::checkLatency() {
  const double currentOneWay = m_currentOneWayMs.load();
  if (currentOneWay <= 0)
    return;  // 无数据

  if (currentOneWay > m_config.maxOneWayLatencyMs) {
    ++m_latencyViolationCount;
    const QString msg = QString("[Client][SafetyMonitorService] latency violation #%1: %2ms > %3ms")
                            .arg(m_latencyViolationCount)
                            .arg(currentOneWay, 0, 'f', 1)
                            .arg(m_config.maxOneWayLatencyMs);
    qWarning().noquote() << msg;

    if (m_latencyViolationCount >= m_config.emergencyTriggerCount) {
      triggerEmergencyStop(QString("Latency too high: %1ms for %2 consecutive checks")
                               .arg(currentOneWay, 0, 'f', 1)
                               .arg(m_latencyViolationCount));
      m_latencyViolationCount = 0;
    } else if (currentOneWay > m_config.warningLatencyMs) {
      emit safetyWarning(msg);
    }
  } else {
    if (m_latencyViolationCount > 0) {
      qInfo() << "[Client][SafetyMonitorService] latency recovered";
    }
    m_latencyViolationCount = 0;
  }
}

void SafetyMonitorService::checkHeartbeat() {
  const int64_t now = safetyNowMs();

  // 未进入 DRIVING/DEGRADED：不要求车端 status 心跳（PRE_FLIGHT 仅等待接管确认，避免仅视频通时误报）。
  const bool strictVehicleHb =
      m_fsm && m_fsm->vehicleTelemetryHeartbeatRequired();
  if (!strictVehicleHb) {
    m_missedHeartbeats = 0;
    m_lastHeartbeatMs = now;
    return;
  }

  const int64_t elapsed = now - m_lastHeartbeatMs;

  if (elapsed > m_config.heartbeatTimeoutMs) {
    ++m_missedHeartbeats;
    const QString msg = QString("[Client][SafetyMonitorService] heartbeat missed #%1 elapsed=%2ms")
                            .arg(m_missedHeartbeats)
                            .arg(elapsed);

    if (m_missedHeartbeats >= m_config.missedBeforeEmergency) {
      qCritical().noquote() << msg;
      triggerEmergencyStop(
          QString("Heartbeat lost: %1 consecutive misses").arg(m_missedHeartbeats));
      m_missedHeartbeats = 0;
    } else if (m_missedHeartbeats >= m_config.missedBeforeWarning) {
      qWarning().noquote() << msg;
      emit safetyWarning(msg);
    }
  }
}

void SafetyMonitorService::checkOperatorActivity() {
  if (!m_fsm || !m_fsm->vehicleTelemetryHeartbeatRequired())
    return;

  const int64_t now = safetyNowMs();
  const int64_t inactiveMs = now - m_lastOperatorActivityMs;

  if (m_deadmanActive.load() && inactiveMs > m_config.inactivityTimeoutMs) {
    const QString msg =
        QString("[Client][SafetyMonitorService] operator inactive for %1ms").arg(inactiveMs);
    qWarning().noquote() << msg;
    emit safetyWarning(msg);
    emit speedLimitRequested(20.0);  // 降速
  }
}

void SafetyMonitorService::checkDeadman() {
  if (!m_fsm || !m_fsm->vehicleTelemetryHeartbeatRequired())
    return;

  const int64_t now = safetyNowMs();
  const int64_t inactiveMs = now - m_lastOperatorActivityMs;

  if (m_deadmanActive.load() && inactiveMs > m_config.deadmanTimeoutMs) {
    qCritical() << "[Client][SafetyMonitorService] DEADMAN timeout" << inactiveMs
                << "ms without activity";
    triggerEmergencyStop(QString("Deadman timer expired: no input for %1ms").arg(inactiveMs));
    m_deadmanActive.store(false);
  }
}

void SafetyMonitorService::triggerEmergencyStop(const QString& reason) {
  qCritical() << "[Client][SafetyMonitorService] EMERGENCY STOP:" << reason;

  emit emergencyStopTriggered(reason);
  emit safetyStatusChanged(false);

  if (m_fsm) {
    if (!m_fsm->fire(SystemStateMachine::Trigger::EMERGENCY_STOP)) {
      qWarning().noquote()
          << "[Client][SafetyMonitorService] FSM rejected EMERGENCY_STOP (state="
          << m_fsm->currentState() << ") — EventBus 仍已发布 CRITICAL";
    }
  }

  EmergencyStopEvent evt;
  evt.reason = reason;
  evt.source = EmergencyStopEvent::Source::SAFETY_MONITOR;
  EventBus::instance().publish(evt);
}
