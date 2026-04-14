#pragma once
#include "eventbus.h"

#include <QMutex>
#include <QObject>

#include <functional>
#include <map>
#include <utility>

class EventBus;

/**
 * 远程驾驶客户端系统状态机（《客户端架构设计》§3.2.2 完整实现）。
 *
 * 状态转换图：
 *   IDLE ──connect──> CONNECTING ──connected──> AUTHENTICATING
 *   AUTHENTICATING ──auth_success──> READY
 *   READY ──start_session──> PRE_FLIGHT ──preflight_ok──> DRIVING
 *   DRIVING ──network_degrade──> DEGRADED ──network_recover──> DRIVING
 *   PRE_FLIGHT ──emergency_stop──> READY（安全看门狗/异常时中止会话）
 *   DRIVING/DEGRADED ──emergency_stop──> EMERGENCY
 *   DRIVING/DEGRADED/EMERGENCY ──stop_session──> STOPPING ──> IDLE
 *   任意状态 ──reset──> IDLE
 *
 * 守卫条件（guard）用于防止非法转换；
 * 进入/退出动作（entry/exit action）在状态切换时自动执行。
 */
class SystemStateMachine : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString currentState READ currentState NOTIFY stateChanged)

 public:
  enum class SystemState {
    IDLE,
    CONNECTING,
    AUTHENTICATING,
    READY,
    PRE_FLIGHT,
    DRIVING,
    DEGRADED,
    EMERGENCY,
    STOPPING,
    ERROR,
  };
  Q_ENUM(SystemState)

  enum class Trigger {
    CONNECT,
    CONNECTED,
    AUTH_SUCCESS,
    AUTH_FAILURE,
    LOGOUT,
    START_SESSION,
    PREFLIGHT_OK,
    PREFLIGHT_FAIL,
    EMERGENCY_STOP,
    NETWORK_DEGRADE,
    NETWORK_RECOVER,
    STOP_SESSION,
    CONNECTION_LOST,
    TIMEOUT,
    RESET,
  };
  Q_ENUM(Trigger)

  explicit SystemStateMachine(EventBus* eventBus, QObject* parent = nullptr);

  Q_INVOKABLE virtual bool fire(Trigger trigger);
  Q_INVOKABLE bool fireByName(const QString& triggerName);

  SystemState stateEnum() const;
  QString currentState() const;

  // 状态检查便利方法
  bool isIdle() const { return stateEnum() == SystemState::IDLE; }
  bool isDriving() const { return stateEnum() == SystemState::DRIVING; }
  bool isDriveActive() const {
    auto s = stateEnum();
    return s == SystemState::DRIVING || s == SystemState::DEGRADED;
  }
  bool isEmergency() const { return stateEnum() == SystemState::EMERGENCY; }

  /**
   * 车端遥测/心跳是否参与 SafetyMonitor 的严格看门狗（心跳丢失→急停、死手等）。
   * READY/PRE_FLIGHT/IDLE 等为 false；进入 DRIVING/DEGRADED 后由车端 status（MQTT）刷新心跳。
   */
  bool vehicleTelemetryHeartbeatRequired() const;

 signals:
  void stateChanged(const QString& newState, const QString& oldState);
  void emergencyActivated(const QString& reason);
  void stateEntryAction(SystemState state);

 private:
  struct Transition {
    SystemState target = SystemState::IDLE;
    std::function<bool()> guard;
    std::function<void()> action;
  };

  void addTransition(SystemState from, Trigger trig, SystemState to,
                     std::function<bool()> guard = {}, std::function<void()> action = {});

  // ★★★ 新增：状态进入/退出/转换动作注册接口 ★★★
  // 状态进入动作：每次进入该状态时执行
  void registerEntryAction(SystemState state, std::function<void()> action);
  // 状态退出动作：每次离开该状态时执行
  void registerExitAction(SystemState state, std::function<void()> action);
  // 转换动作：特定触发器触发时执行（在 enter/exit 动作之后）
  void registerTransitionAction(Trigger trigger, std::function<void()> action);

  void setupTransitions();
  void onEnterState(SystemState state);
  void onExitState(SystemState state);

  static QString stateToString(SystemState s);
  static SystemState stringToState(const QString& s);
  static QString triggerToString(Trigger t);
  static Trigger stringToTrigger(const QString& name);

  void publishEmergencyEvent(const QString& reason);

  EventBus* m_eventBus = nullptr;
  std::map<std::pair<SystemState, Trigger>, Transition> m_transitions;
  std::map<SystemState, std::function<void()>> m_entryActions;
  std::map<SystemState, std::function<void()>> m_exitActions;
  std::map<Trigger, std::function<void()>> m_transitionActions;  // ★★★ 修复：转换动作映射 ★★★
  SystemState m_current = SystemState::IDLE;
  mutable QMutex m_mutex;
};
