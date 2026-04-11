#include "app/client_window_frame_policy.h"

#include <QtTest/QtTest>

class TestWindowFramePolicy : public QObject {
  Q_OBJECT

 private slots:
  void explicit_use_window_frame() {
    ClientApp::WindowFramePolicyInputs in;
    in.platformName = QStringLiteral("xcb");
    in.dockerEnvFileExists = false;
    in.environment.insert(QStringLiteral("CLIENT_USE_WINDOW_FRAME"), QStringLiteral("1"));
    const ClientApp::WindowFramePolicyResult r = ClientApp::evaluateWindowFramePolicy(in);
    QVERIFY(r.useWindowFrame);
    QCOMPARE(r.decisionReason, QStringLiteral("explicit_env_system_frame"));
  }

  void explicit_disable_frameless() {
    ClientApp::WindowFramePolicyInputs in;
    in.platformName = QStringLiteral("xcb");
    in.environment.insert(QStringLiteral("CLIENT_DISABLE_FRAMELESS"), QStringLiteral("1"));
    const ClientApp::WindowFramePolicyResult r = ClientApp::evaluateWindowFramePolicy(in);
    QVERIFY(r.useWindowFrame);
    QCOMPARE(r.decisionReason, QStringLiteral("explicit_env_system_frame"));
  }

  void auto_mitigate_when_dockerenv_and_xcb() {
    ClientApp::WindowFramePolicyInputs in;
    in.platformName = QStringLiteral("xcb");
    in.dockerEnvFileExists = true;
    const ClientApp::WindowFramePolicyResult r = ClientApp::evaluateWindowFramePolicy(in);
    QVERIFY(r.useWindowFrame);
    QCOMPARE(r.decisionReason, QStringLiteral("auto_xcb_container_mitigate_transparency"));
    QVERIFY(r.likelyContainerRuntime);
  }

  void cgroup_docker_path_triggers_auto() {
    ClientApp::WindowFramePolicyInputs in;
    in.platformName = QStringLiteral("xcb");
    in.dockerEnvFileExists = false;
    in.procSelfCgroupSnippet = QStringLiteral("0::/docker/abc123");
    const ClientApp::WindowFramePolicyResult r = ClientApp::evaluateWindowFramePolicy(in);
    QVERIFY(r.useWindowFrame);
    QVERIFY(r.cgroupHit);
    QVERIFY(r.likelyContainerRuntime);
  }

  void native_xcb_host_stays_frameless_by_default() {
    ClientApp::WindowFramePolicyInputs in;
    in.platformName = QStringLiteral("xcb");
    in.dockerEnvFileExists = false;
    in.procSelfCgroupSnippet.clear();
    const ClientApp::WindowFramePolicyResult r = ClientApp::evaluateWindowFramePolicy(in);
    QVERIFY(!r.useWindowFrame);
    QCOMPARE(r.decisionReason, QStringLiteral("default_frameless_native_host"));
  }

  void force_frameless_overrides_container_auto() {
    ClientApp::WindowFramePolicyInputs in;
    in.platformName = QStringLiteral("xcb");
    in.dockerEnvFileExists = true;
    in.environment.insert(QStringLiteral("CLIENT_FORCE_FRAMELESS"), QStringLiteral("1"));
    const ClientApp::WindowFramePolicyResult r = ClientApp::evaluateWindowFramePolicy(in);
    QVERIFY(!r.useWindowFrame);
    QCOMPARE(r.decisionReason, QStringLiteral("CLIENT_FORCE_FRAMELESS=1"));
  }

  void auto_disabled_by_env_zero() {
    ClientApp::WindowFramePolicyInputs in;
    in.platformName = QStringLiteral("xcb");
    in.dockerEnvFileExists = true;
    in.environment.insert(QStringLiteral("CLIENT_AUTO_WINDOW_FRAME_FOR_CONTAINER"),
                          QStringLiteral("0"));
    const ClientApp::WindowFramePolicyResult r = ClientApp::evaluateWindowFramePolicy(in);
    QVERIFY(!r.useWindowFrame);
    QCOMPARE(r.decisionReason, QStringLiteral("CLIENT_AUTO_WINDOW_FRAME_FOR_CONTAINER=0"));
  }

  void use_window_frame_zero_overrides_auto() {
    ClientApp::WindowFramePolicyInputs in;
    in.platformName = QStringLiteral("xcb");
    in.dockerEnvFileExists = true;
    in.environment.insert(QStringLiteral("CLIENT_USE_WINDOW_FRAME"), QStringLiteral("0"));
    const ClientApp::WindowFramePolicyResult r = ClientApp::evaluateWindowFramePolicy(in);
    QVERIFY(!r.useWindowFrame);
    QCOMPARE(r.decisionReason, QStringLiteral("CLIENT_USE_WINDOW_FRAME=0"));
  }

  void client_in_container_env_alone_triggers_auto() {
    ClientApp::WindowFramePolicyInputs in;
    in.platformName = QStringLiteral("xcb");
    in.dockerEnvFileExists = false;
    in.procSelfCgroupSnippet.clear();
    in.environment.insert(QStringLiteral("CLIENT_IN_CONTAINER"), QStringLiteral("1"));
    const ClientApp::WindowFramePolicyResult r = ClientApp::evaluateWindowFramePolicy(in);
    QVERIFY(r.useWindowFrame);
    QVERIFY(r.likelyContainerRuntime);
  }

  void wayland_container_does_not_auto_frame() {
    ClientApp::WindowFramePolicyInputs in;
    in.platformName = QStringLiteral("wayland");
    in.dockerEnvFileExists = true;
    const ClientApp::WindowFramePolicyResult r = ClientApp::evaluateWindowFramePolicy(in);
    QVERIFY(!r.useWindowFrame);
    QCOMPARE(r.decisionReason, QStringLiteral("default_frameless_non_xcb"));
  }
};

QTEST_MAIN(TestWindowFramePolicy)
#include "test_window_frame_policy.moc"
