#include "authmanager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QDateTime>
#include <QUrl>

AuthManager::AuthManager(QObject *parent)
    : AuthManager(parent, true)  // 默认加载保存的凭据
{
}

AuthManager::AuthManager(QObject *parent, bool loadSavedCredentials)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    if (loadSavedCredentials) {
        loadCredentials();
    } else {
        qDebug() << "AuthManager: Skipping loadCredentials (reset mode)";
    }
}

AuthManager::~AuthManager()
{
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

    QString username = m_pendingUsername;
    if (username.isEmpty() && json.contains("preferred_username"))
        username = json["preferred_username"].toString();
    QJsonObject userInfo;
    userInfo["username"] = username;

    updateLoginStatus(true, username, token);
    saveCredentials();
    qDebug().noquote() << "[Client][Auth] onLoginReply: 登录成功 username=" << username << " tokenLen=" << token.size() << " → emit loginSucceeded";
    emit loginSucceeded(token, userInfo);
}

void AuthManager::logout()
{
    QString prevUser = m_username;
    qDebug().noquote() << "[Client][Auth] 退出登录: username=" << (prevUser.isEmpty() ? "(空)" : prevUser);
    updateLoginStatus(false);
    m_authToken.clear();
    m_username.clear();
    saveCredentials();
    emit loginStatusChanged(false);
    qDebug().noquote() << "[Client][Auth] 已清除 token/username 并保存; 当前 isLoggedIn=false";
}

void AuthManager::refreshToken()
{
    if (!m_isLoggedIn || m_authToken.isEmpty()) {
        emit tokenRefreshed(QString());
        return;
    }

    // abort 上一轮 refresh（防止并发；不影响登录 reply m_currentReply）
    if (m_refreshReply) {
        m_refreshReply->abort();
        m_refreshReply->deleteLater();
        m_refreshReply = nullptr;
    }

    QUrl refreshUrl(m_serverUrl + QStringLiteral("/api/auth/refresh"));
    QNetworkRequest request(refreshUrl);
    request.setRawHeader("Authorization", ("Bearer " + m_authToken).toUtf8());

    m_refreshReply = m_networkManager->post(request, QByteArray());

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
        QString token = json.contains(QStringLiteral("token"))
            ? json[QStringLiteral("token")].toString()
            : json[QStringLiteral("data")].toObject()[QStringLiteral("token")].toString();

        if (!token.isEmpty()) {
            m_authToken = token;
            emit authTokenChanged(m_authToken);
            emit tokenRefreshed(m_authToken);
            saveCredentials();
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
    QSettings settings;
    settings.setValue("auth/username", m_username);
    settings.setValue("auth/token", m_authToken);
    settings.setValue("auth/loggedIn", m_isLoggedIn);
    settings.setValue("auth/serverUrl", m_serverUrl);
    settings.setValue("auth/usernameHistory", m_usernameHistory);
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
    m_authToken = settings.value("auth/token").toString();
    m_isLoggedIn = settings.value("auth/loggedIn", false).toBool();
    m_serverUrl = settings.value("auth/serverUrl").toString();
    m_usernameHistory = settings.value("auth/usernameHistory").toStringList();

    qDebug().noquote() << "[Client][Auth] loadCredentials: username=" << (m_username.isEmpty() ? "(空)" : m_username) << " usernameHistory.count=" << m_usernameHistory.size();

    if (m_isLoggedIn && !m_authToken.isEmpty()) {
        emit usernameChanged(m_username);
        emit authTokenChanged(m_authToken);
        emit serverUrlChanged(m_serverUrl);
        emit loginStatusChanged(true);
    }
    if (!m_usernameHistory.isEmpty())
        emit usernameHistoryChanged();
}

void AuthManager::clearCredentials()
{
    qDebug() << "AuthManager: Clearing saved credentials";
    
    // 清除内存中的状态
    m_username.clear();
    m_authToken.clear();
    m_serverUrl.clear();
    m_isLoggedIn = false;
    
    // 清除 QSettings 中的保存值
    QSettings settings;
    settings.remove("auth/username");
    settings.remove("auth/token");
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
