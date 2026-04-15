#include "safetymonitorservice.h"

#include "vehiclecontrolservice.h"
#include "../core/systemstatemachine.h"
#include "../utils/TimeUtils.h"
#include "../vehiclestatus.h"

#include <QDebug>
#include <QThread>

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

/**
 * 【内部分离工作类】运行在独立的安全线程中。
 * 负责定时器与核心检查逻辑，避免 UI 挂起导致安全失效。
 */
class SafetyWorker : public QObject {
  Q_OBJECT
 public:
  explicit SafetyWorker(SafetyMonitorService* svc) : m_svc(svc) {
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(SafetyMonitorService::kSafetyCheckIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &SafetyWorker::runSafetyChecks);
  }

  void start() { m_timer.start(); }
  void stop() { m_timer.stop(); }

 public slots:
  void runSafetyChecks() {
    checkLatency();
    checkHeartbeat();
    checkOperatorActivity();
    checkDeadman();
    checkUiStall();
    checkCircuitBreaker();
  }

 private:
  void checkLatency() {
    const double currentOneWay = m_svc->m_currentOneWayMs.load();
    if (currentOneWay <= 0) return;

    if (currentOneWay > m_svc->m_config.maxOneWayLatencyMs) {
      int count = ++m_svc->m_latencyViolationCount;
      const QString msg =
          QString("[Client][SafetyMonitorService] latency violation #%1: %2ms > %3ms")
              .arg(count)
              .arg(currentOneWay, 0, 'f', 1)
              .arg(m_svc->m_config.maxOneWayLatencyMs);
      qWarning().noquote() << msg;

      if (count >= m_svc->m_config.emergencyTriggerCount) {
        m_svc->triggerEmergencyStop(QString("Latency too high: %1ms for %2 consecutive checks")
                                        .arg(currentOneWay, 0, 'f', 1)
                                        .arg(count));
        m_svc->m_latencyViolationCount.store(0);
      } else if (currentOneWay > m_svc->m_config.warningLatencyMs) {
        emit m_svc->safetyWarning(msg);
      }
    } else {
      if (m_svc->m_latencyViolationCount.load() > 0) {
        qInfo() << "[Client][SafetyMonitorService] latency recovered";
      }
      m_svc->m_latencyViolationCount.store(0);
    }
  }

  void checkHeartbeat() {
    const int64_t now = safetyNowMs();
    const bool strictVehicleHb = m_svc->m_fsm && m_svc->m_fsm->vehicleTelemetryHeartbeatRequired();
    if (!strictVehicleHb) {
      m_svc->m_missedHeartbeats.store(0);
      m_svc->m_lastHeartbeatMs.store(now);
      return;
    }

    const int64_t elapsed = now - m_svc->m_lastHeartbeatMs.load();
    if (elapsed > m_svc->m_config.heartbeatTimeoutMs) {
      int missed = ++m_svc->m_missedHeartbeats;
      const QString msg =
          QString("[Client][SafetyMonitorService] heartbeat missed #%1 elapsed=%2ms")
              .arg(missed)
              .arg(elapsed);

      if (missed >= m_svc->m_config.missedBeforeEmergency) {
        qCritical().noquote() << msg;
        m_svc->triggerEmergencyStop(QString("Heartbeat lost: %1 consecutive misses").arg(missed));
        m_svc->m_missedHeartbeats.store(0);
      } else if (missed >= m_svc->m_config.missedBeforeWarning) {
        qWarning().noquote() << msg;
        emit m_svc->safetyWarning(msg);
      }
    }
  }

  void checkOperatorActivity() {
    if (!m_svc->m_fsm || !m_svc->m_fsm->vehicleTelemetryHeartbeatRequired()) return;

    const int64_t now = safetyNowMs();
    const int64_t inactiveMs = now - m_svc->m_lastOperatorActivityMs.load();

    if (m_svc->m_deadmanActive.load() && inactiveMs > m_svc->m_config.inactivityTimeoutMs) {
      const QString msg =
          QString("[Client][SafetyMonitorService] operator inactive for %1ms").arg(inactiveMs);
      qWarning().noquote() << msg;
      emit m_svc->safetyWarning(msg);
      emit m_svc->speedLimitRequested(20.0);
    }
  }

  void checkDeadman() {
    if (!m_svc->m_fsm || !m_svc->m_fsm->vehicleTelemetryHeartbeatRequired()) return;

    const int64_t now = safetyNowMs();
    const int64_t inactiveMs = now - m_svc->m_lastOperatorActivityMs.load();

    if (m_svc->m_deadmanActive.load() && inactiveMs > m_svc->m_config.deadmanTimeoutMs) {
      qCritical() << "[Client][SafetyMonitorService] DEADMAN timeout" << inactiveMs
                  << "ms without activity";
      m_svc->triggerEmergencyStop(
          QString("Deadman timer expired: no input for %1ms").arg(inactiveMs));
      m_svc->m_deadmanActive.store(false);
    }
  }

  void checkUiStall() {
    const int64_t now = safetyNowMs();
    const int64_t lastHb = m_svc->m_lastUiHeartbeatMs.load(std::memory_order_acquire);
    if (lastHb == 0) return;

    const int64_t elapsed = now - lastHb;
    if (elapsed > 150) {
      static int64_t s_lastLogMs = 0;
      if (now - s_lastLogMs >= 500) {
        s_lastLogMs = now;
        qWarning().noquote() << "[Client][SafetyMonitorService] ★ UI 线程卡顿延迟 =" << elapsed
                             << "ms";
      }

      if (elapsed > 200) {
        qCritical().noquote()
            << "[Client][SafetyMonitorService] ★★★ UI 线程疑似死锁或严重卡顿超过 200ms ★★★"
            << " elapsed=" << elapsed << "ms" << " ★ 强制触发紧急停车(Decoupled Watchdog)";
        m_svc->triggerEmergencyStop(QStringLiteral("UI thread stalled for %1ms").arg(elapsed));
      }
    }
  }

  void checkCircuitBreaker() {
    bool ok = true;
    if (m_svc->m_vehicleStatus && !m_svc->m_vehicleStatus->mqttConnected()) {
      ok = false;
    }
    if (m_svc->m_fsm && m_svc->m_fsm->isDriveActive()) {
      if (m_svc->m_vehicleStatus && !m_svc->m_vehicleStatus->videoConnected()) {
        ok = false;
      }
    }
    if (m_svc->m_emergencyActive.load()) {
      ok = false;
    }

    const bool prev = m_svc->m_allSystemsGo.exchange(ok);
    if (prev != ok) {
      qWarning() << "[Client][Safety] Circuit Breaker state changed:" << prev << "->" << ok;
      m_svc->notifyAllSystemsGo(ok);
    }
  }

  SafetyMonitorService* m_svc;
  QTimer m_timer;
};

SafetyMonitorService::SafetyMonitorService(VehicleStatus* vs, SystemStateMachine* fsm,
                                           QObject* parent)
    : QObject(parent), m_vehicleStatus(vs), m_fsm(fsm) {
  m_worker = std::make_unique<SafetyWorker>(this);

  // 订阅异常总线
  m_errorSub = EventBus::instance().subscribe<SystemErrorEvent>(
      [this](const SystemErrorEvent& e) { onSystemError(e); });
}

SafetyMonitorService::~SafetyMonitorService() {
  stop();
  if (m_errorSub > 0) {
    EventBus::instance().unsubscribe(m_errorSub);
  }
}

void SafetyMonitorService::notifyEmergency(const QString& reason) {
  // 确保在主线程发射信号（若当前在 Worker 线程则排队）
  if (QThread::currentThread() != this->thread()) {
    QMetaObject::invokeMethod(this, [this, reason]() { notifyEmergency(reason); },
                              Qt::QueuedConnection);
    return;
  }
  {
    QMutexLocker lock(&m_reasonMutex);
    m_emergencyReason = reason;
  }
  emit emergencyActiveChanged();
}

void SafetyMonitorService::notifyAllSystemsGo(bool ok) {
  if (QThread::currentThread() != this->thread()) {
    QMetaObject::invokeMethod(this, [this, ok]() { notifyAllSystemsGo(ok); }, Qt::QueuedConnection);
    return;
  }
  emit allSystemsGoChanged(ok);
}

void SafetyMonitorService::onSystemError(const SystemErrorEvent& evt) {
  if (evt.severity == SystemErrorEvent::Severity::CRITICAL) {
    qCritical() << "[Client][Safety] CRITICAL error received from domain" << evt.domain << ":"
                << evt.message << " — Triggering immediate E-STOP";
    triggerEmergencyStop(QString("[%1] %2").arg(evt.domain, evt.message));
  } else if (evt.severity == SystemErrorEvent::Severity::ERROR) {
    qWarning() << "[Client][Safety] ERROR received from domain" << evt.domain << ":" << evt.message;
    emit safetyWarning(evt.message);
  }
}

void SafetyMonitorService::setConfig(const Config& cfg) { m_config = cfg; }

bool SafetyMonitorService::initialize() {
  const int64_t now = safetyNowMs();
  m_lastHeartbeatMs.store(now);
  m_lastOperatorActivityMs.store(now);
  m_latencyViolationCount.store(0);
  m_missedHeartbeats.store(0);
  m_allSystemsGo.store(false);
  qInfo() << "[Client][SafetyMonitorService] initialized"
          << "checkHz=" << kSafetyCheckHz << "deadman=" << m_config.deadmanTimeoutMs << "ms"
          << "latencyLimit=" << m_config.maxOneWayLatencyMs << "ms";
  return true;
}

void SafetyMonitorService::attachToSafetyThread(QThread* thread) {
  if (thread && m_worker) {
    m_worker->moveToThread(thread);
    qInfo() << "[Client][SafetyMonitorService] worker moved to safety thread";
  }
}

void SafetyMonitorService::start() {
  if (m_started.exchange(true)) return;
  m_emergencyActive.store(false);
  m_allSystemsGo.store(true);  // 启动时尝试恢复正常
  {
    QMutexLocker lock(&m_reasonMutex);
    m_emergencyReason.clear();
  }
  emit emergencyActiveChanged();
  emit allSystemsGoChanged(true);

  const int64_t now = safetyNowMs();
  m_lastHeartbeatMs.store(now);
  m_lastOperatorActivityMs.store(now);

  // 通知 Worker 启动定时器
  QMetaObject::invokeMethod(m_worker.get(), &SafetyWorker::start, Qt::QueuedConnection);
  qInfo() << "[Client][SafetyMonitorService] started 50Hz safety checks via worker";
}

void SafetyMonitorService::stop() {
  if (!m_started.exchange(false)) return;
  // 通知 Worker 停止定时器
  QMetaObject::invokeMethod(m_worker.get(), &SafetyWorker::stop, Qt::QueuedConnection);

  m_deadmanActive.store(false);
  m_emergencyActive.store(false);
  m_allSystemsGo.store(false);
  {
    QMutexLocker lock(&m_reasonMutex);
    m_emergencyReason.clear();
  }
  emit emergencyActiveChanged();
  emit allSystemsGoChanged(false);
}

void SafetyMonitorService::runSafetyChecks() {
  if (m_worker) {
    // 同线程直接调，异线程排队（单测通常在同线程执行以保证同步）
    if (QThread::currentThread() == m_worker->thread()) {
      m_worker->runSafetyChecks();
    } else {
      QMetaObject::invokeMethod(m_worker.get(), &SafetyWorker::runSafetyChecks, Qt::BlockingQueuedConnection);
    }
  }
}

void SafetyMonitorService::updateLatency(double oneWayMs, double rttMs) {
  m_currentOneWayMs.store(oneWayMs);
  m_currentRTTMs.store(rttMs);
}

void SafetyMonitorService::onHeartbeatReceived() {
  m_lastHeartbeatMs.store(safetyNowMs());
  m_missedHeartbeats.store(0);
}

void SafetyMonitorService::onOperatorActivity() {
  m_lastOperatorActivityMs.store(safetyNowMs());
  m_deadmanActive.store(true);
}

void SafetyMonitorService::noteUiHeartbeat() {
  m_lastUiHeartbeatMs.store(TimeUtils::nowMs(), std::memory_order_release);
}

void SafetyMonitorService::triggerEmergencyStop(const QString& reason) {
  qCritical() << "[Client][SafetyMonitorService] EMERGENCY STOP:" << reason;

  if (!m_emergencyActive.exchange(true)) {
    notifyEmergency(reason);
  }

  emit emergencyStopTriggered(reason);
  emit safetyStatusChanged(false);

  if (m_fsm) {
    if (!m_fsm->fire(SystemStateMachine::Trigger::EMERGENCY_STOP)) {
      qWarning().noquote() << "[Client][SafetyMonitorService] FSM rejected EMERGENCY_STOP (state="
                           << m_fsm->currentState() << ") — EventBus 仍已发布 CRITICAL";
    }
  }

  // ★ 绕过 UI 线程，直接通知控车服务发送急停指令
#ifndef REMOTE_DRIVING_UNIT_TEST
  if (m_control) {
    m_control->requestEmergencyStop();
  }
#endif

  EmergencyStopEvent evt;
  evt.reason = reason;
  evt.source = EmergencyStopEvent::Source::SAFETY_MONITOR;
  EventBus::instance().publish(evt);
}

#include "safetymonitorservice.moc"
