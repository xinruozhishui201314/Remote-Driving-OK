#include "authmanager.h"
#include "security/keystoremanager.h"
#include "core/logger.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QDateTime>
#include <QUrl>
#include <QCryptographicHash>
#include <QRegularExpression>

// JWT payload 解析辅助函数
static QJsonObject parseJwtPayload(const QString &token)
{
    // JWT 格式: header.payload.signature
    QStringList parts = token.split('.');
    if (parts.size() < 2) {
        return QJsonObject();
    }

    // Base64Url 解码
    QString payloadStr = parts[1];
    // 替换 URL 安全字符
    payloadStr.replace('-', '+');
    payloadStr.replace('_', '/');

    // 添加 padding
    int padding = (4 - payloadStr.size() % 4) % 4;
    payloadStr.append(QString(padding, '='));

    QByteArray payloadBytes = QByteArray::fromBase64(payloadStr.toUtf8());
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(payloadBytes, &error);

    if (error.error != QJsonParseError::NoError) {
        qWarning() << "[AuthManager] JWT payload 解析失败:" << error.errorString();
        return QJsonObject();
    }

    return doc.object();
}

AuthManager::AuthManager(QObject *parent)
    : AuthManager(parent, true)  // 默认加载保存的凭据
{
}

AuthManager::AuthManager(QObject *parent, bool loadSavedCredentials)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_keystore(&KeystoreManager::instance())
    , m_tokenExpiryTimer(new QTimer(this))
{
    // 设置令牌过期检查定时器（每 30 秒检查一次）
    m_tokenExpiryTimer->setInterval(30000);
    connect(m_tokenExpiryTimer, &QTimer::timeout, this, &AuthManager::onTokenExpiryCheck);

    if (loadSavedCredentials) {
        loadCredentials();
    } else {
        qDebug() << "AuthManager: Skipping loadCredentials (reset mode)";
    }
}

AuthManager::~AuthManager()
{
    m_tokenExpiryTimer->stop();
    saveCredentials();
}

void AuthManager::login(const QString &username, const QString &password, const QString &serverUrl)
{
    qDebug().noquote() << "[Client][Auth] 发起登录: username=" << username << " serverUrl=" << serverUrl;
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }

    m_serverUrl = serverUrl;
    emit serverUrlChanged(m_serverUrl);

#ifndef QT_DEBUG
    // 仅 Debug 构建允许测试账号，生产构建（Release）此块被编译器裁剪
    Q_UNUSED(username); Q_UNUSED(password); // 占位，避免下方 if 消失导致引用未定义
#else
    // 测试模式：如果用户名和密码都是 "123"，直接模拟登录成功（仅 Debug 构建可用）
    if (username == QLatin1String("123") && password == QLatin1String("123")) {
        qDebug().noquote() << "[Client][Auth] 测试模式: 模拟登录成功 (123/123) [DEBUG BUILD ONLY]";
        QString testToken = QStringLiteral("test_token_") + QString::number(QDateTime::currentMSecsSinceEpoch());
        QJsonObject testUserInfo;
        testUserInfo[QStringLiteral("username")] = username;
        testUserInfo[QStringLiteral("id")] = 1;
        updateLoginStatus(true, username, testToken);
        // 模拟令牌 5 分钟后过期
        storeTokensSecurely(testToken, QString(), 300);
        saveCredentials();
        emit loginSucceeded(testToken, testUserInfo);
        return;
    }
#endif

    // 使用 Keycloak token 接口（后端无 /api/auth/login，需直接向 Keycloak 取 JWT）
    // 宿主机: backend 多为 8081、Keycloak 8080 → 同 host 端口改为 8080
    // 容器内: backend 为 backend:8080、Keycloak 为 keycloak:8080 → host 改为 keycloak
    QUrl baseUrl(serverUrl);
    if (baseUrl.host() == QLatin1String("backend")) {
        baseUrl.setHost(QLatin1String("keycloak"));
        baseUrl.setPort(8080);
    } else if (baseUrl.port() == 8081 || baseUrl.port() <= 0) {
        baseUrl.setPort(8080);
    }
    QString keycloakBase = baseUrl.toString();
    if (!keycloakBase.endsWith(QLatin1Char('/')))
        keycloakBase += QLatin1Char('/');
    QUrl loginUrl(keycloakBase + QStringLiteral("realms/teleop/protocol/openid-connect/token"));
    qDebug().noquote() << "[Client][Auth] Keycloak token URL:" << loginUrl.toString();

    QByteArray formBody;
    formBody.append("grant_type=password");
    formBody.append("&client_id=teleop-client");
    formBody.append("&username=" + QUrl::toPercentEncoding(username));
    formBody.append("&password=" + QUrl::toPercentEncoding(password));

    QNetworkRequest request(loginUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(formBody.size()));

    m_pendingUsername = username;
    m_currentReply = m_networkManager->post(request, formBody);
    qDebug().noquote() << "[Client][Auth] POST 已发出 url=" << loginUrl.toString();

    connect(m_currentReply, &QNetworkReply::finished, this, [this]() {
        onLoginReply(m_currentReply);
    });
    connect(m_currentReply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError error) {
        QNetworkReply *reply = m_currentReply;
        QString errStr = reply ? reply->errorString() : QString();
        qDebug().noquote() << "[Client][Auth] errorOccurred: code=" << static_cast<int>(error) << " errorString=" << errStr << " url=" << (reply && reply->url().isValid() ? reply->url().toString() : QString());
        emit loginFailed(tr("网络错误: %1").arg(errStr));
        if (reply) {
            reply->deleteLater();
            m_currentReply = nullptr;
        }
    });
}

void AuthManager::onLoginReply(QNetworkReply *reply)
{
    if (m_currentReply == reply)
        m_currentReply = nullptr;

    if (!reply) {
        // Qt 会先发 errorOccurred 再发 finished；errorOccurred 里已把 m_currentReply 置空并 emit 了真实错误，此处不再重复 emit 避免覆盖为「无响应」
        qDebug().noquote() << "[Client][Auth] onLoginReply: reply=null（已由 errorOccurred 处理，不再重复 emit）";
        return;
    }
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        QString err = reply->errorString();
        qDebug().noquote() << "[Client][Auth] onLoginReply: HTTP 错误 statusCode=" << statusCode << " error=" << err << " → loginFailed";
        emit loginFailed(tr("登录失败: %1").arg(err));
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    qDebug().noquote() << "[Client][Auth] onLoginReply: HTTP" << statusCode << " bodySize=" << data.size();

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        qDebug().noquote() << "[Client][Auth] onLoginReply: 响应非 JSON offset=" << parseErr.offset << " snippet=" << QString::fromUtf8(data.left(150));
        emit loginFailed(tr("服务器响应格式错误"));
        return;
    }

    QJsonObject json = doc.object();

    if (json.contains("error")) {
        QString desc = json.contains("error_description") ? json["error_description"].toString() : json["error"].toString();
        qDebug().noquote() << "[Client][Auth] onLoginReply: Keycloak error=" << json["error"].toString() << " description=" << desc << " → loginFailed";
        emit loginFailed(desc.isEmpty() ? tr("认证失败") : desc);
        return;
    }

    QString token = json["access_token"].toString();
    if (token.isEmpty())
        token = json["token"].toString();
    if (token.isEmpty() && json.contains("data")) {
        QJsonObject dataObj = json["data"].toObject();
        token = dataObj["token"].toString();
        if (token.isEmpty())
            token = dataObj["access_token"].toString();
    }
    if (token.isEmpty()) {
        qDebug().noquote() << "[Client][Auth] onLoginReply: 响应中无 access_token/token keys=" << json.keys().join(QLatin1Char(',')) << " → loginFailed(响应中无令牌)";
        emit loginFailed(tr("响应中无令牌"));
        return;
    }

    // ═══════════════════════════════════════════════════════════════
    // JWT 安全存储（Phase2 Task 8）
    // ═══════════════════════════════════════════════════════════════
    QString refreshToken = json["refresh_token"].toString();
    qint64 expiresIn = json["expires_in"].toInt(300);  // 默认 5 分钟
    qDebug().noquote() << "[Client][Auth] 收到令牌: expiresIn=" << expiresIn << "s, hasRefreshToken=" << !refreshToken.isEmpty();

    // 解析 JWT payload 获取实际过期时间
    QJsonObject jwtPayload = parseJwtPayload(token);
    if (!jwtPayload.isEmpty()) {
        // JWT 标准的 exp 字段是 Unix 时间戳（秒）
        if (jwtPayload.contains("exp")) {
            m_tokenExpiresIn = jwtPayload["exp"].toVariant().toLongLong();
            m_tokenIssuedAt = jwtPayload["iat"].toVariant().toLongLong();
            qDebug().noquote() << "[Client][Auth] JWT exp=" << m_tokenExpiresIn << " iat=" << m_tokenIssuedAt;
        }
    }

    // 安全存储令牌
    storeTokensSecurely(token, refreshToken, expiresIn);

    QString username = m_pendingUsername;
    if (username.isEmpty() && json.contains("preferred_username"))
        username = json["preferred_username"].toString();
    QJsonObject userInfo;
    userInfo["username"] = username;

    updateLoginStatus(true, username, token);
    saveCredentials();

    // 启动令牌过期检查
    m_tokenExpiryTimer->start();

    qDebug().noquote() << "[Client][Auth] onLoginReply: 登录成功 username=" << username << " tokenLen=" << token.size() << " → emit loginSucceeded";
    emit loginSucceeded(token, userInfo);
}

void AuthManager::logout()
{
    QString prevUser = m_username;
    qDebug().noquote() << "[Client][Auth] 退出登录: username=" << (prevUser.isEmpty() ? "(空)" : prevUser);

    // 停止令牌过期检查
    m_tokenExpiryTimer->stop();

    updateLoginStatus(false);
    m_authToken.clear();
    m_username.clear();

    // 安全删除存储的令牌
    deleteTokensFromSecureStorage();

    saveCredentials();
    emit loginStatusChanged(false);
    qDebug().noquote() << "[Client][Auth] 已清除 token/username 并保存; 当前 isLoggedIn=false";
}

void AuthManager::refreshToken()
{
    if (!m_isLoggedIn || m_authToken.isEmpty()) {
        qDebug().noquote() << "[Client][Auth] refreshToken: 未登录，跳过刷新";
        emit tokenRefreshed(QString());
        return;
    }

    // 检查令牌是否真正需要刷新
    if (!isTokenExpiringSoon(60)) {
        qDebug().noquote() << "[Client][Auth] refreshToken: 令牌尚未过期，跳过刷新";
        emit tokenRefreshed(m_authToken);
        return;
    }

    // 如果没有 refresh token 且令牌还未过期，尝试刷新
    if (m_refreshToken.isEmpty()) {
        qDebug().noquote() << "[Client][Auth] refreshToken: 无 refresh token，触发重新登录";
        logout();
        return;
    }

    qDebug().noquote() << "[Client][Auth] 刷新令牌中...";

    // abort 上一轮 refresh（防止并发；不影响登录 reply m_currentReply）
    if (m_refreshReply) {
        m_refreshReply->abort();
        m_refreshReply->deleteLater();
        m_refreshReply = nullptr;
    }

    // 使用 Keycloak refresh_token 端点
    QUrl baseUrl(m_serverUrl);
    if (baseUrl.host() == QLatin1String("backend")) {
        baseUrl.setHost(QLatin1String("keycloak"));
        baseUrl.setPort(8080);
    } else if (baseUrl.port() == 8081 || baseUrl.port() <= 0) {
        baseUrl.setPort(8080);
    }
    QString keycloakBase = baseUrl.toString();
    if (!keycloakBase.endsWith(QLatin1Char('/')))
        keycloakBase += QLatin1Char('/');
    QUrl refreshUrl(keycloakBase + QStringLiteral("realms/teleop/protocol/openid-connect/token"));

    QByteArray formBody;
    formBody.append("grant_type=refresh_token");
    formBody.append("&client_id=teleop-client");
    formBody.append("&refresh_token=" + QUrl::toPercentEncoding(m_refreshToken));

    QNetworkRequest request(refreshUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(formBody.size()));

    m_refreshReply = m_networkManager->post(request, formBody);

    connect(m_refreshReply, &QNetworkReply::finished, this, [this]() {
        onRefreshReply(m_refreshReply);
    });
    connect(m_refreshReply, &QNetworkReply::errorOccurred, this,
            [this](QNetworkReply::NetworkError err) {
        Q_UNUSED(err)
        QNetworkReply *reply = m_refreshReply;
        const QString errStr = reply ? reply->errorString() : QString();
        qWarning().noquote() << "[Client][Auth] refreshToken 网络错误:" << errStr;
        emit tokenRefreshed(QString());
        if (reply) {
            reply->deleteLater();
            m_refreshReply = nullptr;
        }
    });
}

void AuthManager::forceRefreshToken()
{
    qDebug().noquote() << "[Client][Auth] forceRefreshToken: 强制刷新令牌";
    if (!m_isLoggedIn || m_refreshToken.isEmpty()) {
        qWarning().noquote() << "[Client][Auth] forceRefreshToken: 无法强制刷新（未登录或无 refresh token）";
        logout();
        return;
    }

    // 中止现有请求
    if (m_refreshReply) {
        m_refreshReply->abort();
        m_refreshReply->deleteLater();
        m_refreshReply = nullptr;
    }

    // 使用 Keycloak refresh_token 端点
    QUrl baseUrl(m_serverUrl);
    if (baseUrl.host() == QLatin1String("backend")) {
        baseUrl.setHost(QLatin1String("keycloak"));
        baseUrl.setPort(8080);
    } else if (baseUrl.port() == 8081 || baseUrl.port() <= 0) {
        baseUrl.setPort(8080);
    }
    QString keycloakBase = baseUrl.toString();
    if (!keycloakBase.endsWith(QLatin1Char('/')))
        keycloakBase += QLatin1Char('/');
    QUrl refreshUrl(keycloakBase + QStringLiteral("realms/teleop/protocol/openid-connect/token"));

    QByteArray formBody;
    formBody.append("grant_type=refresh_token");
    formBody.append("&client_id=teleop-client");
    formBody.append("&refresh_token=" + QUrl::toPercentEncoding(m_refreshToken));

    QNetworkRequest request(refreshUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(formBody.size()));

    m_refreshReply = m_networkManager->post(request, formBody);

    connect(m_refreshReply, &QNetworkReply::finished, this, [this]() {
        onRefreshReply(m_refreshReply);
    });
    connect(m_refreshReply, &QNetworkReply::errorOccurred, this,
            [this](QNetworkReply::NetworkError err) {
        Q_UNUSED(err)
        QNetworkReply *reply = m_refreshReply;
        const QString errStr = reply ? reply->errorString() : QString();
        qWarning().noquote() << "[Client][Auth] forceRefreshToken 网络错误:" << errStr;
        if (reply) {
            reply->deleteLater();
            m_refreshReply = nullptr;
        }
    });
}

void AuthManager::onRefreshReply(QNetworkReply *reply)
{
    if (!reply || reply != m_refreshReply) {
        if (reply) reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qWarning().noquote() << "[Client][Auth] token 刷新失败，触发重新登录 error=" << reply->errorString();
        reply->deleteLater();
        m_refreshReply = nullptr;
        logout();
        return;
    }

    QByteArray data = reply->readAll();
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error == QJsonParseError::NoError) {
        QJsonObject json = doc.object();
        QString token = json.contains(QStringLiteral("access_token"))
            ? json[QStringLiteral("access_token")].toString()
            : json[QStringLiteral("token")].toString();

        if (!token.isEmpty()) {
            QString newRefreshToken = json["refresh_token"].toString();
            qint64 expiresIn = json["expires_in"].toInt(300);

            m_authToken = token;
            if (!newRefreshToken.isEmpty()) {
                m_refreshToken = newRefreshToken;
            }

            // 解析 JWT 获取过期时间
            QJsonObject jwtPayload = parseJwtPayload(token);
            if (!jwtPayload.isEmpty()) {
                m_tokenExpiresIn = jwtPayload["exp"].toVariant().toLongLong();
                m_tokenIssuedAt = jwtPayload["iat"].toVariant().toLongLong();
            }

            // 安全存储新令牌
            storeTokensSecurely(token, m_refreshToken, expiresIn);

            emit authTokenChanged(m_authToken);
            emit tokenRefreshed(m_authToken);
            saveCredentials();

            qDebug().noquote() << "[Client][Auth] 令牌刷新成功";
        } else {
            qWarning().noquote() << "[Client][Auth] token 刷新响应中无有效 token，触发重新登录";
            reply->deleteLater();
            m_refreshReply = nullptr;
            logout();
            return;
        }
    } else {
        qWarning().noquote() << "[Client][Auth] token 刷新响应 JSON 解析失败:" << error.errorString();
    }

    reply->deleteLater();
    m_refreshReply = nullptr;
}

void AuthManager::onTokenExpiryCheck()
{
    if (!m_isLoggedIn || m_authToken.isEmpty()) {
        return;
    }

    qint64 now = QDateTime::currentSecsSinceEpoch();

    // 检查令牌是否已过期
    if (m_tokenExpiresIn > 0 && now >= m_tokenExpiresIn) {
        qWarning().noquote() << "[Client][Auth] 令牌已过期，触发过期信号";
        emit tokenExpired();

        // 尝试使用 refresh token 刷新
        if (!m_refreshToken.isEmpty()) {
            forceRefreshToken();
        } else {
            logout();
        }
        return;
    }

    // 检查令牌是否即将过期（5 分钟内）
    if (isTokenExpiringSoon(300) && !m_refreshToken.isEmpty()) {
        qDebug().noquote() << "[Client][Auth] 令牌即将过期（剩余" << (m_tokenExpiresIn - now) << "秒），自动刷新";
        forceRefreshToken();
    }
}

bool AuthManager::isTokenExpiringSoon(int thresholdSeconds) const
{
    if (m_tokenExpiresIn <= 0) {
        return false;  // 无法确定过期时间
    }

    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 remaining = m_tokenExpiresIn - now;
    return remaining < thresholdSeconds;
}

QString AuthManager::getRefreshToken() const
{
    return m_refreshToken;
}

void AuthManager::updateLoginStatus(bool loggedIn, const QString &username, const QString &token)
{
    bool wasLoggedIn = m_isLoggedIn;
    m_isLoggedIn = loggedIn;

    if (loggedIn) {
        if (!username.isEmpty()) {
            m_username = username;
            emit usernameChanged(m_username);
        }
        if (!token.isEmpty()) {
            m_authToken = token;
            emit authTokenChanged(m_authToken);
        }
        if (!wasLoggedIn)
            qDebug().noquote() << "[Client][Auth] 登录状态更新: isLoggedIn=true username=" << m_username;
    } else {
        if (wasLoggedIn)
            qDebug().noquote() << "[Client][Auth] 登录状态更新: isLoggedIn=false (已登出)";
    }

    emit loginStatusChanged(m_isLoggedIn);
}

void AuthManager::saveCredentials()
{
    // 保存非敏感信息到 QSettings
    QSettings settings;
    settings.setValue("auth/username", m_username);
    settings.setValue("auth/loggedIn", m_isLoggedIn);
    settings.setValue("auth/serverUrl", m_serverUrl);
    settings.setValue("auth/usernameHistory", m_usernameHistory);

    // Token 已经通过 KeystoreManager 安全存储
    qDebug().noquote() << "[Client][Auth] saveCredentials: 保存非敏感信息完成";
}

void AuthManager::addUsernameToHistory(const QString &username)
{
    QString u = username.trimmed();
    if (u.isEmpty()) {
        qDebug().noquote() << "[Client][Auth] addUsernameToHistory: 跳过空账户名";
        return;
    }
    m_usernameHistory.removeAll(u);
    m_usernameHistory.prepend(u);
    while (m_usernameHistory.size() > 10)
        m_usernameHistory.removeLast();
    QSettings settings;
    settings.setValue("auth/usernameHistory", m_usernameHistory);
    qDebug().noquote() << "[Client][Auth] 保存账户名历史 count=" << m_usernameHistory.size() << " 新增=" << u;
    emit usernameHistoryChanged();
}

void AuthManager::loadCredentials()
{
    QSettings settings;
    m_username = settings.value("auth/username").toString();
    m_isLoggedIn = settings.value("auth/loggedIn", false).toBool();
    m_serverUrl = settings.value("auth/serverUrl").toString();
    m_usernameHistory = settings.value("auth/usernameHistory").toStringList();

    // 从安全存储加载令牌
    loadTokensFromSecureStorage();

    qDebug().noquote() << "[Client][Auth] loadCredentials: username=" << (m_username.isEmpty() ? "(空)" : m_username)
                      << " usernameHistory.count=" << m_usernameHistory.size()
                      << " tokenLoaded=" << !m_authToken.isEmpty();

    if (m_isLoggedIn && !m_authToken.isEmpty()) {
        emit usernameChanged(m_username);
        emit authTokenChanged(m_authToken);
        emit serverUrlChanged(m_serverUrl);
        emit loginStatusChanged(true);

        // 启动令牌过期检查
        m_tokenExpiryTimer->start();
    }
    if (!m_usernameHistory.isEmpty())
        emit usernameHistoryChanged();
}

void AuthManager::clearCredentials()
{
    qDebug() << "AuthManager: Clearing saved credentials";

    // 停止令牌过期检查
    m_tokenExpiryTimer->stop();

    // 清除内存中的状态
    m_username.clear();
    m_authToken.clear();
    m_serverUrl.clear();
    m_isLoggedIn = false;
    m_tokenExpiresIn = 0;
    m_tokenIssuedAt = 0;
    m_refreshToken.clear();

    // 安全删除存储的令牌
    deleteTokensFromSecureStorage();

    // 清除 QSettings 中的保存值
    QSettings settings;
    settings.remove("auth/username");
    settings.remove("auth/loggedIn");
    settings.remove("auth/serverUrl");
    settings.remove("auth/usernameHistory");
    m_usernameHistory.clear();
    emit usernameHistoryChanged();

    // 发射信号通知状态变化
    emit usernameChanged(m_username);
    emit authTokenChanged(m_authToken);
    emit serverUrlChanged(m_serverUrl);
    emit loginStatusChanged(false);

    qDebug() << "AuthManager: Credentials cleared";
}

// ═══════════════════════════════════════════════════════════════
// JWT 安全存储实现（Phase2 Task 8）
// ═══════════════════════════════════════════════════════════════

void AuthManager::storeTokensSecurely(const QString &accessToken, const QString &refreshToken, qint64 expiresIn)
{
    if (!m_keystore) {
        qWarning() << "[AuthManager] KeystoreManager 不可用，使用 QSettings fallback";
        QSettings settings;
        settings.setValue("auth/token", accessToken);
        settings.setValue("auth/tokenExpiry", expiresIn > 0 ? QDateTime::currentSecsSinceEpoch() + expiresIn : 0);
        return;
    }

    bool success = true;
    QString errorMsg;

    // 存储 access token
    if (!accessToken.isEmpty()) {
        if (!m_keystore->storeToken(KEY_ACCESS_TOKEN, accessToken)) {
            success = false;
            errorMsg = "access token";
        }
    }

    // 存储 refresh token
    if (!refreshToken.isEmpty()) {
        if (!m_keystore->storeToken(KEY_REFRESH_TOKEN, refreshToken)) {
            success = false;
            errorMsg = "refresh token";
        }
    }

    // 存储过期时间
    QString expiryStr = QString::number(expiresIn);
    if (!m_keystore->storeToken(KEY_TOKEN_EXPIRY, expiryStr)) {
        success = false;
        errorMsg = "token expiry";
    }

    if (!success) {
        QString err = QString("安全存储失败: %1").arg(errorMsg);
        qCritical() << "[AuthManager]" << err;
        emit secureStorageError(err);
    } else {
        qDebug().noquote() << "[Client][Auth] 令牌安全存储成功: expiresIn=" << expiresIn << "s";
    }
}

void AuthManager::loadTokensFromSecureStorage()
{
    if (!m_keystore) {
        qWarning() << "[AuthManager] KeystoreManager 不可用，使用 QSettings fallback";
        QSettings settings;
        m_authToken = settings.value("auth/token").toString();
        qint64 expiry = settings.value("auth/tokenExpiry", 0).toLongLong();
        if (expiry > 0) {
            m_tokenExpiresIn = expiry;
        }
        return;
    }

    // 加载 access token
    m_authToken = m_keystore->getToken(KEY_ACCESS_TOKEN);

    // 加载 refresh token
    m_refreshToken = m_keystore->getToken(KEY_REFRESH_TOKEN);

    // 加载过期时间
    QString expiryStr = m_keystore->getToken(KEY_TOKEN_EXPIRY);
    if (!expiryStr.isEmpty()) {
        bool ok;
        qint64 expiresIn = expiryStr.toLongLong(&ok);
        if (ok && expiresIn > 0) {
            // 假设 expiresIn 是相对秒数，需要加上当前时间得到绝对时间戳
            // 但 Keycloak 返回的可能是绝对时间戳（exp 字段），也可能是相对秒数
            // 这里需要根据上下文判断
            if (expiresIn > 1e9) {
                // 看起来是 Unix 时间戳（大于 10 亿）
                m_tokenExpiresIn = expiresIn;
            } else {
                // 是相对秒数
                m_tokenExpiresIn = QDateTime::currentSecsSinceEpoch() + expiresIn;
            }
        }
    }

    // 额外检查：如果有 access token，尝试解析 JWT 获取过期时间
    if (!m_authToken.isEmpty() && m_tokenExpiresIn == 0) {
        QJsonObject payload = parseJwtPayload(m_authToken);
        if (!payload.isEmpty() && payload.contains("exp")) {
            m_tokenExpiresIn = payload["exp"].toVariant().toLongLong();
            m_tokenIssuedAt = payload["iat"].toVariant().toLongLong();
        }
    }

    if (!m_authToken.isEmpty()) {
        qDebug().noquote() << "[Client][Auth] 从安全存储加载令牌成功: tokenLen=" << m_authToken.size()
                          << " hasRefresh=" << !m_refreshToken.isEmpty()
                          << " expiresAt=" << m_tokenExpiresIn;
    } else {
        qDebug() << "[Client][Auth] 安全存储中无令牌";
    }
}

void AuthManager::deleteTokensFromSecureStorage()
{
    if (!m_keystore) {
        qWarning() << "[AuthManager] KeystoreManager 不可用";
        QSettings settings;
        settings.remove("auth/token");
        settings.remove("auth/tokenExpiry");
        return;
    }

    m_keystore->deleteToken(KEY_ACCESS_TOKEN);
    m_keystore->deleteToken(KEY_REFRESH_TOKEN);
    m_keystore->deleteToken(KEY_TOKEN_EXPIRY);

    qDebug() << "[Client][Auth] 已从安全存储删除令牌";
}

qint64 AuthManager::decodeTokenExp(const QString &token) const
{
    QJsonObject payload = parseJwtPayload(token);
    if (payload.isEmpty() || !payload.contains("exp")) {
        return 0;
    }
    return payload["exp"].toVariant().toLongLong();
}
