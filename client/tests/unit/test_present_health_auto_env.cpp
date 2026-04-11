#include "app/client_present_health_auto_env.h"

#include <QtTest/QtTest>

class TestPresentHealthAutoEnv : public QObject {
  Q_OBJECT

 private slots:
  void ci_empty_env_not_ci() {
    QProcessEnvironment env;
    QVERIFY(!ClientPresentHealthAutoEnv::looksLikeCiEnvironment(env));
  }

  void ci_ci_true() {
    QProcessEnvironment env;
    env.insert(QStringLiteral("CI"), QStringLiteral("true"));
    QVERIFY(ClientPresentHealthAutoEnv::looksLikeCiEnvironment(env));
  }

  void ci_ci_one() {
    QProcessEnvironment env;
    env.insert(QStringLiteral("CI"), QStringLiteral("1"));
    QVERIFY(ClientPresentHealthAutoEnv::looksLikeCiEnvironment(env));
  }

  void ci_github_actions_nonempty() {
    QProcessEnvironment env;
    env.insert(QStringLiteral("GITHUB_ACTIONS"), QStringLiteral("true"));
    QVERIFY(ClientPresentHealthAutoEnv::looksLikeCiEnvironment(env));
  }

  void ci_gitlab_ci_nonempty() {
    QProcessEnvironment env;
    env.insert(QStringLiteral("GITLAB_CI"), QStringLiteral("true"));
    QVERIFY(ClientPresentHealthAutoEnv::looksLikeCiEnvironment(env));
  }

  void software_gl_only_when_one() {
    QProcessEnvironment env;
    QVERIFY(!ClientPresentHealthAutoEnv::isSoftwareGlEnv(env));
    env.insert(QStringLiteral("LIBGL_ALWAYS_SOFTWARE"), QStringLiteral("1"));
    QVERIFY(ClientPresentHealthAutoEnv::isSoftwareGlEnv(env));
    env.insert(QStringLiteral("LIBGL_ALWAYS_SOFTWARE"), QStringLiteral("0"));
    QVERIFY(!ClientPresentHealthAutoEnv::isSoftwareGlEnv(env));
  }

  void container_client_in_container() {
    QProcessEnvironment env;
    QVERIFY(!ClientPresentHealthAutoEnv::likelyContainerRuntimeEnv(env));
    env.insert(QStringLiteral("CLIENT_IN_CONTAINER"), QStringLiteral("1"));
    QVERIFY(ClientPresentHealthAutoEnv::likelyContainerRuntimeEnv(env));
  }
};

QTEST_MAIN(TestPresentHealthAutoEnv)
#include "test_present_health_auto_env.moc"
