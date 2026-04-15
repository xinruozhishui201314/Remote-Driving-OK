#include "systemstatemachine.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include <boost/sml.hpp>
#pragma GCC diagnostic pop

#include <mutex>
#include <type_traits>
#include <QDebug>
#include <QSet>

namespace sml = boost::sml;

// 事件定义
struct e_connect {};
struct e_connected {};
struct e_auth_success {};
struct e_auth_failure {};
struct e_logout {};
struct e_start_session {};
struct e_preflight_ok {};
struct e_preflight_fail {};
struct e_emergency_stop {};
struct e_network_degrade {};
struct e_network_recover {};
struct e_stop_session {};
struct e_connection_lost {};
struct e_timeout {};
struct e_reset {};

// 状态定义 (Tags)
struct s_idle {};
struct s_connecting {};
struct s_authenticating {};
struct s_ready {};
struct s_pre_flight {};
struct s_driving {};
struct s_degraded {};
struct s_emergency {};
struct s_stopping {};
struct s_error {};

// 子状态机：驾驶会话（Hierarchical SML）
struct driving_session_def {
  auto operator()() const noexcept {
    using namespace sml;
    return make_transition_table(
        *state<s_driving> + event<e_network_degrade> = state<s_degraded>,
        state<s_degraded> + event<e_network_recover> = state<s_driving>);
  }
};

// 状态机定义移出 Impl 以提高兼容性
struct state_machine_def {
  SystemStateMachine* q;

  auto operator()() const noexcept {
    using namespace sml;

    return make_transition_table(
        // IDLE
        *state<s_idle> + event<e_connect> = state<s_connecting>,
        state<s_idle> + event<e_auth_success> = state<s_ready>,

        // CONNECTING
        state<s_connecting> + event<e_connected> = state<s_authenticating>,
        state<s_connecting> + event<e_timeout> = state<s_idle>,
        state<s_connecting> + event<e_reset> = state<s_idle>,

        // AUTHENTICATING
        state<s_authenticating> + event<e_auth_success> = state<s_ready>,
        state<s_authenticating> + event<e_auth_failure> = state<s_idle>,
        state<s_authenticating> + event<e_timeout> = state<s_idle>,
        state<s_authenticating> + event<e_reset> = state<s_idle>,

        // READY
        state<s_ready> + event<e_auth_success> = state<s_ready>,
        state<s_ready> + event<e_logout> = state<s_idle>,
        state<s_ready> + event<e_start_session> = state<s_pre_flight>,
        state<s_ready> + event<e_connection_lost> = state<s_idle>,
        state<s_ready> + event<e_reset> = state<s_idle>,

        // PRE_FLIGHT
        state<s_pre_flight> + event<e_preflight_ok> = state<driving_session_def>,
        state<s_pre_flight> + event<e_preflight_fail> = state<s_ready>,
        state<s_pre_flight> + event<e_stop_session> = state<s_ready>,
        state<s_pre_flight> + event<e_emergency_stop> = state<s_ready>,
        state<s_pre_flight> + event<e_reset> = state<s_idle>,

        // DRIVING_SESSION (Hierarchical)
        state<driving_session_def> + event<e_stop_session> = state<s_stopping>,
        state<driving_session_def> + event<e_logout> = state<s_stopping>,
        state<driving_session_def> + event<e_emergency_stop> = state<s_emergency>,
        state<driving_session_def> + event<e_connection_lost> = state<s_emergency>,
        state<driving_session_def> + event<e_reset> = state<s_idle>,

        // EMERGENCY
        state<s_emergency> + event<e_stop_session> = state<s_stopping>,
        state<s_emergency> + event<e_reset> = state<s_idle>,
        state<s_emergency> + event<e_logout> = state<s_idle>,

        // STOPPING
        state<s_stopping> + event<e_stop_session> = state<s_idle>,
        state<s_stopping> + event<e_reset> = state<s_idle>,

        // ERROR
        state<s_error> + event<e_reset> = state<s_idle>);
  }
};

struct SystemStateMachine::Impl {
  SystemStateMachine* q;
  sml::sm<state_machine_def> sm;

  explicit Impl(SystemStateMachine* q_ptr) : q(q_ptr), sm{state_machine_def{q_ptr}} {}
};

SystemStateMachine::SystemStateMachine(EventBus* eventBus, QObject* parent)
    : QObject(parent), m_eventBus(eventBus), m_impl(std::make_unique<Impl>(this)) {}

SystemStateMachine::~SystemStateMachine() = default;

SystemStateMachine::SystemState SystemStateMachine::stateEnum() const {
  using S = SystemState;
  SystemState current = S::IDLE;
  m_impl->sm.visit_current_states([&current](auto state) {
    using T = typename decltype(state)::type;
    if constexpr (std::is_same_v<T, s_idle>) current = S::IDLE;
    else if constexpr (std::is_same_v<T, s_connecting>) current = S::CONNECTING;
    else if constexpr (std::is_same_v<T, s_authenticating>) current = S::AUTHENTICATING;
    else if constexpr (std::is_same_v<T, s_ready>) current = S::READY;
    else if constexpr (std::is_same_v<T, s_pre_flight>) current = S::PRE_FLIGHT;
    else if constexpr (std::is_same_v<T, s_driving>) current = S::DRIVING;
    else if constexpr (std::is_same_v<T, s_degraded>) current = S::DEGRADED;
    else if constexpr (std::is_same_v<T, s_emergency>) current = S::EMERGENCY;
    else if constexpr (std::is_same_v<T, s_stopping>) current = S::STOPPING;
    else if constexpr (std::is_same_v<T, s_error>) current = S::ERROR;
  });
  return current;
}

QString SystemStateMachine::currentState() const {
  return SystemStateMachine::stateToString(stateEnum());
}

bool SystemStateMachine::fire(Trigger trigger) {
  SystemState oldS = stateEnum();
  bool handled = false;
  
  switch (trigger) {
    case Trigger::CONNECT: handled = m_impl->sm.process_event(e_connect{}); break;
    case Trigger::CONNECTED: handled = m_impl->sm.process_event(e_connected{}); break;
    case Trigger::AUTH_SUCCESS: handled = m_impl->sm.process_event(e_auth_success{}); break;
    case Trigger::AUTH_FAILURE: handled = m_impl->sm.process_event(e_auth_failure{}); break;
    case Trigger::LOGOUT: handled = m_impl->sm.process_event(e_logout{}); break;
    case Trigger::START_SESSION: handled = m_impl->sm.process_event(e_start_session{}); break;
    case Trigger::PREFLIGHT_OK: handled = m_impl->sm.process_event(e_preflight_ok{}); break;
    case Trigger::PREFLIGHT_FAIL: handled = m_impl->sm.process_event(e_preflight_fail{}); break;
    case Trigger::EMERGENCY_STOP: handled = m_impl->sm.process_event(e_emergency_stop{}); break;
    case Trigger::NETWORK_DEGRADE: handled = m_impl->sm.process_event(e_network_degrade{}); break;
    case Trigger::NETWORK_RECOVER: handled = m_impl->sm.process_event(e_network_recover{}); break;
    case Trigger::STOP_SESSION: handled = m_impl->sm.process_event(e_stop_session{}); break;
    case Trigger::CONNECTION_LOST: handled = m_impl->sm.process_event(e_connection_lost{}); break;
    case Trigger::TIMEOUT: handled = m_impl->sm.process_event(e_timeout{}); break;
    case Trigger::RESET: handled = m_impl->sm.process_event(e_reset{}); break;
  }

  if (handled) {
    SystemState newS = stateEnum();
    if (oldS != newS) {
      qInfo().noquote() << "[Client][FSM] transition" << SystemStateMachine::stateToString(oldS) << "->" << SystemStateMachine::stateToString(newS)
                        << "via" << SystemStateMachine::triggerToString(trigger);
      emit stateChanged(SystemStateMachine::stateToString(newS), SystemStateMachine::stateToString(oldS));
    }
  } else {
    qWarning().noquote() << "[Client][FSM] invalid transition state=" << SystemStateMachine::stateToString(oldS)
                         << " trigger=" << SystemStateMachine::triggerToString(trigger);
  }
  return handled;
}

bool SystemStateMachine::fireByName(const QString& triggerName) {
  Trigger t = stringToTrigger(triggerName.trimmed().toUpper());
  if (t == Trigger::RESET && triggerName.trimmed().toUpper() != QStringLiteral("RESET")) {
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

bool SystemStateMachine::vehicleTelemetryHeartbeatRequired() const {
  auto s = stateEnum();
  return (s == SystemState::DRIVING || s == SystemState::DEGRADED);
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
    case SystemState::IDLE: return QStringLiteral("IDLE");
    case SystemState::CONNECTING: return QStringLiteral("CONNECTING");
    case SystemState::AUTHENTICATING: return QStringLiteral("AUTHENTICATING");
    case SystemState::READY: return QStringLiteral("READY");
    case SystemState::PRE_FLIGHT: return QStringLiteral("PRE_FLIGHT");
    case SystemState::DRIVING: return QStringLiteral("DRIVING");
    case SystemState::DEGRADED: return QStringLiteral("DEGRADED");
    case SystemState::EMERGENCY: return QStringLiteral("EMERGENCY");
    case SystemState::STOPPING: return QStringLiteral("STOPPING");
    case SystemState::ERROR: return QStringLiteral("ERROR");
  }
  return QStringLiteral("UNKNOWN");
}

QString SystemStateMachine::triggerToString(Trigger t) {
  switch (t) {
    case Trigger::CONNECT: return QStringLiteral("CONNECT");
    case Trigger::CONNECTED: return QStringLiteral("CONNECTED");
    case Trigger::AUTH_SUCCESS: return QStringLiteral("AUTH_SUCCESS");
    case Trigger::AUTH_FAILURE: return QStringLiteral("AUTH_FAILURE");
    case Trigger::LOGOUT: return QStringLiteral("LOGOUT");
    case Trigger::START_SESSION: return QStringLiteral("START_SESSION");
    case Trigger::PREFLIGHT_OK: return QStringLiteral("PREFLIGHT_OK");
    case Trigger::PREFLIGHT_FAIL: return QStringLiteral("PREFLIGHT_FAIL");
    case Trigger::EMERGENCY_STOP: return QStringLiteral("EMERGENCY_STOP");
    case Trigger::NETWORK_DEGRADE: return QStringLiteral("NETWORK_DEGRADE");
    case Trigger::NETWORK_RECOVER: return QStringLiteral("NETWORK_RECOVER");
    case Trigger::STOP_SESSION: return QStringLiteral("STOP_SESSION");
    case Trigger::CONNECTION_LOST: return QStringLiteral("CONNECTION_LOST");
    case Trigger::TIMEOUT: return QStringLiteral("TIMEOUT");
    case Trigger::RESET: return QStringLiteral("RESET");
  }
  return QStringLiteral("?");
}

SystemStateMachine::Trigger SystemStateMachine::stringToTrigger(const QString& name) {
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
  return Trigger::RESET;
}
