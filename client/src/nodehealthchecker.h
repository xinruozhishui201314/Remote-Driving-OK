#ifndef NODEHEALTHCHECKER_H
#define NODEHEALTHCHECKER_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

/**
 * @brief 节点健康检测器
 * 根据服务器地址检测 Backend、Keycloak、ZLM 等节点是否可达，供登录界面展示
 */
class NodeHealthChecker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString backendStatus READ backendStatus NOTIFY backendStatusChanged)
    Q_PROPERTY(QString backendMessage READ backendMessage NOTIFY backendMessageChanged)
    Q_PROPERTY(QString keycloakStatus READ keycloakStatus NOTIFY keycloakStatusChanged)
    Q_PROPERTY(QString keycloakMessage READ keycloakMessage NOTIFY keycloakMessageChanged)
    Q_PROPERTY(QString zlmStatus READ zlmStatus NOTIFY zlmStatusChanged)
    Q_PROPERTY(QString zlmMessage READ zlmMessage NOTIFY zlmMessageChanged)
    Q_PROPERTY(bool isChecking READ isChecking NOTIFY isCheckingChanged)

public:
    explicit NodeHealthChecker(QObject *parent = nullptr);

    QString backendStatus() const { return m_backendStatus; }
    QString backendMessage() const { return m_backendMessage; }
    QString keycloakStatus() const { return m_keycloakStatus; }
    QString keycloakMessage() const { return m_keycloakMessage; }
    QString zlmStatus() const { return m_zlmStatus; }
    QString zlmMessage() const { return m_zlmMessage; }
    bool isChecking() const { return m_isChecking; }

    /** 根据 serverUrl 检测各节点，结果通过属性与信号更新 */
    Q_INVOKABLE void refresh(const QString &serverUrl);

signals:
    void backendStatusChanged();
    void backendMessageChanged();
    void keycloakStatusChanged();
    void keycloakMessageChanged();
    void zlmStatusChanged();
    void zlmMessageChanged();
    void isCheckingChanged();
    /** 全部检测完成（无论成功失败） */
    void checkFinished();

private:
    void setBackendStatus(const QString &status, const QString &message);
    void setKeycloakStatus(const QString &status, const QString &message);
    void setZlmStatus(const QString &status, const QString &message);
    void setChecking(bool v);
    void onBackendFinished();
    void onKeycloakFinished();
    void onZlmFinished();
    void tryFinish();

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_backendReply = nullptr;
    QNetworkReply *m_keycloakReply = nullptr;
    QNetworkReply *m_zlmReply = nullptr;

    QString m_backendStatus;
    QString m_backendMessage;
    QString m_keycloakStatus;
    QString m_keycloakMessage;
    QString m_zlmStatus;
    QString m_zlmMessage;
    bool m_isChecking = false;
    int m_pendingCount = 0;
};

#endif // NODEHEALTHCHECKER_H
