#include "safetymonitorservice.h"

#include "vehiclecontrolservice.h"
#include "../core/systemstatemachine.h"
#include "../core/errorregistry.h"
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
  Q_DISABLE_COPY(SafetyWorker)
 public:
  explicit SafetyWorker(SafetyMonitorService* svc)
      : QObject(nullptr), m_svc(svc), m_timer(this) {
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
    checkControlStall();
    checkActuatorConsistency(); // [Poka-yoke]
    checkCircuitBreaker();
  }

 private:
  void checkControlStall() {
    const int64_t now = safetyNowMs();
    const int64_t lastTick = m_svc->m_lastControlTickMs.load(std::memory_order_acquire);
    if (lastTick == 0) return;

    const int64_t elapsed = now - lastTick;
    if (elapsed > 100) { // 控制环频率 100Hz (10ms)，超过 100ms 无响应则警告
      static int64_t s_lastLogMs = 0;
      if (now - s_lastLogMs >= 1000) {
        s_lastLogMs = now;
        qWarning().noquote() << "[Client][SafetyMonitorService] ★ 控制线程卡顿延迟 =" << elapsed
                             << "ms";
      }

      if (elapsed > 200) { // 超过 200ms 触发急停
        qCritical().noquote()
            << "[Client][SafetyMonitorService] ★★★ 控制线程疑似死锁或严重卡顿超过 200ms ★★★"
            << " elapsed=" << elapsed << "ms" << " ★ 强制触发紧急停车";
        ErrorRegistry::instance().reportFault(QStringLiteral("TEL-1004"), QStringLiteral("SafetyMonitor"));
        m_svc->triggerEmergencyStop(QStringLiteral("Control thread stalled for %1ms").arg(elapsed));
      }
    }
  }

  void checkActuatorConsistency() {
    // [Poka-yoke Guard] 控制闭环一致性检查
    // 如果操作员有明显的转向意图，但车辆反馈 steering_norm 始终为 0，说明控制协议失效或链路断开
    if (!m_svc->m_fsm || !m_svc->m_fsm->isDriveActive()) return;

    const int64_t now = safetyNowMs();
    
    // 获取最新的用户输入意图（从控制服务读取最近一次尝试发送的指令）
    double intentSteer = 0;
    if (m_svc->m_control) {
        intentSteer = std::abs(m_svc->m_control->lastCommand().steeringAngle);
    }

    // 获取车辆回显的实际状态
    double actualSteer = 0;
    if (m_svc->m_vehicleStatus) {
        actualSteer = std::abs(m_svc->m_vehicleStatus->steering());
    }

    // 防错逻辑：如果意图很大（>0.3）但反馈几乎为 0（<0.01）
    if (intentSteer > 0.3 && actualSteer < 0.01) {
        static int64_t s_lastMismatchStart = 0;
        if (s_lastMismatchStart == 0) s_lastMismatchStart = now;

        const int64_t mismatchDuration = now - s_lastMismatchStart;
        if (mismatchDuration > 1000) { // 持续 1s 不一致则触发告警/熔断
            qCritical().noquote() << "[Client][Safety][Poka-yoke] ★★★ 检测到控制失能(Actuator Inconsistency) ★★★"
                                 << "Intent(ABS)=" << intentSteer << " Actual(ABS)=" << actualSteer
                                 << " Duration=" << mismatchDuration << "ms"
                                 << " ★ 判定为协议版本不匹配或执行器故障";
            ErrorRegistry::instance().reportFault(QStringLiteral("VEH-4001"), QStringLiteral("SafetyMonitor"));
            m_svc->triggerEmergencyStop(QStringLiteral("Actuator mismatch: Steer intent vs feedback"));
            s_lastMismatchStart = 0;
        }
    } else {
        // 一致性恢复，重置计时器
        // s_lastMismatchStart = 0; // 静态变量在 checkCircuitBreaker 后重置
    }
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
        ErrorRegistry::instance().reportFault(QStringLiteral("NET-2001"), QStringLiteral("SafetyMonitor"));
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
        ErrorRegistry::instance().reportFault(QStringLiteral("VEH-3002"), QStringLiteral("SafetyMonitor"));
        m_svc->triggerEmergencyStop(QString("Heartbeat lost: %1 consecutive misses").arg(missed));
        m_svc->m_missedHeartbeats.store(0);
      } else if (missed >= m_svc->m_config.missedBeforeWarning) {
        qWarning().noquote() << msg;
        ErrorRegistry::instance().reportFault(QStringLiteral("NET-2003"), QStringLiteral("SafetyMonitor"));
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
      ErrorRegistry::instance().reportFault(QStringLiteral("TEL-1005"), QStringLiteral("SafetyMonitor"));
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
      ErrorRegistry::instance().reportFault(QStringLiteral("TEL-1006"), QStringLiteral("SafetyMonitor"));
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
    if (elapsed > 400) {
      static int64_t s_lastLogMs = 0;
      if (now - s_lastLogMs >= 500) {
        s_lastLogMs = now;
        qWarning().noquote() << "[Client][SafetyMonitorService] ★ UI 线程卡顿延迟 =" << elapsed
                             << "ms";
      }

      if (elapsed > 500) {
        qCritical().noquote()
            << "[Client][SafetyMonitorService] ★★★ UI 线程疑似死锁或严重卡顿超过 500ms ★★★"
            << " elapsed=" << elapsed << "ms" << " ★ 强制触发紧急停车(Decoupled Watchdog)";
        ErrorRegistry::instance().reportFault(QStringLiteral("TEL-1003"), QStringLiteral("SafetyMonitor"));
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
    : QObject(parent),
      m_config(),
      m_vehicleStatus(vs),
      m_fsm(fsm),
      m_control(nullptr),
      m_worker(nullptr),
      m_emergencyReason(),
      m_reasonMutex() {
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
  m_lastControlTickMs.store(now);
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

void SafetyMonitorService::clearEmergency() {
  qInfo().noquote() << "[Client][SafetyMonitorService] User requested EMERGENCY CLEAR/RECOVER. "
                    << "Current Status: emergencyActive=" << (m_emergencyActive.load() ? "YES" : "NO")
                    << " allSystemsGo=" << (m_allSystemsGo.load() ? "YES" : "NO");

  // ★ 核心修复：复位所有安全判定计数器和时间戳
  // 这样做是为了让下一次安全检查（20ms后）看到的是最新的、正常的起始数据
  int64_t now = safetyNowMs(); 
  m_missedHeartbeats.store(0);           // 重置心跳丢失计数
  m_latencyViolationCount.store(0);      // 重置延迟超限计数
  m_lastHeartbeatMs.store(now);          // 更新最后心跳时间为当前
  m_lastOperatorActivityMs.store(now);   // 更新操作员最后活动时间
  m_lastControlTickMs.store(now);        // 更新控制环最后 Tick 时间
  m_lastUiHeartbeatMs.store(now);        // 更新 UI 最后心跳时间
  m_deadmanActive.store(true);           // 恢复后默认标记操作员在线（或等待下一次 UI 触发）


  m_emergencyActive.store(false);
  m_allSystemsGo.store(true);
  {
    QMutexLocker lock(&m_reasonMutex);
    m_emergencyReason.clear();
  }

  // 通知 FSM 恢复到 READY 态
  if (m_fsm) {
    auto currentState = m_fsm->stateEnum();
    if (currentState == SystemStateMachine::SystemState::EMERGENCY) {
      if (m_fsm->fire(SystemStateMachine::Trigger::RECOVER)) {
          qInfo().noquote() << "[Client][SafetyMonitorService] FSM state transitioned to READY/DRIVE after recovery.";
      } else {
          qWarning().noquote() << "[Client][SafetyMonitorService] FSM REJECTED recovery trigger. Current State:" << m_fsm->currentState();
      }
    } else if (currentState == SystemStateMachine::SystemState::STOPPING) {
      qInfo().noquote() << "[Client][SafetyMonitorService] FSM is in STOPPING, triggering RESET to return to IDLE.";
      m_fsm->fire(SystemStateMachine::Trigger::RESET);
    } else {
      qInfo().noquote() << "[Client][SafetyMonitorService] FSM in state" << m_fsm->currentState() << ", bypassing RECOVER trigger.";
    }
  }

  // 通知控制服务解除锁定（单测不链接 VehicleControlService 全量实现）
#ifndef REMOTE_DRIVING_UNIT_TEST
  if (m_control) {
    m_control->clearEmergencyStop();
  }
#endif

  emit emergencyActiveChanged();
  emit allSystemsGoChanged(true);
  emit safetyStatusChanged(true);

  qInfo().noquote() << "[Client][SafetyMonitorService] Emergency cleared successfully. Systems back to operational mode.";
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
  emit operatorActivityReported();
}

void SafetyMonitorService::onControlTick() {
  m_lastControlTickMs.store(safetyNowMs());
  emit controlTickReported();
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
