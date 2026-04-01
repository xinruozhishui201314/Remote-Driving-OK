#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

/**
 * @brief 认证管理器
 * 处理用户登录、认证令牌管理
 */
class AuthManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString username READ username NOTIFY usernameChanged)
    Q_PROPERTY(bool isLoggedIn READ isLoggedIn NOTIFY loginStatusChanged)
    Q_PROPERTY(QString authToken READ authToken NOTIFY authTokenChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(QStringList usernameHistory READ usernameHistory NOTIFY usernameHistoryChanged)

public:
    explicit AuthManager(QObject *parent = nullptr);
    /** 构造函数，可选择是否加载保存的凭据 */
    explicit AuthManager(QObject *parent, bool loadSavedCredentials);
    ~AuthManager();

    QString username() const { return m_username; }
    bool isLoggedIn() const { return m_isLoggedIn; }
    QString authToken() const { return m_authToken; }
    QString serverUrl() const { return m_serverUrl; }
    QStringList usernameHistory() const { return m_usernameHistory; }

    /** 登录成功后由 UI 调用，将账户名加入历史并持久化（最多保留 10 条） */
    Q_INVOKABLE void addUsernameToHistory(const QString &username);

public slots:
    void login(const QString &username, const QString &password, const QString &serverUrl);
    void logout();
    void refreshToken();
    /** 清除所有保存的登录凭据（用于测试/重置） */
    void clearCredentials();

signals:
    void usernameChanged(const QString &username);
    void loginStatusChanged(bool loggedIn);
    void authTokenChanged(const QString &token);
    void serverUrlChanged(const QString &serverUrl);
    void usernameHistoryChanged();
    void loginSucceeded(const QString &token, const QJsonObject &userInfo);
    void loginFailed(const QString &error);
    void tokenRefreshed(const QString &token);

private slots:
    void onLoginReply(QNetworkReply *reply);
    void onRefreshReply(QNetworkReply *reply);

private:
    void updateLoginStatus(bool loggedIn, const QString &username = QString(), const QString &token = QString());
    void saveCredentials();
    void loadCredentials();

    QString m_username;
    QStringList m_usernameHistory;
    QString m_authToken;
    QString m_serverUrl;
    bool m_isLoggedIn = false;
    QString m_pendingUsername;  // 登录请求发出时的用户名，用于 Keycloak 回调里填充 userInfo

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;   // 登录请求 reply
    QNetworkReply *m_refreshReply = nullptr;   // token 刷新 reply（独立，避免与登录 reply 竞态）
};

#endif // AUTHMANAGER_H
