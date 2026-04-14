#include "systemstatemachine.h"

#include "eventbus.h"

#include <QDebug>

SystemStateMachine::SystemStateMachine(EventBus* eventBus, QObject* parent)
    : QObject(parent), m_eventBus(eventBus) {
  setupTransitions();
}

SystemStateMachine::SystemState SystemStateMachine::stateEnum() const {
  QMutexLocker lock(&m_mutex);
  return m_current;
}

QString SystemStateMachine::currentState() const {
  QMutexLocker lock(&m_mutex);
  return stateToString(m_current);
}

bool SystemStateMachine::vehicleTelemetryHeartbeatRequired() const {
  QMutexLocker lock(&m_mutex);
  switch (m_current) {
    // PRE_FLIGHT：仅等待车端确认远驾/检查项，尚未正式控车；若此处要求 status 心跳，
    // 会在「仅视频通、MQTT 断」时误急停且 EMERGENCY_STOP 无法从 PRE_FLIGHT 转移（历史缺陷）。
    case SystemState::DRIVING:
    case SystemState::DEGRADED:
      return true;
    default:
      return false;
  }
}

void SystemStateMachine::addTransition(SystemState from, Trigger trig, SystemState to,
                                       std::function<bool()> guard, std::function<void()> action) {
  m_transitions[{from, trig}] = Transition{to, std::move(guard), std::move(action)};
}

// ★★★ 状态动作注册方法实现 ★★★
void SystemStateMachine::registerEntryAction(SystemState state, std::function<void()> action) {
  QMutexLocker lock(&m_mutex);
  m_entryActions[state] = std::move(action);
}

void SystemStateMachine::registerExitAction(SystemState state, std::function<void()> action) {
  QMutexLocker lock(&m_mutex);
  m_exitActions[state] = std::move(action);
}

void SystemStateMachine::registerTransitionAction(Trigger trigger, std::function<void()> action) {
  QMutexLocker lock(&m_mutex);
  m_transitionActions[trigger] = std::move(action);
}

void SystemStateMachine::setupTransitions() {
  using S = SystemState;
  using T = Trigger;

  // ── IDLE ────────────────────────────────────────────────────────────────
  addTransition(S::IDLE, T::CONNECT, S::CONNECTING);
  addTransition(S::IDLE, T::AUTH_SUCCESS, S::READY);  // 直接登录（跳过连接状态的便利路径）

  // ── CONNECTING ──────────────────────────────────────────────────────────
  addTransition(S::CONNECTING, T::CONNECTED, S::AUTHENTICATING);
  addTransition(S::CONNECTING, T::TIMEOUT, S::IDLE);
  addTransition(S::CONNECTING, T::RESET, S::IDLE);

  // ── AUTHENTICATING ──────────────────────────────────────────────────────
  addTransition(S::AUTHENTICATING, T::AUTH_SUCCESS, S::READY);
  addTransition(S::AUTHENTICATING, T::AUTH_FAILURE, S::IDLE);
  addTransition(S::AUTHENTICATING, T::TIMEOUT, S::IDLE);
  addTransition(S::AUTHENTICATING, T::RESET, S::IDLE);

  // ── READY ────────────────────────────────────────────────────────────────
  addTransition(S::READY, T::AUTH_SUCCESS, S::READY);  // 刷新登录
  addTransition(S::READY, T::LOGOUT, S::IDLE);
  addTransition(S::READY, T::START_SESSION, S::PRE_FLIGHT, nullptr, [this]() {
    qInfo().noquote() << "[Client][FSM] entering PRE_FLIGHT - starting system checks";
    emit stateEntryAction(SystemState::PRE_FLIGHT);
  });
  addTransition(S::READY, T::CONNECTION_LOST, S::IDLE);
  addTransition(S::READY, T::RESET, S::IDLE);

  // ── PRE_FLIGHT ────────────────────────────────────────────────────────────
  addTransition(S::PRE_FLIGHT, T::PREFLIGHT_OK, S::DRIVING, nullptr, [this]() {
    qInfo().noquote() << "[Client][FSM] PRE_FLIGHT checks passed - entering DRIVING";
    emit stateEntryAction(SystemState::DRIVING);
  });
  addTransition(S::PRE_FLIGHT, T::PREFLIGHT_FAIL, S::READY);
  addTransition(S::PRE_FLIGHT, T::STOP_SESSION, S::READY);
  addTransition(S::PRE_FLIGHT, T::EMERGENCY_STOP, S::READY, nullptr, [this]() {
    qCritical().noquote()
        << "[Client][FSM] EMERGENCY_STOP during PRE_FLIGHT -> READY (abort session / safety)";
    emit emergencyActivated(QStringLiteral("EMERGENCY_STOP during PRE_FLIGHT (session aborted)"));
    publishEmergencyEvent("EMERGENCY_STOP_PRE_FLIGHT");
  });
  addTransition(S::PRE_FLIGHT, T::RESET, S::IDLE);

  // ── DRIVING ──────────────────────────────────────────────────────────────
  addTransition(S::DRIVING, T::STOP_SESSION, S::STOPPING, nullptr,
                [this]() { qInfo().noquote() << "[Client][FSM] DRIVING -> STOPPING"; });
  addTransition(S::DRIVING, T::LOGOUT, S::STOPPING);
  addTransition(S::DRIVING, T::NETWORK_DEGRADE, S::DEGRADED, nullptr, [this]() {
    qWarning().noquote() << "[Client][FSM] network degraded - entering DEGRADED mode";
  });
  addTransition(S::DRIVING, T::EMERGENCY_STOP, S::EMERGENCY, nullptr, [this]() {
    emit emergencyActivated(QStringLiteral("EMERGENCY_STOP triggered in DRIVING state"));
    publishEmergencyEvent("EMERGENCY_STOP");
  });
  addTransition(S::DRIVING, T::CONNECTION_LOST, S::EMERGENCY, nullptr, [this]() {
    emit emergencyActivated(QStringLiteral("Connection lost during DRIVING"));
    publishEmergencyEvent("CONNECTION_LOST");
  });
  addTransition(S::DRIVING, T::RESET, S::IDLE);

  // ── DEGRADED ─────────────────────────────────────────────────────────────
  addTransition(S::DEGRADED, T::NETWORK_RECOVER, S::DRIVING, nullptr, [this]() {
    qInfo().noquote() << "[Client][FSM] network recovered - resuming DRIVING";
  });
  addTransition(S::DEGRADED, T::EMERGENCY_STOP, S::EMERGENCY, nullptr, [this]() {
    emit emergencyActivated(QStringLiteral("EMERGENCY_STOP triggered in DEGRADED state"));
    publishEmergencyEvent("EMERGENCY_STOP");
  });
  addTransition(S::DEGRADED, T::STOP_SESSION, S::STOPPING);
  addTransition(S::DEGRADED, T::LOGOUT, S::STOPPING);
  addTransition(S::DEGRADED, T::CONNECTION_LOST, S::EMERGENCY, nullptr, [this]() {
    emit emergencyActivated(QStringLiteral("Connection lost during DEGRADED"));
    publishEmergencyEvent("CONNECTION_LOST");
  });
  addTransition(S::DEGRADED, T::RESET, S::IDLE);

  // ── EMERGENCY ────────────────────────────────────────────────────────────
  addTransition(S::EMERGENCY, T::STOP_SESSION, S::STOPPING);
  addTransition(S::EMERGENCY, T::RESET, S::IDLE);
  addTransition(S::EMERGENCY, T::LOGOUT, S::IDLE);

  // ── STOPPING ─────────────────────────────────────────────────────────────
  addTransition(S::STOPPING, T::STOP_SESSION, S::IDLE);  // cleanup complete
  addTransition(S::STOPPING, T::RESET, S::IDLE);

  // ── ERROR ─────────────────────────────────────────────────────────────────
  addTransition(S::ERROR, T::RESET, S::IDLE);
}

bool SystemStateMachine::fire(Trigger trigger) {
  Transition tr;
  SystemState oldS{};
  SystemState newS{};
  {
    QMutexLocker lock(&m_mutex);
    const auto key = std::make_pair(m_current, trigger);
    auto it = m_transitions.find(key);
    if (it == m_transitions.end()) {
      qWarning().noquote() << "[Client][FSM] invalid transition state=" << stateToString(m_current)
                           << " trigger=" << triggerToString(trigger);
      return false;
    }
    tr = it->second;
    if (tr.guard && !tr.guard()) {
      qDebug().noquote() << "[Client][FSM] guard rejected trigger=" << triggerToString(trigger);
      return false;
    }
    oldS = m_current;
    newS = tr.target;

    // 执行退出动作（持锁外）先记录，释放锁后执行
    m_current = newS;
  }

  // 退出动作
  onExitState(oldS);

  // 转换动作：先执行 Transition 中定义的 action，再执行 m_transitionActions 中注册的同名触发器动作
  if (tr.action) {
    tr.action();
  }

  // 执行已注册的转换动作（如果有）
  {
    QMutexLocker lock(&m_mutex);
    auto it = m_transitionActions.find(trigger);
    if (it != m_transitionActions.end() && it->second) {
      it->second();
    }
  }

  // 进入动作
  onEnterState(newS);

  const QString oldName = stateToString(oldS);
  const QString newName = stateToString(newS);
  qInfo().noquote() << "[Client][FSM] transition" << oldName << "->" << newName << "via"
                    << triggerToString(trigger);
  emit stateChanged(newName, oldName);
  return true;
}

bool SystemStateMachine::fireByName(const QString& triggerName) {
  Trigger t = stringToTrigger(triggerName.trimmed().toUpper());
  if (t == Trigger::RESET && triggerName.trimmed().toUpper() != QStringLiteral("RESET")) {
    // stringToTrigger returns RESET as sentinel; check if it was actually RESET
    // or an unknown trigger that defaulted to RESET
    static const QSet<QString> knownTriggers = {
        "CONNECT",         "CONNECTED",    "AUTH_SUCCESS",    "AUTH_FAILURE",   "LOGOUT",
        "START_SESSION",   "PREFLIGHT_OK", "PREFLIGHT_FAIL",  "EMERGENCY_STOP", "NETWORK_DEGRADE",
        "NETWORK_RECOVER", "STOP_SESSION", "CONNECTION_LOST", "TIMEOUT",        "RESET"};
    if (!knownTriggers.contains(triggerName.trimmed().toUpper())) {
      qWarning().noquote() << "[Client][FSM] unknown trigger name:" << triggerName;
      return false;
    }
  }
  return fire(t);
}

void SystemStateMachine::onEnterState(SystemState state) {
  auto it = m_entryActions.find(state);
  if (it != m_entryActions.end() && it->second) {
    it->second();
  }
}

void SystemStateMachine::onExitState(SystemState state) {
  auto it = m_exitActions.find(state);
  if (it != m_exitActions.end() && it->second) {
    it->second();
  }
}

void SystemStateMachine::publishEmergencyEvent(const QString& reason) {
  if (!m_eventBus)
    return;
  EmergencyStopEvent evt;
  evt.reason = reason;
  evt.source = EmergencyStopEvent::Source::SAFETY_MONITOR;
  m_eventBus->publish(evt);
}

QString SystemStateMachine::stateToString(SystemState s) {
  switch (s) {
    case SystemState::IDLE:
      return QStringLiteral("IDLE");
    case SystemState::CONNECTING:
      return QStringLiteral("CONNECTING");
    case SystemState::AUTHENTICATING:
      return QStringLiteral("AUTHENTICATING");
    case SystemState::READY:
      return QStringLiteral("READY");
    case SystemState::PRE_FLIGHT:
      return QStringLiteral("PRE_FLIGHT");
    case SystemState::DRIVING:
      return QStringLiteral("DRIVING");
    case SystemState::DEGRADED:
      return QStringLiteral("DEGRADED");
    case SystemState::EMERGENCY:
      return QStringLiteral("EMERGENCY");
    case SystemState::STOPPING:
      return QStringLiteral("STOPPING");
    case SystemState::ERROR:
      return QStringLiteral("ERROR");
  }
  return QStringLiteral("UNKNOWN");
}

QString SystemStateMachine::triggerToString(Trigger t) {
  switch (t) {
    case Trigger::CONNECT:
      return QStringLiteral("CONNECT");
    case Trigger::CONNECTED:
      return QStringLiteral("CONNECTED");
    case Trigger::AUTH_SUCCESS:
      return QStringLiteral("AUTH_SUCCESS");
    case Trigger::AUTH_FAILURE:
      return QStringLiteral("AUTH_FAILURE");
    case Trigger::LOGOUT:
      return QStringLiteral("LOGOUT");
    case Trigger::START_SESSION:
      return QStringLiteral("START_SESSION");
    case Trigger::PREFLIGHT_OK:
      return QStringLiteral("PREFLIGHT_OK");
    case Trigger::PREFLIGHT_FAIL:
      return QStringLiteral("PREFLIGHT_FAIL");
    case Trigger::EMERGENCY_STOP:
      return QStringLiteral("EMERGENCY_STOP");
    case Trigger::NETWORK_DEGRADE:
      return QStringLiteral("NETWORK_DEGRADE");
    case Trigger::NETWORK_RECOVER:
      return QStringLiteral("NETWORK_RECOVER");
    case Trigger::STOP_SESSION:
      return QStringLiteral("STOP_SESSION");
    case Trigger::CONNECTION_LOST:
      return QStringLiteral("CONNECTION_LOST");
    case Trigger::TIMEOUT:
      return QStringLiteral("TIMEOUT");
    case Trigger::RESET:
      return QStringLiteral("RESET");
  }
  return QStringLiteral("?");
}

SystemStateMachine::Trigger SystemStateMachine::stringToTrigger(const QString& name) {
  // Future-use: maps trigger name string to Trigger enum.
  // Currently used by fireByName() for QML-friendly trigger firing.
  static const QMap<QString, Trigger> kMap{
      {"CONNECT", Trigger::CONNECT},
      {"CONNECTED", Trigger::CONNECTED},
      {"AUTH_SUCCESS", Trigger::AUTH_SUCCESS},
      {"AUTH_FAILURE", Trigger::AUTH_FAILURE},
      {"LOGOUT", Trigger::LOGOUT},
      {"START_SESSION", Trigger::START_SESSION},
      {"PREFLIGHT_OK", Trigger::PREFLIGHT_OK},
      {"PREFLIGHT_FAIL", Trigger::PREFLIGHT_FAIL},
      {"EMERGENCY_STOP", Trigger::EMERGENCY_STOP},
      {"NETWORK_DEGRADE", Trigger::NETWORK_DEGRADE},
      {"NETWORK_RECOVER", Trigger::NETWORK_RECOVER},
      {"STOP_SESSION", Trigger::STOP_SESSION},
      {"CONNECTION_LOST", Trigger::CONNECTION_LOST},
      {"TIMEOUT", Trigger::TIMEOUT},
      {"RESET", Trigger::RESET},
  };
  auto it = kMap.constFind(name.toUpper());
  if (it != kMap.constEnd()) {
    return it.value();
  }
  return Trigger::RESET;  // sentinel value for unknown triggers
}

SystemStateMachine::SystemState SystemStateMachine::stringToState(const QString& name) {
  // Future-use: maps state name string to SystemState enum.
  // Reserved for future QML-friendly state inspection/manipulation.
  static const QMap<QString, SystemState> kMap{
      {"IDLE", SystemState::IDLE},
      {"CONNECTING", SystemState::CONNECTING},
      {"AUTHENTICATING", SystemState::AUTHENTICATING},
      {"READY", SystemState::READY},
      {"PRE_FLIGHT", SystemState::PRE_FLIGHT},
      {"DRIVING", SystemState::DRIVING},
      {"DEGRADED", SystemState::DEGRADED},
      {"EMERGENCY", SystemState::EMERGENCY},
      {"STOPPING", SystemState::STOPPING},
      {"ERROR", SystemState::ERROR},
  };
  auto it = kMap.constFind(name.toUpper());
  if (it != kMap.constEnd()) {
    return it.value();
  }
  return SystemState::IDLE;  // sentinel value for unknown states
}
