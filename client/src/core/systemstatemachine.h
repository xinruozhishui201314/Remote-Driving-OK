#pragma once
#include "eventbus.h"

#include <QMutex>
#include <QObject>

#include <functional>
#include <memory>

class EventBus;

/**
 * 远程驾驶客户端系统状态机（2025/2026 规范重构版）。
 * 采用 Boost.SML 声明式层次化状态机驱动。
 */
class SystemStateMachine : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(SystemStateMachine)
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
    RECOVER,
    NETWORK_DEGRADE,
    NETWORK_RECOVER,
    STOP_SESSION,
    CONNECTION_LOST,
    TIMEOUT,
    RESET,
  };
  Q_ENUM(Trigger)

  explicit SystemStateMachine(EventBus* eventBus, QObject* parent = nullptr);
  ~SystemStateMachine() override;

  Q_INVOKABLE virtual bool fire(Trigger trigger);
  Q_INVOKABLE bool fireByName(const QString& triggerName);

  SystemState stateEnum() const;
  QString currentState() const;

  bool isIdle() const { return stateEnum() == SystemState::IDLE; }
  bool isDriving() const { return stateEnum() == SystemState::DRIVING; }
  bool isDriveActive() const {
    auto s = stateEnum();
    return s == SystemState::DRIVING || s == SystemState::DEGRADED;
  }
  bool isEmergency() const { return stateEnum() == SystemState::EMERGENCY; }

  bool vehicleTelemetryHeartbeatRequired() const;

 signals:
  void stateChanged(const QString& newState, const QString& oldState);
  void emergencyActivated(const QString& reason);
  void stateEntryAction(SystemState state);

 private:
  EventBus* m_eventBus = nullptr;
  struct Impl;
  std::unique_ptr<Impl> m_impl;

  static QString stateToString(SystemState s);
  static QString triggerToString(Trigger t);
  static Trigger stringToTrigger(const QString& name);

  void publishEmergencyEvent(const QString& reason);
};
