#ifndef CLIENT_SERVICES_SESSIONMANAGER_H
#define CLIENT_SERVICES_SESSIONMANAGER_H

#include <QObject>
#include <QString>
#include <QJsonObject>

class AuthManager;
class VehicleManager;
class MqttController;
class WebRtcStreamManager;
class SystemStateMachine;

/**
 * 会话编排：登录后加载车辆、VIN 切换、与状态机联动（对应架构文档 SessionManager）。
 */
class SessionManager : public QObject
{
    Q_OBJECT

public:
    explicit SessionManager(AuthManager *auth,
                            VehicleManager *vehicles,
                            MqttController *mqtt,
                            WebRtcStreamManager *webrtc,
                            SystemStateMachine *fsm,
                            QObject *parent = nullptr);

    AuthManager *authManager() const { return m_auth; }
    VehicleManager *vehicleManager() const { return m_vehicles; }

public slots:
    void onLoginSucceeded(const QString &token, const QJsonObject &userInfo);
    void onLogout();
    /** 与 AuthManager::loginStatusChanged 绑定：检测 true→false 时 onLogout */
    void onLoginStatusChanged(bool loggedIn);
    /**
     * VIN 切换时设置流管理器 VIN 并发起初始连接（使用 ZLM_VIDEO_URL 回退）。
     * 后续 onSessionCreated 会以正确 whepUrl 重连。
     * 与 VehicleManager::currentVinChanged 绑定。
     */
    void onVinSelected(const QString &vin);
    /**
     * 会话创建成功后用正确 whepUrl 重连四路流。
     * 与 VehicleManager::sessionCreated 绑定，确保 base host 与流名均正确。
     */
    void onSessionCreated(const QString &sessionId, const QString &whipUrl,
                          const QString &whepUrl, const QJsonObject &controlConfig);

private:
    AuthManager *m_auth = nullptr;
    VehicleManager *m_vehicles = nullptr;
    MqttController *m_mqtt = nullptr;
    WebRtcStreamManager *m_webrtc = nullptr;
    SystemStateMachine *m_fsm = nullptr;
    bool m_hadSuccessfulLogin = false;
    QString m_selectedVin;  // onVinSelected 缓存，供 onSessionCreated 使用
};

#endif // CLIENT_SERVICES_SESSIONMANAGER_H
