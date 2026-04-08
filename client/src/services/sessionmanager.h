#ifndef CLIENT_SERVICES_SESSIONMANAGER_H
#define CLIENT_SERVICES_SESSIONMANAGER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QAtomicInt>

class AuthManager;
class VehicleManager;
class MqttController;
class WebRtcStreamManager;
class SystemStateMachine;
class VehicleControlService;

/**
 * 会话编排：登录后加载车辆、VIN 切换、与状态机联动（对应架构文档 SessionManager）。
 */
class SessionManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasError READ hasError NOTIFY errorOccurred)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)

public:
    explicit SessionManager(AuthManager *auth,
                            VehicleManager *vehicles,
                            MqttController *mqtt,
                            WebRtcStreamManager *webrtc,
                            SystemStateMachine *fsm,
                            QObject *parent = nullptr);

    AuthManager *authManager() const { return m_auth; }
    VehicleManager *vehicleManager() const { return m_vehicles; }

    void setVehicleControl(VehicleControlService *vcs);

    bool hasError() const { return m_hasError.loadRelaxed(); }
    QString lastError() const { return m_lastError; }

signals:
    void errorOccurred(const QString &error);

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
    void setError(const QString &error);

    AuthManager *m_auth = nullptr;
    VehicleManager *m_vehicles = nullptr;
    MqttController *m_mqtt = nullptr;
    WebRtcStreamManager *m_webrtc = nullptr;
    SystemStateMachine *m_fsm = nullptr;
    VehicleControlService *m_vehicleControl = nullptr;
    bool m_hadSuccessfulLogin = false;
    QString m_selectedVin;  // onVinSelected 缓存，供 onSessionCreated 使用
    QAtomicInt m_hasError{0};
    QString m_lastError;
};

#endif // CLIENT_SERVICES_SESSIONMANAGER_H
