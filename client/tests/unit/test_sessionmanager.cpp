#include "app/client_app_bootstrap.h"
#include "authmanager.h"
#include "core/eventbus.h"
#include "core/metricscollector.h"
#include "core/systemstatemachine.h"
#include "services/safetymonitorservice.h"
#include "services/sessionmanager.h"
#include "services/vehiclecontrolservice.h"
#include "vehiclemanager.h"
#include "webrtcstreammanager.h"

#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QSurfaceFormat>
#include <QtTest/QtTest>

#include <stdexcept>

namespace {

class ThrowingWebRtcStreamManager final : public WebRtcStreamManager {
 public:
  bool throwOnConnect = false;
  bool throwIntOnConnect = false;
  bool throwOnDisconnect = false;
  bool throwIntOnDisconnect = false;
  bool throwOnSetVinEmpty = false;
  bool throwIntOnSetVinEmpty = false;

  void setCurrentVin(const QString &vin) override {
    if (vin.isEmpty()) {
      if (throwOnSetVinEmpty)
        throw std::runtime_error("setCurrentVin empty boom");
      if (throwIntOnSetVinEmpty)
        throw 42;
    }
    WebRtcStreamManager::setCurrentVin(vin);
  }

  void connectFourStreams(const QString &whepUrl = QString()) override {
    if (throwIntOnConnect)
      throw 42;
    if (throwOnConnect)
      throw std::runtime_error("connectFourStreams boom");
    WebRtcStreamManager::connectFourStreams(whepUrl);
  }

  void disconnectAll() override {
    if (throwIntOnDisconnect)
      throw 42;
    if (throwOnDisconnect)
      throw std::runtime_error("disconnectAll boom");
    WebRtcStreamManager::disconnectAll();
  }
};

/** 不启真实 QTimer，仅统计 SessionManager 对 SafetyMonitorService 的 start/stop 接线 */
class RecordingSafetyMonitorService final : public SafetyMonitorService {
 public:
  int startCount = 0;
  int stopCount = 0;

  RecordingSafetyMonitorService() : SafetyMonitorService(nullptr, nullptr) {}

  void start() override { ++startCount; }

  void stop() override { ++stopCount; }
};

class ThrowingVehicleManager final : public VehicleManager {
 public:
  bool throwAddTest = false;
  bool throwIntAddTest = false;
  bool throwLoadList = false;
  bool throwIntLoadList = false;

  void addTestVehicle(const QString &vin, const QString &name) override {
    if (throwIntAddTest)
      throw 42;
    if (throwAddTest)
      throw std::runtime_error("addTestVehicle boom");
    VehicleManager::addTestVehicle(vin, name);
  }

  void loadVehicleList(const QString &serverUrl, const QString &authToken) override {
    if (throwIntLoadList)
      throw 42;
    if (throwLoadList)
      throw std::runtime_error("loadVehicleList boom");
    VehicleManager::loadVehicleList(serverUrl, authToken);
  }
};

/** 不发起网络：仅统计 loadVehicleList 调用 */
class CountingVehicleManager final : public VehicleManager {
 public:
  int loadVehicleListCalls = 0;
  QString lastServerUrl;
  QString lastToken;

  void loadVehicleList(const QString &serverUrl, const QString &authToken) override {
    ++loadVehicleListCalls;
    lastServerUrl = serverUrl;
    lastToken = authToken;
  }
};

class ThrowingFsm final : public SystemStateMachine {
 public:
  bool throwStdOnAuthSuccess = false;
  bool throwIntOnAuthSuccess = false;
  bool throwStdOnLogout = false;
  bool throwIntOnLogout = false;

  explicit ThrowingFsm(EventBus *bus, QObject *parent = nullptr)
      : SystemStateMachine(bus, parent) {}

  bool fire(Trigger trigger) override {
    if (trigger == Trigger::AUTH_SUCCESS) {
      if (throwIntOnAuthSuccess)
        throw 42;
      if (throwStdOnAuthSuccess)
        throw std::runtime_error("fire AUTH_SUCCESS boom");
    }
    if (trigger == Trigger::LOGOUT) {
      if (throwIntOnLogout)
        throw 42;
      if (throwStdOnLogout)
        throw std::runtime_error("fire LOGOUT boom");
    }
    return SystemStateMachine::fire(trigger);
  }
};

class RecordingVehicleControlService final : public VehicleControlService {
 public:
  int setCredCount = 0;
  int clearCredCount = 0;
  int startCount = 0;
  int stopCount = 0;
  QString lastVin;
  QString lastSessionId;
  QString lastToken;

  RecordingVehicleControlService() : VehicleControlService(nullptr, nullptr, nullptr) {}

  void setSessionCredentials(const QString &vin, const QString &sessionId,
                             const QString &token) override {
    ++setCredCount;
    lastVin = vin;
    lastSessionId = sessionId;
    lastToken = token;
    VehicleControlService::setSessionCredentials(vin, sessionId, token);
  }

  void clearSessionCredentials() override {
    ++clearCredCount;
    VehicleControlService::clearSessionCredentials();
  }

  void start() override {
    ++startCount;
    VehicleControlService::start();
  }

  void stop() override {
    ++stopCount;
    VehicleControlService::stop();
  }
};

class ThrowingVehicleControlService final : public VehicleControlService {
 public:
  bool throwOnSet = false;
  bool throwOnClear = false;
  bool throwOnStop = false;
  int startCallCount = 0;

  ThrowingVehicleControlService() : VehicleControlService(nullptr, nullptr, nullptr) {}

  void setSessionCredentials(const QString &vin, const QString &sessionId,
                             const QString &token) override {
    if (throwOnSet)
      throw std::runtime_error("setSessionCredentials boom");
    VehicleControlService::setSessionCredentials(vin, sessionId, token);
  }

  void start() override {
    ++startCallCount;
    VehicleControlService::start();
  }

  void stop() override {
    if (throwOnStop)
      throw std::runtime_error("stop boom");
    VehicleControlService::stop();
  }

  void clearSessionCredentials() override {
    if (throwOnClear)
      throw std::runtime_error("clearSessionCredentials boom");
    VehicleControlService::clearSessionCredentials();
  }
};

}  // namespace

class TestSessionManager : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();

  // ── onLoginStatusChanged ─────────────────────────────────────────────
  void loginStatusChanged_false_withoutPriorLogin_noError();
  void loginStatusChanged_true_withoutPriorLogin_noError();
  void loginStatusChanged_false_afterLogin_triggersLogout_disconnectThrows_std();
  void loginStatusChanged_false_afterLogin_disconnectThrows_unknown();

  // ── onLoginSucceeded ───────────────────────────────────────────────────
  void loginSucceeded_nullVehicles_returnsAfterFsm();
  void loginSucceeded_nullFsm_noCrash();
  void loginSucceeded_testToken_addTestVehicle_ok();
  void loginSucceeded_testToken_addTestVehicleThrows_std();
  void loginSucceeded_testToken_addTestVehicleThrows_unknown();
  void loginSucceeded_normalToken_noAuth_skipsLoad();
  void loginSucceeded_normalToken_emptyServerUrl_skipsLoad();
  void loginSucceeded_normalToken_loadVehicleList_invoked();
  void loginSucceeded_normalToken_loadVehicleListThrows_std();
  void loginSucceeded_normalToken_loadVehicleListThrows_unknown();
  void loginSucceeded_fireAuthSuccessThrows_std();
  void loginSucceeded_fireAuthSuccessThrows_unknown();

  // ── onLogout ──────────────────────────────────────────────────────────
  void logout_nullWebrtc_noCrash();
  void logout_nullFsm_noCrash();
  void logout_disconnectAllThrows_std();
  void logout_fireLogoutThrows_std();
  void logout_fireLogoutThrows_unknown();
  void logout_clearSessionCredentialsThrows_std();
  void logout_stopThrows_recordsError();
  void recordingVcs_onLogout_incrementsClearCount();

  // ── onVinSelected ─────────────────────────────────────────────────────
  void vinSelected_nonEmpty_nullWebrtc_noError();
  void vinSelected_connectFourStreamsThrows_std();
  void vinSelected_connectFourStreamsThrows_unknown();
  void vinSelected_empty_clears_clearsCredentials_viaRecordingVcs();
  void vinSelected_empty_disconnectThrows_std();
  void vinSelected_empty_setCurrentVinThrows_std();
  void vinSelected_empty_setCurrentVinThrows_unknown();
  void vinSelected_empty_clearCredentialsThrows_std();

  // ── onSessionCreated ────────────────────────────────────────────────────
  void sessionCreated_emptyWhep_skipsConnect_noError();
  void sessionCreated_emptyWhep_stillInvokesSetCredentialsWhenAuthPresent();
  void sessionCreated_noAuth_recordingVcs_controlLoopNotStarted();
  void sessionCreated_nonEmptyWhep_nullWebrtc_startsSafetyWhenAuthPresent();
  void sessionCreated_longWhepUrl_noCrash();
  void sessionCreated_vehicleVinMismatch_logsStillNoError();
  void sessionCreated_recordingVcs_setsCredentials();
  void sessionCreated_setSessionCredentialsThrows_setsError();
  void sessionCreated_nonEmptyWhep_nullWebrtc_noCrash();
  void sessionCreated_nonEmptyWhep_connectThrows_std();
  void sessionCreated_nonEmptyWhep_connectThrows_unknown();

  // ── ClientApp 启动/显示探测（与 main 前序路径一致；Qt Test 实践：自包含、可重复）────────
  void clientBootstrap_applyPresentationSurfaceFormatDefaults_smoke();
  void clientBootstrap_probeOpenGl_skippedWhenSkipEnvSet();
  void clientBootstrap_runDisplayEnvironmentCheck_skipProbeReturnsZero();
  void clientBootstrap_resolveQmlMainUrl_findsOrSkip();
  void clientBootstrap_lastHardwarePresentationFalseAfterSkippedProbe();

  void cleanupTestCase();

 private:
  QString m_savedZlmVideoUrl;
  bool m_hadZlmEnv = false;
};

void TestSessionManager::initTestCase() {
  (void)MetricsCollector::instance();
  m_savedZlmVideoUrl = qEnvironmentVariable("ZLM_VIDEO_URL");
  m_hadZlmEnv = qEnvironmentVariableIsSet("ZLM_VIDEO_URL");
  qunsetenv("ZLM_VIDEO_URL");
}

void TestSessionManager::cleanupTestCase() {
  if (m_hadZlmEnv)
    qputenv("ZLM_VIDEO_URL", m_savedZlmVideoUrl.toUtf8());
  else
    qunsetenv("ZLM_VIDEO_URL");
}

void TestSessionManager::loginStatusChanged_false_withoutPriorLogin_noError() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  SessionManager sm(nullptr, nullptr, nullptr, nullptr, &fsm);
  sm.onLoginStatusChanged(false);
  QVERIFY(!sm.hasError());
}

void TestSessionManager::loginStatusChanged_true_withoutPriorLogin_noError() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  SessionManager sm(nullptr, nullptr, nullptr, nullptr, &fsm);
  sm.onLoginStatusChanged(true);
  QVERIFY(!sm.hasError());
}

void TestSessionManager::loginStatusChanged_false_afterLogin_triggersLogout_disconnectThrows_std() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  AuthManager auth(nullptr, false);
  ThrowingWebRtcStreamManager wsm;
  wsm.throwOnDisconnect = true;
  SessionManager sm(&auth, nullptr, nullptr, &wsm, &fsm);
  sm.onLoginSucceeded(QStringLiteral("tok"), QJsonObject{});
  QVERIFY(!sm.hasError());
  sm.onLoginStatusChanged(false);
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("disconnectAll")));
}

void TestSessionManager::loginStatusChanged_false_afterLogin_disconnectThrows_unknown() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  AuthManager auth(nullptr, false);
  ThrowingWebRtcStreamManager wsm;
  wsm.throwIntOnDisconnect = true;
  SessionManager sm(&auth, nullptr, nullptr, &wsm, &fsm);
  sm.onLoginSucceeded(QStringLiteral("tok"), QJsonObject{});
  sm.onLoginStatusChanged(false);
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("disconnectAll unknown")));
}

void TestSessionManager::loginSucceeded_nullVehicles_returnsAfterFsm() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  SessionManager sm(nullptr, nullptr, nullptr, nullptr, &fsm);
  sm.onLoginSucceeded(QStringLiteral("any"), QJsonObject{});
  QVERIFY(!sm.hasError());
}

void TestSessionManager::loginSucceeded_nullFsm_noCrash() {
  ThrowingVehicleManager vm;
  AuthManager auth(nullptr, false);
  SessionManager sm(&auth, &vm, nullptr, nullptr, nullptr);
  sm.onLoginSucceeded(QStringLiteral("test_token_x"), QJsonObject{});
  QVERIFY(vm.hasVehicles());
  QVERIFY(!sm.hasError());
}

void TestSessionManager::loginSucceeded_testToken_addTestVehicle_ok() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  VehicleManager vm;
  AuthManager auth(nullptr, false);
  SessionManager sm(&auth, &vm, nullptr, nullptr, &fsm);
  sm.onLoginSucceeded(QStringLiteral("test_token_ok"), QJsonObject{});
  QVERIFY(!sm.hasError());
  QVERIFY(vm.hasVehicles());
}

void TestSessionManager::loginSucceeded_testToken_addTestVehicleThrows_std() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingVehicleManager vm;
  vm.throwAddTest = true;
  AuthManager auth(nullptr, false);
  SessionManager sm(&auth, &vm, nullptr, nullptr, &fsm);
  sm.onLoginSucceeded(QStringLiteral("test_token_bad"), QJsonObject{});
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("addTestVehicle")));
}

void TestSessionManager::loginSucceeded_testToken_addTestVehicleThrows_unknown() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingVehicleManager vm;
  vm.throwIntAddTest = true;
  AuthManager auth(nullptr, false);
  SessionManager sm(&auth, &vm, nullptr, nullptr, &fsm);
  sm.onLoginSucceeded(QStringLiteral("test_token_int"), QJsonObject{});
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("addTestVehicle")));
}

void TestSessionManager::loginSucceeded_normalToken_noAuth_skipsLoad() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  CountingVehicleManager vm;
  SessionManager sm(nullptr, &vm, nullptr, nullptr, &fsm);
  sm.onLoginSucceeded(QStringLiteral("normal_jwt"), QJsonObject{});
  QCOMPARE(vm.loadVehicleListCalls, 0);
  QVERIFY(!sm.hasError());
}

void TestSessionManager::loginSucceeded_normalToken_emptyServerUrl_skipsLoad() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  CountingVehicleManager vm;
  AuthManager auth(nullptr, false);
  SessionManager sm(&auth, &vm, nullptr, nullptr, &fsm);
  sm.onLoginSucceeded(QStringLiteral("normal_jwt_2"), QJsonObject{});
  QCOMPARE(vm.loadVehicleListCalls, 0);
  QVERIFY(!sm.hasError());
}

void TestSessionManager::loginSucceeded_normalToken_loadVehicleList_invoked() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  CountingVehicleManager vm;
  AuthManager auth(nullptr, false);
  auth.setUnitTestServerUrl(QStringLiteral("http://127.0.0.1:9099"));
  SessionManager sm(&auth, &vm, nullptr, nullptr, &fsm);
  sm.onLoginSucceeded(QStringLiteral("Bearer_xyz"), QJsonObject{});
  QCOMPARE(vm.loadVehicleListCalls, 1);
  QCOMPARE(vm.lastServerUrl, QStringLiteral("http://127.0.0.1:9099"));
  QCOMPARE(vm.lastToken, QStringLiteral("Bearer_xyz"));
  QVERIFY(!sm.hasError());
}

void TestSessionManager::loginSucceeded_normalToken_loadVehicleListThrows_std() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingVehicleManager vm;
  vm.throwLoadList = true;
  AuthManager auth(nullptr, false);
  auth.setUnitTestServerUrl(QStringLiteral("http://127.0.0.1:8081"));
  SessionManager sm(&auth, &vm, nullptr, nullptr, &fsm);
  sm.onLoginSucceeded(QStringLiteral("not_a_test_token"), QJsonObject{});
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("loadVehicleList")));
}

void TestSessionManager::loginSucceeded_normalToken_loadVehicleListThrows_unknown() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingVehicleManager vm;
  vm.throwIntLoadList = true;
  AuthManager auth(nullptr, false);
  auth.setUnitTestServerUrl(QStringLiteral("http://127.0.0.1:8081"));
  SessionManager sm(&auth, &vm, nullptr, nullptr, &fsm);
  sm.onLoginSucceeded(QStringLiteral("jwt_int"), QJsonObject{});
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("loadVehicleList")));
}

void TestSessionManager::loginSucceeded_fireAuthSuccessThrows_std() {
  EventBus bus;
  ThrowingFsm fsm(&bus);
  fsm.throwStdOnAuthSuccess = true;
  SessionManager sm(nullptr, nullptr, nullptr, nullptr, &fsm);
  sm.onLoginSucceeded(QStringLiteral("any_token"), QJsonObject{});
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("fire(AUTH_SUCCESS)")));
}

void TestSessionManager::loginSucceeded_fireAuthSuccessThrows_unknown() {
  EventBus bus;
  ThrowingFsm fsm(&bus);
  fsm.throwIntOnAuthSuccess = true;
  SessionManager sm(nullptr, nullptr, nullptr, nullptr, &fsm);
  sm.onLoginSucceeded(QStringLiteral("any_token_2"), QJsonObject{});
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("fire(AUTH_SUCCESS)")));
}

void TestSessionManager::logout_nullWebrtc_noCrash() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  SessionManager sm(nullptr, nullptr, nullptr, nullptr, &fsm);
  sm.onLogout();
  QVERIFY(!sm.hasError());
}

void TestSessionManager::logout_nullFsm_noCrash() {
  ThrowingWebRtcStreamManager wsm;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, nullptr);
  sm.onLogout();
  QVERIFY(!sm.hasError());
}

void TestSessionManager::logout_disconnectAllThrows_std() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingWebRtcStreamManager wsm;
  wsm.throwOnDisconnect = true;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, &fsm);
  sm.onLogout();
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("disconnectAll")));
}

void TestSessionManager::logout_fireLogoutThrows_std() {
  EventBus bus;
  ThrowingFsm fsm(&bus);
  ThrowingWebRtcStreamManager wsm;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, &fsm);
  fsm.throwStdOnLogout = true;
  sm.onLogout();
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("fire(LOGOUT)")));
}

void TestSessionManager::logout_fireLogoutThrows_unknown() {
  EventBus bus;
  ThrowingFsm fsm(&bus);
  ThrowingWebRtcStreamManager wsm;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, &fsm);
  fsm.throwIntOnLogout = true;
  sm.onLogout();
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("fire(LOGOUT) unknown")));
}

void TestSessionManager::logout_clearSessionCredentialsThrows_std() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingVehicleControlService vcs;
  vcs.throwOnClear = true;
  SessionManager sm(nullptr, nullptr, nullptr, nullptr, &fsm);
  sm.setVehicleControl(&vcs);
  sm.onLogout();
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("onLogout exception")));
}

void TestSessionManager::recordingVcs_onLogout_incrementsClearCount() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  RecordingVehicleControlService vcs;
  RecordingSafetyMonitorService safety;
  ThrowingWebRtcStreamManager wsm;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, &fsm);
  sm.setVehicleControl(&vcs);
  sm.setSafetyMonitor(&safety);
  sm.onLogout();
  QVERIFY(safety.stopCount >= 1);
  QVERIFY(vcs.stopCount >= 1);
  QVERIFY(vcs.clearCredCount >= 1);
  QVERIFY(!sm.hasError());
}

void TestSessionManager::vinSelected_nonEmpty_nullWebrtc_noError() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  SessionManager sm(nullptr, nullptr, nullptr, nullptr, &fsm);
  sm.onVinSelected(QStringLiteral("VIN_ONLY"));
  QVERIFY(!sm.hasError());
}

void TestSessionManager::vinSelected_connectFourStreamsThrows_std() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingWebRtcStreamManager wsm;
  wsm.throwOnConnect = true;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, &fsm);
  sm.onVinSelected(QStringLiteral("VIN_THROW"));
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("connectFourStreams")));
}

void TestSessionManager::vinSelected_connectFourStreamsThrows_unknown() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingWebRtcStreamManager wsm;
  wsm.throwIntOnConnect = true;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, &fsm);
  sm.onVinSelected(QStringLiteral("VIN_INT"));
  QVERIFY(sm.hasError());
}

void TestSessionManager::vinSelected_empty_clears_clearsCredentials_viaRecordingVcs() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  RecordingVehicleControlService vcs;
  RecordingSafetyMonitorService safety;
  ThrowingWebRtcStreamManager wsm;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, &fsm);
  sm.setVehicleControl(&vcs);
  sm.setSafetyMonitor(&safety);
  sm.onVinSelected(QString());
  QVERIFY(safety.stopCount >= 1);
  QVERIFY(vcs.stopCount >= 1);
  QVERIFY(vcs.clearCredCount >= 1);
  QVERIFY(!sm.hasError());
}

void TestSessionManager::vinSelected_empty_disconnectThrows_std() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingWebRtcStreamManager wsm;
  wsm.throwOnDisconnect = true;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, &fsm);
  sm.onVinSelected(QString());
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("VIN clear")));
}

void TestSessionManager::vinSelected_empty_setCurrentVinThrows_std() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingWebRtcStreamManager wsm;
  wsm.throwOnSetVinEmpty = true;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, &fsm);
  sm.onVinSelected(QString());
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("VIN clear")));
}

void TestSessionManager::vinSelected_empty_setCurrentVinThrows_unknown() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingWebRtcStreamManager wsm;
  wsm.throwIntOnSetVinEmpty = true;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, &fsm);
  sm.onVinSelected(QString());
  QVERIFY(sm.hasError());
}

void TestSessionManager::vinSelected_empty_clearCredentialsThrows_std() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingVehicleControlService vcs;
  vcs.throwOnClear = true;
  ThrowingWebRtcStreamManager wsm;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, &fsm);
  sm.setVehicleControl(&vcs);
  sm.onVinSelected(QString());
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("onVinSelected")));
}

void TestSessionManager::sessionCreated_emptyWhep_skipsConnect_noError() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  SessionManager sm(nullptr, nullptr, nullptr, nullptr, &fsm);
  sm.onSessionCreated(QStringLiteral("VIN1"), QStringLiteral("sid1"), QStringLiteral("whip"),
                      QString(), {});
  QVERIFY(!sm.hasError());
}

void TestSessionManager::sessionCreated_emptyWhep_stillInvokesSetCredentialsWhenAuthPresent() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  AuthManager auth(nullptr, false);
  auth.setUnitTestAuthToken(QStringLiteral("tok"));
  RecordingVehicleControlService vcs;
  RecordingSafetyMonitorService safety;
  SessionManager sm(&auth, nullptr, nullptr, nullptr, &fsm);
  sm.setVehicleControl(&vcs);
  sm.setSafetyMonitor(&safety);
  sm.onSessionCreated(QStringLiteral("VINZ"), QStringLiteral("sidZ"), QStringLiteral("whip"),
                      QString(), {});
  QCOMPARE(vcs.setCredCount, 1);
  QCOMPARE(vcs.startCount, 1);
  QCOMPARE(safety.startCount, 1);
  QCOMPARE(vcs.lastVin, QStringLiteral("VINZ"));
  QCOMPARE(vcs.lastSessionId, QStringLiteral("sidZ"));
  QCOMPARE(vcs.lastToken, QStringLiteral("tok"));
}

void TestSessionManager::sessionCreated_longWhepUrl_noCrash() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  SessionManager sm(nullptr, nullptr, nullptr, nullptr, &fsm);
  QString whep(120, QChar('a'));
  sm.onSessionCreated(QStringLiteral("V"), QStringLiteral("s"), QStringLiteral("whip"), whep, {});
  QVERIFY(!sm.hasError());
}

void TestSessionManager::sessionCreated_vehicleVinMismatch_logsStillNoError() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  VehicleManager vm;
  vm.addTestVehicle(QStringLiteral("VIN_A"), QStringLiteral("a"));
  vm.setCurrentVin(QStringLiteral("VIN_A"));
  SessionManager sm(nullptr, &vm, nullptr, nullptr, &fsm);
  sm.onSessionCreated(QStringLiteral("VIN_B"), QStringLiteral("sid"), QStringLiteral("w"),
                      QStringLiteral("http://h/w"), {});
  QVERIFY(!sm.hasError());
}

void TestSessionManager::sessionCreated_recordingVcs_setsCredentials() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  AuthManager auth(nullptr, false);
  auth.setUnitTestAuthToken(QStringLiteral("atok"));
  RecordingVehicleControlService vcs;
  RecordingSafetyMonitorService safety;
  const QString whep =
      QStringLiteral("http://127.0.0.1:80/index/api/webrtc?app=live&stream=x&type=play");
  ThrowingWebRtcStreamManager wsm;
  wsm.throwOnConnect = true;
  SessionManager sm(&auth, nullptr, nullptr, &wsm, &fsm);
  sm.setVehicleControl(&vcs);
  sm.setSafetyMonitor(&safety);
  sm.onSessionCreated(QStringLiteral("VIN1"), QStringLiteral("sid1"), QStringLiteral("whip"), whep,
                      {});
  QCOMPARE(vcs.setCredCount, 1);
  QCOMPARE(vcs.startCount, 1);
  QCOMPARE(safety.startCount, 1);
  QVERIFY(sm.hasError());
}

void TestSessionManager::sessionCreated_setSessionCredentialsThrows_setsError() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  AuthManager auth(nullptr, false);
  auth.setUnitTestAuthToken(QStringLiteral("t"));
  ThrowingVehicleControlService vcs;
  vcs.throwOnSet = true;
  const QString whep =
      QStringLiteral("http://127.0.0.1:80/index/api/webrtc?app=x&stream=y&type=play");
  SessionManager sm(&auth, nullptr, nullptr, nullptr, &fsm);
  sm.setVehicleControl(&vcs);
  sm.onSessionCreated(QStringLiteral("V"), QStringLiteral("s"), QStringLiteral("whip"), whep, {});
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("onSessionCreated")));
  QCOMPARE(vcs.startCallCount, 0);
}

void TestSessionManager::sessionCreated_noAuth_recordingVcs_controlLoopNotStarted() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  RecordingVehicleControlService vcs;
  RecordingSafetyMonitorService safety;
  SessionManager sm(nullptr, nullptr, nullptr, nullptr, &fsm);
  sm.setVehicleControl(&vcs);
  sm.setSafetyMonitor(&safety);
  sm.onSessionCreated(QStringLiteral("VIN1"), QStringLiteral("sid1"), QStringLiteral("whip"),
                      QString(), {});
  QCOMPARE(vcs.startCount, 0);
  QCOMPARE(vcs.setCredCount, 0);
  QCOMPARE(safety.startCount, 0);
  QVERIFY(!sm.hasError());
}

void TestSessionManager::logout_stopThrows_recordsError() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  ThrowingVehicleControlService vcs;
  vcs.throwOnStop = true;
  RecordingSafetyMonitorService safety;
  ThrowingWebRtcStreamManager wsm;
  SessionManager sm(nullptr, nullptr, nullptr, &wsm, &fsm);
  sm.setVehicleControl(&vcs);
  sm.setSafetyMonitor(&safety);
  sm.onLogout();
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("onLogout")));
  QCOMPARE(safety.stopCount, 1);
}

void TestSessionManager::sessionCreated_nonEmptyWhep_nullWebrtc_noCrash() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  AuthManager auth(nullptr, false);
  auth.setUnitTestAuthToken(QStringLiteral("t"));
  SessionManager sm(&auth, nullptr, nullptr, nullptr, &fsm);
  const QString whep =
      QStringLiteral("http://127.0.0.1:80/index/api/webrtc?app=live&stream=x&type=play");
  sm.onSessionCreated(QStringLiteral("V"), QStringLiteral("s"), QStringLiteral("whip"), whep, {});
  QVERIFY(!sm.hasError());
}

void TestSessionManager::sessionCreated_nonEmptyWhep_nullWebrtc_startsSafetyWhenAuthPresent() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  AuthManager auth(nullptr, false);
  auth.setUnitTestAuthToken(QStringLiteral("t"));
  RecordingSafetyMonitorService safety;
  SessionManager sm(&auth, nullptr, nullptr, nullptr, &fsm);
  sm.setSafetyMonitor(&safety);
  const QString whep =
      QStringLiteral("http://127.0.0.1:80/index/api/webrtc?app=live&stream=x&type=play");
  sm.onSessionCreated(QStringLiteral("V"), QStringLiteral("s"), QStringLiteral("whip"), whep, {});
  QCOMPARE(safety.startCount, 1);
  QVERIFY(!sm.hasError());
}

void TestSessionManager::sessionCreated_nonEmptyWhep_connectThrows_std() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  AuthManager auth(nullptr, false);
  auth.setUnitTestAuthToken(QStringLiteral("tok"));
  ThrowingWebRtcStreamManager wsm;
  wsm.throwOnConnect = true;
  SessionManager sm(&auth, nullptr, nullptr, &wsm, &fsm);
  const QString whep =
      QStringLiteral("http://127.0.0.1:80/index/api/webrtc?app=live&stream=x&type=play");
  sm.onSessionCreated(QStringLiteral("VIN1"), QStringLiteral("sid1"), QStringLiteral("whip"), whep,
                      {});
  QVERIFY(sm.hasError());
  QVERIFY(sm.lastError().contains(QStringLiteral("connectFourStreams(whepUrl)")));
}

void TestSessionManager::sessionCreated_nonEmptyWhep_connectThrows_unknown() {
  EventBus bus;
  SystemStateMachine fsm(&bus);
  AuthManager auth(nullptr, false);
  auth.setUnitTestAuthToken(QStringLiteral("tok"));
  ThrowingWebRtcStreamManager wsm;
  wsm.throwIntOnConnect = true;
  SessionManager sm(&auth, nullptr, nullptr, &wsm, &fsm);
  const QString whep =
      QStringLiteral("http://127.0.0.1:80/index/api/webrtc?app=live&stream=x&type=play");
  sm.onSessionCreated(QStringLiteral("VIN1"), QStringLiteral("sid1"), QStringLiteral("whip"), whep,
                      {});
  QVERIFY(sm.hasError());
}

void TestSessionManager::clientBootstrap_applyPresentationSurfaceFormatDefaults_smoke() {
  ClientApp::applyPresentationSurfaceFormatDefaults();
  const QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
  QVERIFY(fmt.renderableType() == QSurfaceFormat::OpenGL || fmt.majorVersion() >= 2);
}

void TestSessionManager::clientBootstrap_probeOpenGl_skippedWhenSkipEnvSet() {
  qputenv("CLIENT_SKIP_OPENGL_PROBE", "1");
  const ClientApp::OpenGlFramebufferProbeResult r = ClientApp::probeOpenGlDefaultFramebuffer();
  qunsetenv("CLIENT_SKIP_OPENGL_PROBE");
  QVERIFY(r.skipped);
  QVERIFY(!r.success);
}

void TestSessionManager::clientBootstrap_runDisplayEnvironmentCheck_skipProbeReturnsZero() {
  qputenv("CLIENT_SKIP_OPENGL_PROBE", "1");
  qunsetenv("CLIENT_REQUIRE_HARDWARE_PRESENTATION");
  qunsetenv("CLIENT_TELOP_STATION");
  qputenv("CLIENT_GPU_PRESENTATION_OPTIONAL", "1");
  const int rc = ClientApp::runDisplayEnvironmentCheck();
  qunsetenv("CLIENT_SKIP_OPENGL_PROBE");
  qunsetenv("CLIENT_GPU_PRESENTATION_OPTIONAL");
  QCOMPARE(rc, 0);
}

void TestSessionManager::clientBootstrap_resolveQmlMainUrl_findsOrSkip() {
  QQmlApplicationEngine engine;
  const QUrl url = ClientApp::resolveQmlMainUrl(&engine);
  if (url.isValid()) {
    QVERIFY(url.isLocalFile());
    QVERIFY(!engine.importPathList().isEmpty());
  } else {
    QSKIP("当前工作目录未命中 main.qml 搜索路径");
  }
}

void TestSessionManager::clientBootstrap_lastHardwarePresentationFalseAfterSkippedProbe() {
  qputenv("CLIENT_SKIP_OPENGL_PROBE", "1");
  qunsetenv("CLIENT_REQUIRE_HARDWARE_PRESENTATION");
  qputenv("CLIENT_GPU_PRESENTATION_OPTIONAL", "1");
  (void)ClientApp::runDisplayEnvironmentCheck();
  qunsetenv("CLIENT_SKIP_OPENGL_PROBE");
  qunsetenv("CLIENT_GPU_PRESENTATION_OPTIONAL");
  QVERIFY(!ClientApp::lastHardwarePresentationEnvironmentOk());
}

QTEST_MAIN(TestSessionManager)
#include "test_sessionmanager.moc"
