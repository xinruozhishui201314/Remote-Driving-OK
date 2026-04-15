#include "sessionmanager.h"

#include "../authmanager.h"
#include "../core/systemstatemachine.h"
#include "../core/tracing.h"
#include "../mqttcontroller.h"
#include "../vehiclemanager.h"
#include "../webrtcstreammanager.h"
#include "safetymonitorservice.h"
#include "vehiclecontrolservice.h"

#include <QDebug>
#include <QProcessEnvironment>

SessionManager::SessionManager(AuthManager *auth, VehicleManager *vehicles, MqttController *mqtt,
                               WebRtcStreamManager *webrtc, SystemStateMachine *fsm,
                               QObject *parent)
    : QObject(parent),
      m_auth(auth),
      m_vehicles(vehicles),
      m_mqtt(mqtt),
      m_webrtc(webrtc),
      m_fsm(fsm) {}

void SessionManager::setVehicleControl(VehicleControlService *vcs) { m_vehicleControl = vcs; }

void SessionManager::setSafetyMonitor(SafetyMonitorService *safety) { m_safetyMonitor = safety; }

void SessionManager::setError(const QString &error) {
  m_lastError = error;
  m_hasError.storeRelaxed(1);
  emit errorOccurred(error);
}

void SessionManager::onLoginStatusChanged(bool loggedIn) {
  try {
    if (m_hadSuccessfulLogin && !loggedIn) {
      try {
        onLogout();
      } catch (const std::exception &e) {
        setError(QStringLiteral("onLogout exception: %1").arg(e.what()));
        qCritical() << "[Client][Session][ERROR] onLogout 异常:" << e.what();
      } catch (...) {
        setError("onLogout unknown exception");
        qCritical() << "[Client][Session][ERROR] onLogout 未知异常";
      }
    }
  } catch (const std::exception &e) {
    setError(QStringLiteral("onLoginStatusChanged exception: %1").arg(e.what()));
    qCritical() << "[Client][Session][ERROR] onLoginStatusChanged 总异常 loggedIn=" << loggedIn
                << " error=" << e.what();
  } catch (...) {
    setError("onLoginStatusChanged unknown exception");
    qCritical() << "[Client][Session][ERROR] onLoginStatusChanged 未知异常 loggedIn=" << loggedIn;
  }
}

void SessionManager::onLoginSucceeded(const QString &token, const QJsonObject &userInfo) {
  try {
    m_hadSuccessfulLogin = true;
    {
      const QString zlm =
          QProcessEnvironment::systemEnvironment().value(QStringLiteral("ZLM_VIDEO_URL"));
      qInfo().noquote()
          << QStringLiteral("[Client][StreamE2E][LOGIN_OK] traceBoot=1 ZLM_VIDEO_URL_set=")
          << (!zlm.isEmpty() ? 1 : 0) << "ZLM_VIDEO_URL_len=" << zlm.size()
          << "★ 登录成功时尚未拉流；无会话 whep 时依赖 ZLM_VIDEO_URL + 顶部「连接」定时器";
    }
    Tracing::instance().setCurrentTraceId(Tracing::generateTraceId());
    qInfo().noquote() << "[Client][Session] new traceId after login="
                      << Tracing::instance().currentTraceId().left(16);
    const bool isTestToken = token.startsWith(QStringLiteral("test_token_"));
    qDebug().noquote() << "[Client][Session] loginSucceeded testToken=" << isTestToken
                       << " tokenLen=" << token.size()
                       << " user=" << userInfo.value(QStringLiteral("username")).toString();

    if (m_fsm) {
      try {
        m_fsm->fire(SystemStateMachine::Trigger::AUTH_SUCCESS);
      } catch (const std::exception &e) {
        setError(QStringLiteral("fire(AUTH_SUCCESS) exception: %1").arg(e.what()));
        qCritical() << "[Client][Session][ERROR] fire(AUTH_SUCCESS) 异常:" << e.what();
      } catch (...) {
        setError("fire(AUTH_SUCCESS) unknown exception");
        qCritical() << "[Client][Session][ERROR] fire(AUTH_SUCCESS) 未知异常";
      }
    }

    if (!m_vehicles)
      return;

    if (isTestToken) {
      try {
        m_vehicles->addTestVehicle(QStringLiteral("123456789"), QStringLiteral("测试车辆"));
      } catch (const std::exception &e) {
        setError(QStringLiteral("addTestVehicle exception: %1").arg(e.what()));
        qCritical() << "[Client][Session][ERROR] addTestVehicle 异常:" << e.what();
      } catch (...) {
        setError("addTestVehicle unknown exception");
        qCritical() << "[Client][Session][ERROR] addTestVehicle 未知异常";
      }
      return;
    }

    if (!m_auth)
      return;
    const QString serverUrl = m_auth->serverUrl();
    if (serverUrl.isEmpty()) {
      qDebug().noquote() << "[Client][Session] serverUrl empty, skip loadVehicleList";
      return;
    }
    try {
      m_vehicles->loadVehicleList(serverUrl, token);
    } catch (const std::exception &e) {
      setError(QStringLiteral("loadVehicleList exception: %1").arg(e.what()));
      qCritical() << "[Client][Session][ERROR] loadVehicleList 异常:" << e.what();
    } catch (...) {
      setError("loadVehicleList unknown exception");
      qCritical() << "[Client][Session][ERROR] loadVehicleList 未知异常";
    }
  } catch (const std::exception &e) {
    setError(QStringLiteral("onLoginSucceeded exception: %1").arg(e.what()));
    qCritical() << "[Client][Session][ERROR] onLoginSucceeded 总异常:" << e.what();
  } catch (...) {
    setError("onLoginSucceeded unknown exception");
    qCritical() << "[Client][Session][ERROR] onLoginSucceeded 未知异常";
  }
}

void SessionManager::onLogout() {
  try {
    qInfo().noquote()
        << QStringLiteral("[Client][StreamE2E][LOGOUT] hadLogin=") << m_hadSuccessfulLogin
        << "m_selectedVin=" << m_selectedVin
        << "★ 将 disconnectAll；m_currentBase 在 C++ 侧通常仍保留最后一次非空值直至下次 CFS";
    // 先停安全巡检，避免断链/清凭证过程中误触死手或急停；再发中立控车、清会话签名
    if (m_safetyMonitor)
      QMetaObject::invokeMethod(m_safetyMonitor, "stop");
    if (m_vehicleControl)
      QMetaObject::invokeMethod(m_vehicleControl, "stop");
    if (m_vehicleControl)
      QMetaObject::invokeMethod(m_vehicleControl, "clearSessionCredentials");
    qInfo().noquote() << "[Client][Session] onLogout → disconnectAll 四路 WebRTC";
    try {
      if (m_webrtc)
        m_webrtc->disconnectAll();
    } catch (const std::exception &e) {
      setError(QStringLiteral("disconnectAll exception: %1").arg(e.what()));
      qCritical() << "[Client][Session][ERROR] disconnectAll 异常:" << e.what();
    } catch (...) {
      setError(QStringLiteral("disconnectAll unknown exception"));
      qCritical() << "[Client][Session][ERROR] disconnectAll 未知异常";
    }
    if (m_fsm) {
      try {
        m_fsm->fire(SystemStateMachine::Trigger::LOGOUT);
      } catch (const std::exception &e) {
        setError(QStringLiteral("fire(LOGOUT) exception: %1").arg(e.what()));
        qCritical() << "[Client][Session][ERROR] fire(LOGOUT) 异常:" << e.what();
      } catch (...) {
        setError(QStringLiteral("fire(LOGOUT) unknown exception"));
        qCritical() << "[Client][Session][ERROR] fire(LOGOUT) 未知异常";
      }
    }
    m_hadSuccessfulLogin = false;
  } catch (const std::exception &e) {
    setError(QStringLiteral("onLogout exception: %1").arg(e.what()));
    qCritical() << "[Client][Session][ERROR] onLogout 总异常:" << e.what();
  } catch (...) {
    setError("onLogout unknown exception");
    qCritical() << "[Client][Session][ERROR] onLogout 未知异常";
  }
}

void SessionManager::stop() {
  qInfo().noquote() << "[Client][Session] stop() requested, cleaning up session=" << m_selectedVin;
  
  // 1. 通过 onVinSelected("") 执行全套下层清理（Safety, Control, WebRTC）
  onVinSelected(QString());

  // 2. 确保 VehicleManager 的当前 VIN 也被同步清空，驱动 UI 回到选车页
  if (m_vehicles && !m_vehicles->currentVin().isEmpty()) {
    m_vehicles->setCurrentVin(QString());
  }
}

void SessionManager::onVinSelected(const QString &vin) {
  try {
    {
      const QString zlm =
          QProcessEnvironment::systemEnvironment().value(QStringLiteral("ZLM_VIDEO_URL"));
      qInfo().noquote()
          << QStringLiteral("[Client][StreamE2E][VIN_SELECTED] vinArg=")
          << (vin.isEmpty() ? QStringLiteral("(empty)") : vin)
          << "ZLM_VIDEO_URL_set=" << (!zlm.isEmpty() ? 1 : 0) << "webrtcPtr=" << (void *)m_webrtc
          << "★ 非空 VIN 将 setCurrentVin + connectFourStreams()（whep 空→走环境变量）";
    }
    m_selectedVin = vin;
    if (vin.isEmpty()) {
      qInfo().noquote() << QStringLiteral(
          "[Client][StreamE2E][VIN_SELECTED] branch=CLEAR disconnectAll");
      qInfo().noquote() << "[Client][Session] VIN 已清空 → disconnectAll 四路";
      if (m_safetyMonitor)
        QMetaObject::invokeMethod(m_safetyMonitor, "stop");
      if (m_vehicleControl)
        QMetaObject::invokeMethod(m_vehicleControl, "stop");
      if (m_vehicleControl)
        QMetaObject::invokeMethod(m_vehicleControl, "clearSessionCredentials");
      if (m_webrtc) {
        try {
          m_webrtc->setCurrentVin(QString());
          m_webrtc->disconnectAll();
        } catch (const std::exception &e) {
          setError(QStringLiteral("VIN clear disconnectAll exception: %1").arg(e.what()));
          qCritical() << "[Client][Session][ERROR] VIN 清空时 disconnectAll 异常:" << e.what();
        }
      }
      return;
    }
    qInfo().noquote() << "[Client][Session] VIN 已选中 vin=" << vin
                      << " → setCurrentVin + connectFourStreams";
    if (m_webrtc) {
      try {
        qInfo().noquote() << QStringLiteral(
            "[Client][StreamE2E][VIN_SELECTED] invoking setCurrentVin+connectFourStreams() no "
            "explicit whep");
        m_webrtc->setCurrentVin(vin);
        m_webrtc->connectFourStreams();
      } catch (const std::exception &e) {
        setError(QStringLiteral("connectFourStreams exception: %1").arg(e.what()));
        qCritical() << "[Client][Session][ERROR] connectFourStreams 异常:" << e.what();
      } catch (...) {
        setError("connectFourStreams unknown exception");
        qCritical() << "[Client][Session][ERROR] connectFourStreams 未知异常";
      }
    }
  } catch (const std::exception &e) {
    setError(QStringLiteral("onVinSelected exception: %1").arg(e.what()));
    qCritical() << "[Client][Session][ERROR] onVinSelected 总异常 vin=" << vin
                << " error=" << e.what();
  } catch (...) {
    setError("onVinSelected unknown exception");
    qCritical() << "[Client][Session][ERROR] onVinSelected 未知异常 vin=" << vin;
  }
}

void SessionManager::onSessionCreated(const QString &sessionVin, const QString &sessionId,
                                      const QString &whipUrl, const QString &whepUrl,
                                      const QJsonObject &controlConfig) {
  try {
    Q_UNUSED(whipUrl)
    Q_UNUSED(controlConfig)
    m_selectedVin = sessionVin;
    {
      const QString zlm =
          QProcessEnvironment::systemEnvironment().value(QStringLiteral("ZLM_VIDEO_URL"));
      qInfo().noquote()
          << QStringLiteral("[Client][StreamE2E][SESSION_CREATED] sessionVin=") << sessionVin
          << "sessionId=" << sessionId << "whepEmpty=" << (whepUrl.isEmpty() ? 1 : 0)
          << "whepLen=" << whepUrl.size() << "ZLM_VIDEO_URL_set=" << (!zlm.isEmpty() ? 1 : 0)
          << "★ whepEmpty=1 时将不会 connectFourStreams(whep)，仅靠后续 UI 定时器/手动连接";
    }
    qInfo().noquote() << "[Client][Session] onSessionCreated sessionVin=" << sessionVin
                      << " sessionId=" << sessionId << " m_selectedVin 已与后端会话对齐"
                      << " whepUrl="
                      << (whepUrl.length() > 80 ? whepUrl.left(80) + "..." : whepUrl);
    if (m_vehicles && m_vehicles->currentVin() != sessionVin) {
      qWarning() << "[Client][Session] onSessionCreated: VehicleManager.currentVin="
                 << m_vehicles->currentVin() << " 与 sessionVin=" << sessionVin
                 << " 不一致（理论上不应发生：过期响应应在 VehicleManager 已丢弃）";
    }
    if (m_vehicleControl && m_auth) {
      QMetaObject::invokeMethod(m_vehicleControl, "setSessionCredentials",
                                Q_ARG(QString, sessionVin), Q_ARG(QString, sessionId),
                                Q_ARG(QString, m_auth->authToken()));
      QMetaObject::invokeMethod(m_vehicleControl, "start");
      qInfo().noquote() << "[Client][Session] VehicleControlService::start() "
                           "已调用（后端会话已建立，100Hz 控车环路与"
                        << "sendDriveCommand/updateInput 闭环）sessionVin=" << sessionVin;

      // ★ 核心修复：自动连接 MQTT Broker
      // 解决「视频流已通但控制未连」的 Split-Brain 问题，确保远驾接管按钮可用
      if (m_mqtt && !m_mqtt->mqttBrokerConnected()) {
        qInfo().noquote() << "[Client][Session] 正在自动触发 MQTT 连接... vin=" << sessionVin;
        m_mqtt->connectToBroker();
      }
    }
    if (m_safetyMonitor && m_auth) {
      QMetaObject::invokeMethod(m_safetyMonitor, "start");
      m_safetyMonitor->onHeartbeatReceived();
      qInfo().noquote() << "[Client][Session] SafetyMonitorService::start() 已调用（50Hz "
                           "延迟/心跳/死手巡检）sessionVin="
                        << sessionVin;
    }
    if (whepUrl.isEmpty()) {
      qCritical().noquote() << QStringLiteral(
                                   "[Client][StreamE2E][SESSION_CREATED] branch=SKIP_CONNECT "
                                   "whepUrl empty — 不会调用 connectFourStreams(whep)")
                            << "sessionId=" << sessionId
                            << "ZLM_VIDEO_URL fallback still possible via DrivingTopChrome 25s "
                               "timer or manual connect";
      qWarning().noquote() << "[Client][Session] onSessionCreated: whepUrl 为空，跳过重连";
      return;
    }
    if (m_webrtc) {
      try {
        qInfo().noquote() << QStringLiteral(
                                 "[Client][StreamE2E][SESSION_CREATED] invoking "
                                 "setCurrentVin+connectFourStreams(whep) whepLen=")
                          << whepUrl.size();
        m_webrtc->setCurrentVin(sessionVin);
        m_webrtc->connectFourStreams(whepUrl);
      } catch (const std::exception &e) {
        setError(QStringLiteral("connectFourStreams(whepUrl) exception: %1").arg(e.what()));
        qCritical() << "[Client][Session][ERROR] connectFourStreams(whepUrl) 异常:" << e.what();
      } catch (...) {
        setError("connectFourStreams(whepUrl) unknown exception");
        qCritical() << "[Client][Session][ERROR] connectFourStreams(whepUrl) 未知异常";
      }
    }
  } catch (const std::exception &e) {
    setError(QStringLiteral("onSessionCreated exception: %1").arg(e.what()));
    qCritical() << "[Client][Session][ERROR] onSessionCreated 总异常 sessionId=" << sessionId
                << " error=" << e.what();
  } catch (...) {
    setError("onSessionCreated unknown exception");
    qCritical() << "[Client][Session][ERROR] onSessionCreated 未知异常 sessionId=" << sessionId;
  }
}
