#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

class KeystoreManager;

/**
 * @brief 认证管理器
 * 处理用户登录、认证令牌管理
 *
 * 安全增强（Phase2 Task 8）：
 * - 使用 KeystoreManager 安全存储 JWT 令牌
 * - 支持令牌过期检测和自动刷新
 * - 支持 refreshToken 安全存储
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

    // ═══════════════════════════════════════════════════════════════
    // JWT 安全存储相关（Phase2 Task 8）
    // ═══════════════════════════════════════════════════════════════

    /** 获取 refresh token（从安全存储） */
    QString getRefreshToken() const;

    /** 获取令牌过期时间（秒） */
    qint64 getTokenExpiresIn() const { return m_tokenExpiresIn; }

    /** 检查令牌是否即将过期（剩余时间 < threshold） */
    bool isTokenExpiringSoon(int thresholdSeconds = 60) const;

    /** 强制刷新令牌 */
    Q_INVOKABLE void forceRefreshToken();

signals:
    void usernameChanged(const QString &username);
    void loginStatusChanged(bool loggedIn);
    void authTokenChanged(const QString &token);
    void serverUrlChanged(const QString &serverUrl);
    void usernameHistoryChanged();
    void loginSucceeded(const QString &token, const QJsonObject &userInfo);
    void loginFailed(const QString &error);
    void tokenRefreshed(const QString &token);
    // 安全存储信号
    void tokenExpired();
    void secureStorageError(const QString &error);

public slots:
    void login(const QString &username, const QString &password, const QString &serverUrl);
    void logout();
    void refreshToken();
    /** 清除所有保存的登录凭据（用于测试/重置） */
    void clearCredentials();

private slots:
    void onLoginReply(QNetworkReply *reply);
    void onRefreshReply(QNetworkReply *reply);
    // 定时检查令牌过期
    void onTokenExpiryCheck();

private:
    void updateLoginStatus(bool loggedIn, const QString &username = QString(), const QString &token = QString());
    void saveCredentials();
    void loadCredentials();

    // JWT 安全存储
    void storeTokensSecurely(const QString &accessToken, const QString &refreshToken, qint64 expiresIn);
    void loadTokensFromSecureStorage();
    void deleteTokensFromSecureStorage();
    qint64 decodeTokenExp(const QString &token) const;

    QString m_username;
    QStringList m_usernameHistory;
    QString m_authToken;
    QString m_serverUrl;
    bool m_isLoggedIn = false;
    QString m_pendingUsername;  // 登录请求发出时的用户名，用于 Keycloak 回调里填充 userInfo

    // ═══════════════════════════════════════════════════════════════
    // JWT 相关字段（Phase2 Task 8）
    // ═══════════════════════════════════════════════════════════════
    qint64 m_tokenExpiresIn = 0;           // 令牌过期时间戳
    qint64 m_tokenIssuedAt = 0;            // 令牌发放时间
    QString m_refreshToken;                // Refresh token（安全存储）
    KeystoreManager* m_keystore = nullptr; // 安全存储管理器
    QTimer* m_tokenExpiryTimer = nullptr;   // 令牌过期检查定时器

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;   // 登录请求 reply
    QNetworkReply *m_refreshReply = nullptr;   // token 刷新 reply（独立，避免与登录 reply 竞态）

    // JWT 安全存储的 key 前缀
    static constexpr const char* KEY_ACCESS_TOKEN = "access_token";
    static constexpr const char* KEY_REFRESH_TOKEN = "refresh_token";
    static constexpr const char* KEY_TOKEN_EXPIRY = "token_expiry";
    static constexpr const char* KEY_TOKEN_ISSUED = "token_issued";
};

#endif // AUTHMANAGER_H