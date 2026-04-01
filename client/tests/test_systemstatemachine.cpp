#include <QtTest/QtTest>
#include "core/eventbus.h"
#include "core/systemstatemachine.h"

class TestSystemStateMachine : public QObject
{
    Q_OBJECT
private slots:
    void transitions_idleToReadyToDriving();
    void invalidTransition_emergencyFromIdle();
};

void TestSystemStateMachine::transitions_idleToReadyToDriving()
{
    EventBus bus;
    SystemStateMachine fsm(&bus);
    QVERIFY(fsm.fire(SystemStateMachine::Trigger::AUTH_SUCCESS));
    QCOMPARE(fsm.currentState(), QStringLiteral("Ready"));
    QVERIFY(fsm.fire(SystemStateMachine::Trigger::START_SESSION));
    QCOMPARE(fsm.currentState(), QStringLiteral("Driving"));
    QVERIFY(fsm.fireByName(QStringLiteral("STOP_SESSION")));
    QCOMPARE(fsm.currentState(), QStringLiteral("Ready"));
}

void TestSystemStateMachine::invalidTransition_emergencyFromIdle()
{
    EventBus bus;
    SystemStateMachine fsm(&bus);
    QVERIFY(!fsm.fire(SystemStateMachine::Trigger::EMERGENCY_STOP));
}

QTEST_MAIN(TestSystemStateMachine)
#include "test_systemstatemachine.moc"
