#include "core/eventbus.h"
#include "core/systemstatemachine.h"

#include <QtTest/QtTest>

class TestSystemStateMachine : public QObject {
  Q_OBJECT
 private slots:
  void transitions_idleToReadyToDriving();
  void invalidTransition_emergencyFromIdle();
  void vehicleTelemetryHeartbeatRequired_onlyInSessionStates();
  void preflight_emergency_stop_returns_to_ready();
};

void TestSystemStateMachine::transitions_idleToReadyToDriving() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  QVERIFY(fsm.fire(SystemStateMachine::Trigger::AUTH_SUCCESS));
  QCOMPARE(fsm.currentState(), QStringLiteral("READY"));
  QVERIFY(fsm.fire(SystemStateMachine::Trigger::START_SESSION));
  QCOMPARE(fsm.currentState(), QStringLiteral("PRE_FLIGHT"));
  QVERIFY(fsm.fire(SystemStateMachine::Trigger::PREFLIGHT_OK));
  QCOMPARE(fsm.currentState(), QStringLiteral("DRIVING"));
  QVERIFY(fsm.fire(SystemStateMachine::Trigger::STOP_SESSION));
  QCOMPARE(fsm.currentState(), QStringLiteral("STOPPING"));
  QVERIFY(fsm.fire(SystemStateMachine::Trigger::STOP_SESSION));
  QCOMPARE(fsm.currentState(), QStringLiteral("IDLE"));
}

void TestSystemStateMachine::invalidTransition_emergencyFromIdle() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  QVERIFY(!fsm.fire(SystemStateMachine::Trigger::EMERGENCY_STOP));
}

void TestSystemStateMachine::vehicleTelemetryHeartbeatRequired_onlyInSessionStates() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  QVERIFY(!fsm.vehicleTelemetryHeartbeatRequired());
  QVERIFY(fsm.fire(SystemStateMachine::Trigger::AUTH_SUCCESS));
  QVERIFY(!fsm.vehicleTelemetryHeartbeatRequired());
  QVERIFY(fsm.fire(SystemStateMachine::Trigger::START_SESSION));
  // PRE_FLIGHT：等待车端确认，不要求 status 心跳（避免仅视频通/MQTT 断时误急停）
  QVERIFY(!fsm.vehicleTelemetryHeartbeatRequired());
  QVERIFY(fsm.fire(SystemStateMachine::Trigger::PREFLIGHT_OK));
  QVERIFY(fsm.vehicleTelemetryHeartbeatRequired());
}

void TestSystemStateMachine::preflight_emergency_stop_returns_to_ready() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  QVERIFY(fsm.fire(SystemStateMachine::Trigger::AUTH_SUCCESS));
  QVERIFY(fsm.fire(SystemStateMachine::Trigger::START_SESSION));
  QCOMPARE(fsm.currentState(), QStringLiteral("PRE_FLIGHT"));
  QVERIFY(fsm.fire(SystemStateMachine::Trigger::EMERGENCY_STOP));
  QCOMPARE(fsm.currentState(), QStringLiteral("READY"));
}

QTEST_MAIN(TestSystemStateMachine)
#include "test_systemstatemachine.moc"
