#include "app/client_startup_readiness_gate.h"

#include <QProcessEnvironment>
#include <QtTest/QtTest>

class TestStartupReadinessProfile : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestStartupReadinessProfile)
 public:
  explicit TestStartupReadinessProfile(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void default_tcp_targets_full_has_four() {
    const QStringList names = ClientApp::defaultTcpTargetNamesForReadinessProfile(
        ClientApp::StartupReadinessProfile::Full);
    QCOMPARE(names.size(), 4);
    QVERIFY(names.contains(QStringLiteral("backend")));
    QVERIFY(names.contains(QStringLiteral("mqtt")));
    QVERIFY(names.contains(QStringLiteral("keycloak")));
    QVERIFY(names.contains(QStringLiteral("zlm")));
  }

  void default_tcp_targets_standard_has_backend_mqtt() {
    const QStringList names = ClientApp::defaultTcpTargetNamesForReadinessProfile(
        ClientApp::StartupReadinessProfile::Standard);
    QCOMPARE(names.size(), 2);
    QCOMPARE(names.at(0), QStringLiteral("backend"));
    QCOMPARE(names.at(1), QStringLiteral("mqtt"));
  }

  void parse_explicit_minimal() {
    QProcessEnvironment e;
    e.insert(QStringLiteral("CLIENT_STARTUP_READINESS_PROFILE"), QStringLiteral("minimal"));
    QCOMPARE(ClientApp::parseStartupReadinessProfile(e), ClientApp::StartupReadinessProfile::Minimal);
  }

  void parse_explicit_full() {
    QProcessEnvironment e;
    e.insert(QStringLiteral("CLIENT_STARTUP_READINESS_PROFILE"), QStringLiteral("production"));
    QCOMPARE(ClientApp::parseStartupReadinessProfile(e), ClientApp::StartupReadinessProfile::Full);
  }

  void parse_dev_alias() {
    QProcessEnvironment e;
    e.insert(QStringLiteral("CLIENT_STARTUP_READINESS_PROFILE"), QStringLiteral("DEV"));
    QCOMPARE(ClientApp::parseStartupReadinessProfile(e), ClientApp::StartupReadinessProfile::Minimal);
  }
};

QTEST_MAIN(TestStartupReadinessProfile)
#include "test_startup_readiness_profile.moc"
