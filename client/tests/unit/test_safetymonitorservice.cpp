#include "core/eventbus.h"
#include "core/systemstatemachine.h"
#include "services/safetymonitorservice.h"

#include <QSignalSpy>
#include <QtTest/QtTest>

class TestSafetyMonitorService : public QObject {
  Q_OBJECT

 private slots:
  void cleanup() { SafetyMonitorService::clearUnitTestClockForTesting(); }

  void latency_consecutive_violations_trigger_emergency() {
    SafetyMonitorService::setUnitTestNowMsForTesting(0);

    SafetyMonitorService svc(nullptr, nullptr);
    SafetyMonitorService::Config cfg;
    cfg.maxOneWayLatencyMs = 100.0;
    cfg.warningLatencyMs = 50.0;
    cfg.emergencyTriggerCount = 3;
    svc.setConfig(cfg);
    QVERIFY(svc.initialize());
    svc.stop();

    QSignalSpy spyEmergency(&svc, &SafetyMonitorService::emergencyStopTriggered);

    svc.updateLatency(200.0, 0.0);
    svc.runSafetyChecks();
    svc.runSafetyChecks();
    QCOMPARE(spyEmergency.count(), 0);
    svc.runSafetyChecks();
    QCOMPARE(spyEmergency.count(), 1);
  }

  void heartbeat_missed_triggers_emergency_after_threshold() {
    SafetyMonitorService::setUnitTestNowMsForTesting(0);

    EventBus bus;
    SystemStateMachine fsm(&bus);
    QVERIFY(fsm.fire(SystemStateMachine::Trigger::AUTH_SUCCESS));
    QVERIFY(fsm.fire(SystemStateMachine::Trigger::START_SESSION));
    QVERIFY(fsm.fire(SystemStateMachine::Trigger::PREFLIGHT_OK));
    QCOMPARE(fsm.currentState(), QStringLiteral("DRIVING"));

    SafetyMonitorService svc(nullptr, &fsm);
    SafetyMonitorService::Config cfg;
    cfg.heartbeatTimeoutMs = 100;
    cfg.missedBeforeWarning = 1;
    cfg.missedBeforeEmergency = 2;
    svc.setConfig(cfg);
    QVERIFY(svc.initialize());
    svc.onHeartbeatReceived();
    svc.stop();

    QSignalSpy spyEmergency(&svc, &SafetyMonitorService::emergencyStopTriggered);

    SafetyMonitorService::setUnitTestNowMsForTesting(300);
    svc.runSafetyChecks();
    QVERIFY(spyEmergency.isEmpty());
    svc.runSafetyChecks();
    QCOMPARE(spyEmergency.count(), 1);
  }

  void heartbeat_ready_state_does_not_trigger_emergency() {
    SafetyMonitorService::setUnitTestNowMsForTesting(0);

    EventBus bus;
    SystemStateMachine fsm(&bus);
    QVERIFY(fsm.fire(SystemStateMachine::Trigger::AUTH_SUCCESS));
    QCOMPARE(fsm.currentState(), QStringLiteral("READY"));

    SafetyMonitorService svc(nullptr, &fsm);
    SafetyMonitorService::Config cfg;
    cfg.heartbeatTimeoutMs = 50;
    cfg.missedBeforeWarning = 1;
    cfg.missedBeforeEmergency = 2;
    svc.setConfig(cfg);
    QVERIFY(svc.initialize());
    svc.onHeartbeatReceived();
    svc.stop();

    QSignalSpy spyEmergency(&svc, &SafetyMonitorService::emergencyStopTriggered);
    SafetyMonitorService::setUnitTestNowMsForTesting(10'000);
    for (int i = 0; i < 20; ++i)
      svc.runSafetyChecks();
    QCOMPARE(spyEmergency.count(), 0);
  }

  void deadman_timeout_triggers_emergency() {
    SafetyMonitorService::setUnitTestNowMsForTesting(0);

    EventBus bus;
    SystemStateMachine fsm(&bus);
    QVERIFY(fsm.fire(SystemStateMachine::Trigger::AUTH_SUCCESS));
    QVERIFY(fsm.fire(SystemStateMachine::Trigger::START_SESSION));
    QVERIFY(fsm.fire(SystemStateMachine::Trigger::PREFLIGHT_OK));
    QCOMPARE(fsm.currentState(), QStringLiteral("DRIVING"));

    SafetyMonitorService svc(nullptr, &fsm);
    SafetyMonitorService::Config cfg;
    cfg.deadmanTimeoutMs = 80;
    svc.setConfig(cfg);
    QVERIFY(svc.initialize());
    svc.onOperatorActivity();
    svc.stop();

    QSignalSpy spyEmergency(&svc, &SafetyMonitorService::emergencyStopTriggered);

    SafetyMonitorService::setUnitTestNowMsForTesting(200);
    svc.runSafetyChecks();
    QCOMPARE(spyEmergency.count(), 1);
  }

  void operator_inactivity_requests_speed_limit() {
    SafetyMonitorService::setUnitTestNowMsForTesting(0);

    EventBus bus;
    SystemStateMachine fsm(&bus);
    QVERIFY(fsm.fire(SystemStateMachine::Trigger::AUTH_SUCCESS));
    QVERIFY(fsm.fire(SystemStateMachine::Trigger::START_SESSION));
    QVERIFY(fsm.fire(SystemStateMachine::Trigger::PREFLIGHT_OK));
    QCOMPARE(fsm.currentState(), QStringLiteral("DRIVING"));

    SafetyMonitorService svc(nullptr, &fsm);
    SafetyMonitorService::Config cfg;
    cfg.inactivityTimeoutMs = 40;
    cfg.deadmanTimeoutMs = 99999;
    svc.setConfig(cfg);
    QVERIFY(svc.initialize());
    svc.onOperatorActivity();
    svc.stop();

    QSignalSpy spySpeed(&svc, &SafetyMonitorService::speedLimitRequested);

    SafetyMonitorService::setUnitTestNowMsForTesting(100);
    svc.runSafetyChecks();
    QCOMPARE(spySpeed.count(), 1);
    QCOMPARE(spySpeed.at(0).at(0).toDouble(), 20.0);
  }
};

QTEST_MAIN(TestSafetyMonitorService)
#include "test_safetymonitorservice.moc"
