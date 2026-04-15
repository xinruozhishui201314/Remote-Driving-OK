#include "core/eventbus.h"
#include "core/systemstatemachine.h"
#include "services/degradationmanager.h"

#include <QMetaObject>
#include <QtTest/QtTest>

class TestDegradationManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestDegradationManager)
 public:
  explicit TestDegradationManager(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void cleanup() { DegradationManager::clearUnitTestClockForTesting(); }

  void mapping_restrictToFull_alwaysFull() {
    DegradationManager::DegradationConfig cfg;
    QCOMPARE(DegradationMapping::targetLevelFromNetworkScore(true, 0.0, cfg),
             DegradationManager::DegradationLevel::FULL);
    QCOMPARE(DegradationMapping::targetLevelFromNetworkScore(true, 1.0, cfg),
             DegradationManager::DegradationLevel::FULL);
  }

  void mapping_thresholds_defaultConfig() {
    DegradationManager::DegradationConfig cfg;
    QCOMPARE(DegradationMapping::targetLevelFromNetworkScore(false, 0.80, cfg),
             DegradationManager::DegradationLevel::FULL);
    QCOMPARE(DegradationMapping::targetLevelFromNetworkScore(false, 0.70, cfg),
             DegradationManager::DegradationLevel::HIGH);
    QCOMPARE(DegradationMapping::targetLevelFromNetworkScore(false, 0.50, cfg),
             DegradationManager::DegradationLevel::MEDIUM);
    QCOMPARE(DegradationMapping::targetLevelFromNetworkScore(false, 0.35, cfg),
             DegradationManager::DegradationLevel::LOW);
    QCOMPARE(DegradationMapping::targetLevelFromNetworkScore(false, 0.20, cfg),
             DegradationManager::DegradationLevel::MINIMAL);
    QCOMPARE(DegradationMapping::targetLevelFromNetworkScore(false, 0.05, cfg),
             DegradationManager::DegradationLevel::SAFETY_STOP);
  }

  void policyForLevel_bitrateMonotonic() {
    const auto pFull =
        DegradationManager::policyForLevel(DegradationManager::DegradationLevel::FULL);
    const auto pHigh =
        DegradationManager::policyForLevel(DegradationManager::DegradationLevel::HIGH);
    const auto pMin =
        DegradationManager::policyForLevel(DegradationManager::DegradationLevel::MINIMAL);
    const auto pStop =
        DegradationManager::policyForLevel(DegradationManager::DegradationLevel::SAFETY_STOP);
    QVERIFY(pFull.maxBitrateKbps >= pHigh.maxBitrateKbps);
    QVERIFY(pHigh.maxBitrateKbps > pMin.maxBitrateKbps);
    QVERIFY(pMin.maxBitrateKbps > pStop.maxBitrateKbps);
  }

  void manager_nullFsm_lowScore_reachesSafetyStopSignals() {
    DegradationManager mgr(nullptr);
    DegradationManager::DegradationConfig cfg;
    cfg.checkIntervalMs = 5000;
    mgr.setConfig(cfg);
    QVERIFY(mgr.initialize());
    QSignalSpy spyStop(&mgr, &DegradationManager::safetyStopRequired);

    NetworkQuality q;
    q.score = 0.0;
    mgr.updateNetworkQuality(q);
    QVERIFY(QMetaObject::invokeMethod(&mgr, "checkDegradation", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&mgr, "checkDegradation", Qt::DirectConnection));

    QCOMPARE(spyStop.count(), 1);
    QCOMPARE(mgr.currentLevel(), DegradationManager::DegradationLevel::SAFETY_STOP);
  }

  void manager_fsmIdle_lowScore_staysFull() {
    EventBus bus;
    SystemStateMachine fsm(&bus);
    DegradationManager mgr(&fsm);
    QVERIFY(mgr.initialize());

    NetworkQuality q;
    q.score = 0.0;
    mgr.updateNetworkQuality(q);
    QVERIFY(QMetaObject::invokeMethod(&mgr, "checkDegradation", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&mgr, "checkDegradation", Qt::DirectConnection));

    QCOMPARE(mgr.currentLevel(), DegradationManager::DegradationLevel::FULL);
  }

  void manager_upgrade_respects_hysteresis_ms() {
    DegradationManager mgr(nullptr);
    DegradationManager::DegradationConfig cfg;
    cfg.hysteresisMs = 1000;
    cfg.checkIntervalMs = 5000;
    cfg.level1ThresholdScore = 0.75;
    cfg.level2ThresholdScore = 0.60;
    cfg.level3ThresholdScore = 0.45;
    mgr.setConfig(cfg);
    QVERIFY(mgr.initialize());

    DegradationManager::setUnitTestNowMsForTesting(0);

    NetworkQuality qMed;
    qMed.score = 0.50;
    mgr.updateNetworkQuality(qMed);
    QVERIFY(QMetaObject::invokeMethod(&mgr, "checkDegradation", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&mgr, "checkDegradation", Qt::DirectConnection));
    QCOMPARE(mgr.currentLevel(), DegradationManager::DegradationLevel::MEDIUM);

    NetworkQuality qFull;
    qFull.score = 0.80;
    mgr.updateNetworkQuality(qFull);
    QVERIFY(QMetaObject::invokeMethod(&mgr, "checkDegradation", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&mgr, "checkDegradation", Qt::DirectConnection));
    QCOMPARE(mgr.currentLevel(), DegradationManager::DegradationLevel::MEDIUM);

    DegradationManager::setUnitTestNowMsForTesting(500);
    QVERIFY(QMetaObject::invokeMethod(&mgr, "checkDegradation", Qt::DirectConnection));
    QCOMPARE(mgr.currentLevel(), DegradationManager::DegradationLevel::MEDIUM);

    DegradationManager::setUnitTestNowMsForTesting(1000);
    QVERIFY(QMetaObject::invokeMethod(&mgr, "checkDegradation", Qt::DirectConnection));
    QCOMPARE(mgr.currentLevel(), DegradationManager::DegradationLevel::FULL);
  }
};

QTEST_MAIN(TestDegradationManager)
#include "test_degradationmanager.moc"
