#include "sessionmanager.h"
#include "../authmanager.h"
#include "../vehiclemanager.h"
#include "../mqttcontroller.h"
#include "../webrtcstreammanager.h"
#include "../core/systemstatemachine.h"
#include <QDebug>

SessionManager::SessionManager(AuthManager *auth,
                               VehicleManager *vehicles,
                               MqttController *mqtt,
                               WebRtcStreamManager *webrtc,
                               SystemStateMachine *fsm,
                               QObject *parent)
    : QObject(parent)
    , m_auth(auth)
    , m_vehicles(vehicles)
    , m_mqtt(mqtt)
    , m_webrtc(webrtc)
    , m_fsm(fsm)
{
}

void SessionManager::onLoginStatusChanged(bool loggedIn)
{
    try {
        if (m_hadSuccessfulLogin && !loggedIn) {
            try {
                onLogout();
            } catch (const std::exception& e) {
                qCritical() << "[Client][Session][ERROR] onLogout 异常:" << e.what();
            }
        }
    } catch (const std::exception& e) {
        qCritical() << "[Client][Session][ERROR] onLoginStatusChanged 总异常 loggedIn=" << loggedIn
                    << " error=" << e.what();
    } catch (...) {
        qCritical() << "[Client][Session][ERROR] onLoginStatusChanged 未知异常 loggedIn=" << loggedIn;
    }
}

void SessionManager::onLoginSucceeded(const QString &token, const QJsonObject &userInfo)
{
    try {
        m_hadSuccessfulLogin = true;
        const bool isTestToken = token.startsWith(QStringLiteral("test_token_"));
        qDebug().noquote() << "[Client][Session] loginSucceeded testToken=" << isTestToken
                           << " tokenLen=" << token.size()
                           << " user=" << userInfo.value(QStringLiteral("username")).toString();

        if (m_fsm) {
            try {
                m_fsm->fire(SystemStateMachine::Trigger::AUTH_SUCCESS);
            } catch (const std::exception& e) {
                qCritical() << "[Client][Session][ERROR] fire(AUTH_SUCCESS) 异常:" << e.what();
            } catch (...) {
                qCritical() << "[Client][Session][ERROR] fire(AUTH_SUCCESS) 未知异常";
            }
        }

        if (!m_vehicles)
            return;

        if (isTestToken) {
            try {
                m_vehicles->addTestVehicle(QStringLiteral("123456789"), QStringLiteral("测试车辆"));
            } catch (const std::exception& e) {
                qCritical() << "[Client][Session][ERROR] addTestVehicle 异常:" << e.what();
            } catch (...) {
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
        } catch (const std::exception& e) {
            qCritical() << "[Client][Session][ERROR] loadVehicleList 异常:" << e.what();
        } catch (...) {
            qCritical() << "[Client][Session][ERROR] loadVehicleList 未知异常";
        }
    } catch (const std::exception& e) {
        qCritical() << "[Client][Session][ERROR] onLoginSucceeded 总异常:" << e.what();
    } catch (...) {
        qCritical() << "[Client][Session][ERROR] onLoginSucceeded 未知异常";
    }
}

void SessionManager::onLogout()
{
    try {
        qInfo().noquote() << "[Client][Session] onLogout → disconnectAll 四路 WebRTC";
        try {
            if (m_webrtc)
                m_webrtc->disconnectAll();
        } catch (const std::exception& e) {
            qCritical() << "[Client][Session][ERROR] disconnectAll 异常:" << e.what();
        }
        if (m_fsm) {
            try {
                m_fsm->fire(SystemStateMachine::Trigger::LOGOUT);
            } catch (const std::exception& e) {
                qCritical() << "[Client][Session][ERROR] fire(LOGOUT) 异常:" << e.what();
            }
        }
        m_hadSuccessfulLogin = false;
    } catch (const std::exception& e) {
        qCritical() << "[Client][Session][ERROR] onLogout 总异常:" << e.what();
    } catch (...) {
        qCritical() << "[Client][Session][ERROR] onLogout 未知异常";
    }
}

void SessionManager::onVinSelected(const QString &vin)
{
    try {
        m_selectedVin = vin;
        if (vin.isEmpty()) {
            qInfo().noquote() << "[Client][Session] VIN 已清空 → disconnectAll 四路";
            if (m_webrtc) {
                try {
                    m_webrtc->setCurrentVin(QString());
                    m_webrtc->disconnectAll();
                } catch (const std::exception& e) {
                    qCritical() << "[Client][Session][ERROR] VIN 清空时 disconnectAll 异常:" << e.what();
                }
            }
            return;
        }
        qInfo().noquote() << "[Client][Session] VIN 已选中 vin=" << vin
                          << " → setCurrentVin + connectFourStreams";
        if (m_webrtc) {
            try {
                m_webrtc->setCurrentVin(vin);
                m_webrtc->connectFourStreams();
            } catch (const std::exception& e) {
                qCritical() << "[Client][Session][ERROR] connectFourStreams 异常:" << e.what();
            } catch (...) {
                qCritical() << "[Client][Session][ERROR] connectFourStreams 未知异常";
            }
        }
    } catch (const std::exception& e) {
        qCritical() << "[Client][Session][ERROR] onVinSelected 总异常 vin=" << vin << " error=" << e.what();
    } catch (...) {
        qCritical() << "[Client][Session][ERROR] onVinSelected 未知异常 vin=" << vin;
    }
}

void SessionManager::onSessionCreated(const QString &sessionId, const QString &whipUrl,
                                      const QString &whepUrl, const QJsonObject &controlConfig)
{
    try {
        Q_UNUSED(whipUrl)
        Q_UNUSED(controlConfig)
        qInfo().noquote() << "[Client][Session] onSessionCreated sessionId=" << sessionId
                          << " vin=" << m_selectedVin
                          << " whepUrl=" << (whepUrl.length() > 80 ? whepUrl.left(80) + "..." : whepUrl);
        if (whepUrl.isEmpty()) {
            qWarning().noquote() << "[Client][Session] onSessionCreated: whepUrl 为空，跳过重连";
            return;
        }
        if (m_webrtc) {
            try {
                m_webrtc->connectFourStreams(whepUrl);
            } catch (const std::exception& e) {
                qCritical() << "[Client][Session][ERROR] connectFourStreams(whepUrl) 异常:" << e.what();
            } catch (...) {
                qCritical() << "[Client][Session][ERROR] connectFourStreams(whepUrl) 未知异常";
            }
        }
    } catch (const std::exception& e) {
        qCritical() << "[Client][Session][ERROR] onSessionCreated 总异常 sessionId=" << sessionId
                    << " error=" << e.what();
    } catch (...) {
        qCritical() << "[Client][Session][ERROR] onSessionCreated 未知异常 sessionId=" << sessionId;
    }
}
