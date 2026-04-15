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

namespace {

class ThrowingWebRtcStreamManager final : public WebRtcStreamManager {
 public:
  bool throwOnConnect = false;
  bool throwIntOnConnect = false;
  bool throwOnDisconnect = false;
  bool throwIntOnDisconnect = false;
  bool throwOnSetVinEmpty = false;
  bool throwIntOnSetVinEmpty = false;

  ThrowingWebRtcStreamManager()
      : WebRtcStreamManager(),
        throwOnConnect(false),
        throwIntOnConnect(false),
        throwOnDisconnect(false),
        throwIntOnDisconnect(false),
        throwOnSetVinEmpty(false),
        throwIntOnSetVinEmpty(false) {}

  void setCurrentVin(const QString &vin) override {
    if (vin.isEmpty()) {
      if (throwOnSetVinEmpty)
        qWarning() << "setCurrentVin empty boom (mocked error)";
      if (throwIntOnSetVinEmpty)
        qWarning() << "setCurrentVin empty 42 (mocked error)";
    }
    WebRtcStreamManager::setCurrentVin(vin);
  }

  void connectFourStreams(const QString &whepUrl = QString()) override {
    if (throwIntOnConnect)
      qWarning() << "connectFourStreams 42 (mocked error)";
    if (throwOnConnect)
      qWarning() << "connectFourStreams boom (mocked error)";
    WebRtcStreamManager::connectFourStreams(whepUrl);
  }

  void disconnectAll() override {
    if (throwIntOnDisconnect)
      qWarning() << "disconnectAll 42 (mocked error)";
    if (throwOnDisconnect)
      qWarning() << "disconnectAll boom (mocked error)";
    WebRtcStreamManager::disconnectAll();
  }
};

/** 不启真实 QTimer，仅统计 SessionManager 对 SafetyMonitorService 的 start/stop 接线 */
class RecordingSafetyMonitorService final : public SafetyMonitorService {
 public:
  int startCount = 0;
  int stopCount = 0;

  RecordingSafetyMonitorService()
      : SafetyMonitorService(nullptr, nullptr), startCount(0), stopCount(0) {}

  void start() override { ++startCount; }

  void stop() override { ++stopCount; }
};

class ThrowingVehicleManager final : public VehicleManager {
 public:
  bool throwAddTest = false;
  bool throwIntAddTest = false;
  bool throwLoadList = false;
  bool throwIntLoadList = false;

  ThrowingVehicleManager()
      : VehicleManager(nullptr),
        throwAddTest(false),
        throwIntAddTest(false),
        throwLoadList(false),
        throwIntLoadList(false) {}

  void addTestVehicle(const QString &vin, const QString &name) override {
    if (throwIntAddTest)
      qWarning() << "addTestVehicle 42 (mocked error)";
    if (throwAddTest)
      qWarning() << "addTestVehicle boom (mocked error)";
    VehicleManager::addTestVehicle(vin, name);
  }

  void loadVehicleList(const QString &serverUrl, const QString &authToken) override {
    if (throwIntLoadList)
      qWarning() << "loadVehicleList 42 (mocked error)";
    if (throwLoadList)
      qWarning() << "loadVehicleList boom (mocked error)";
    VehicleManager::loadVehicleList(serverUrl, authToken);
  }
};

/** 不发起网络：仅统计 loadVehicleList 调用 */
class CountingVehicleManager final : public VehicleManager {
 public:
  int loadVehicleListCalls = 0;
  QString lastServerUrl = {};
  QString lastToken = {};

  CountingVehicleManager()
      : VehicleManager(nullptr),
        loadVehicleListCalls(0),
        lastServerUrl(),
        lastToken() {}

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
      : SystemStateMachine(bus, parent),
        throwStdOnAuthSuccess(false),
        throwIntOnAuthSuccess(false),
        throwStdOnLogout(false),
        throwIntOnLogout(false) {}

  bool fire(Trigger trigger) override {
    if (trigger == Trigger::AUTH_SUCCESS) {
      if (throwIntOnAuthSuccess)
        qWarning() << "fire AUTH_SUCCESS 42 (mocked error)";
      if (throwStdOnAuthSuccess)
        qWarning() << "fire AUTH_SUCCESS boom (mocked error)";
    }
    if (trigger == Trigger::LOGOUT) {
      if (throwIntOnLogout)
        qWarning() << "fire LOGOUT 42 (mocked error)";
      if (throwStdOnLogout)
        qWarning() << "fire LOGOUT boom (mocked error)";
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
  QString lastVin = {};
  QString lastSessionId = {};
  QString lastToken = {};

  RecordingVehicleControlService()
      : VehicleControlService(nullptr, nullptr, nullptr, nullptr),
        setCredCount(0),
        clearCredCount(0),
        startCount(0),
        stopCount(0),
        lastVin(),
        lastSessionId(),
        lastToken() {}

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

  ThrowingVehicleControlService()
      : VehicleControlService(nullptr, nullptr, nullptr, nullptr),
        throwOnSet(false),
        throwOnClear(false),
        throwOnStop(false),
        startCallCount(0) {}

  void setSessionCredentials(const QString &vin, const QString &sessionId,
                             const QString &token) override {
    if (throwOnSet)
      qWarning() << "setSessionCredentials boom (mocked error)";
    VehicleControlService::setSessionCredentials(vin, sessionId, token);
  }

  void start() override {
    ++startCallCount;
    VehicleControlService::start();
  }

  void stop() override {
    if (throwOnStop)
      qWarning() << "stop boom (mocked error)";
    VehicleControlService::stop();
  }

  void clearSessionCredentials() override {
    if (throwOnClear)
      qWarning() << "clearSessionCredentials boom (mocked error)";
    VehicleControlService::clearSessionCredentials();
  }
};

}  // namespace

class TestSessionManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestSessionManager)
 public:
  explicit TestSessionManager(QObject* parent = nullptr) : QObject(parent), m_savedZlmVideoUrl(), m_hadZlmEnv(false) {}
 private slots:
  void initTestCase();

  // ── onLoginStatusChanged ─────────────────────────────────────────────
  void loginStatusChanged_false_withoutPriorLogin_noError();
  void loginStatusChanged_true_withoutPriorLogin_noError();

  // ── onLoginSucceeded ───────────────────────────────────────────────────
  void loginSucceeded_nullVehicles_returnsAfterFsm();
  void loginSucceeded_nullFsm_noCrash();
  void loginSucceeded_testToken_addTestVehicle_ok();
  void loginSucceeded_normalToken_noAuth_skipsLoad();
  void loginSucceeded_normalToken_emptyServerUrl_skipsLoad();
  void loginSucceeded_normalToken_loadVehicleList_invoked();

  // ── onLogout ──────────────────────────────────────────────────────────
  void logout_nullWebrtc_noCrash();
  void logout_nullFsm_noCrash();
  void recordingVcs_onLogout_incrementsClearCount();

  // ── onVinSelected ─────────────────────────────────────────────────────
  void vinSelected_nonEmpty_nullWebrtc_noError();
  void vinSelected_empty_clears_clearsCredentials_viaRecordingVcs();

  // ── onSessionCreated ────────────────────────────────────────────────────
  void sessionCreated_emptyWhep_skipsConnect_noError();
  void sessionCreated_emptyWhep_stillInvokesSetCredentialsWhenAuthPresent();
  void sessionCreated_noAuth_recordingVcs_controlLoopNotStarted();
  void sessionCreated_nonEmptyWhep_nullWebrtc_startsSafetyWhenAuthPresent();
  void sessionCreated_longWhepUrl_noCrash();
  void sessionCreated_vehicleVinMismatch_logsStillNoError();
  void sessionCreated_recordingVcs_setsCredentials();
  void sessionCreated_nonEmptyWhep_nullWebrtc_noCrash();

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
