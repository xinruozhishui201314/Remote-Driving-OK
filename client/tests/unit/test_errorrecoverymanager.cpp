#include "services/errorrecoverymanager.h"

#include <QSignalSpy>
#include <QtTest/QtTest>

class TestErrorRecoveryManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestErrorRecoveryManager)
 public:
  explicit TestErrorRecoveryManager(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void recovery_success_clearsCategory() {
    ErrorRecoveryManager mgr;
    ErrorRecoveryManager::RecoveryConfig cfg;
    cfg.baseRetryMs = 10;
    cfg.maxRetries = 5;
    mgr.setConfig(cfg);
    bool called = false;
    mgr.registerRecoveryAction(ErrorRecoveryManager::RecoveryLevel::AUTO_RETRY, [&called]() {
      called = true;
      return true;
    });

    QSignalSpy spyOk(&mgr, &ErrorRecoveryManager::recoveryAttempted);
    mgr.reportError(ErrorRecoveryManager::ErrorCategory::NETWORK_ERROR, QStringLiteral("net"));

    QVERIFY(spyOk.wait(200));
    QVERIFY(called);
    QCOMPARE(spyOk.count(), 1);
    QCOMPARE(spyOk.at(0).at(0).toInt(),
             static_cast<int>(ErrorRecoveryManager::RecoveryLevel::AUTO_RETRY));
    QCOMPARE(spyOk.at(0).at(1).toBool(), true);
  }

  void recovery_noAction_incrementsRetryAndEmitsFalse() {
    ErrorRecoveryManager mgr;
    ErrorRecoveryManager::RecoveryConfig cfg;
    cfg.baseRetryMs = 10;
    cfg.maxRetries = 5;
    mgr.setConfig(cfg);

    QSignalSpy spy(&mgr, &ErrorRecoveryManager::recoveryAttempted);
    mgr.reportError(ErrorRecoveryManager::ErrorCategory::MEDIA_ERROR, QStringLiteral("media"));

    QVERIFY(spy.wait(200));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toBool(), false);
  }

  void escalate_afterMaxFailures_eventuallySafeStop() {
    ErrorRecoveryManager mgr;
    ErrorRecoveryManager::RecoveryConfig cfg;
    cfg.baseRetryMs = 5;
    cfg.maxRetries = 1;
    cfg.backoffFactor = 2.0;
    mgr.setConfig(cfg);
    mgr.registerRecoveryAction(ErrorRecoveryManager::RecoveryLevel::AUTO_RETRY,
                               []() { return false; });
    mgr.registerRecoveryAction(ErrorRecoveryManager::RecoveryLevel::SERVICE_RESTART,
                               []() { return false; });
    mgr.registerRecoveryAction(ErrorRecoveryManager::RecoveryLevel::SESSION_REBUILD,
                               []() { return false; });

    QSignalSpy spySafe(&mgr, &ErrorRecoveryManager::safeStopRequired);
    mgr.reportError(ErrorRecoveryManager::ErrorCategory::CONTROL_ERROR, QStringLiteral("ctrl"));

    int attempts = 0;
    while (spySafe.isEmpty() && attempts < 40) {
      QTest::qWait(80);
      ++attempts;
    }
    QVERIFY2(!spySafe.isEmpty(), "expected safeStopRequired after escalations");
  }

  void clearError_removesRecord() {
    ErrorRecoveryManager mgr;
    mgr.reportError(ErrorRecoveryManager::ErrorCategory::AUTH_ERROR, QStringLiteral("auth"));
    mgr.clearError(ErrorRecoveryManager::ErrorCategory::AUTH_ERROR);
    // No crash on second clear
    mgr.clearError(ErrorRecoveryManager::ErrorCategory::AUTH_ERROR);
  }
};

QTEST_MAIN(TestErrorRecoveryManager)
#include "test_errorrecoverymanager.moc"
