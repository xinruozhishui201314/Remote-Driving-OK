#include "nodehealthchecker.h"

#include <QDebug>
#include <QProcessEnvironment>
#include <QUrl>

static const int REQUEST_TIMEOUT_MS = 4000;

// 登录页仍可能为 localhost（宿主机视角）；容器内 localhost 不可达
// Backend。此时用环境变量中的服务地址探测。
static bool isLocalLoopbackHost(const QString &host) {
  const QString h = host.toLower();
  return h.isEmpty() || h == QLatin1String("localhost") || h == QLatin1String("127.0.0.1");
}

static QString backendBaseForProbes(const QString &serverUrl) {
  const QString envB =
      QProcessEnvironment::systemEnvironment().value(QStringLiteral("BACKEND_URL"));
  if (envB.isEmpty())
    return serverUrl;
  const QUrl su(serverUrl);
  if (isLocalLoopbackHost(su.host()))
    return envB;
  return serverUrl;
}

static QUrl keycloakHealthUrlFromServer(const QString &serverUrl) {
  const QString envK =
      QProcessEnvironment::systemEnvironment().value(QStringLiteral("KEYCLOAK_URL"));
  if (!envK.isEmpty()) {
    const QUrl su(serverUrl);
    if (isLocalLoopbackHost(su.host())) {
      QUrl kc(envK);
      kc.setPath(QStringLiteral("/health/ready"));
      return kc;
    }
  }

  QUrl kcBase(serverUrl);
  if (kcBase.host() == QLatin1String("backend")) {
    kcBase.setHost(QLatin1String("keycloak"));
    kcBase.setPort(8080);
  } else if (kcBase.port() == 8081 || kcBase.port() <= 0) {
    kcBase.setPort(8080);
  }
  QString kcPath = kcBase.path();
  if (kcPath.isEmpty() || kcPath == QLatin1String("/")) {
    kcBase.setPath(QStringLiteral("/health/ready"));
  } else {
    if (!kcPath.endsWith(QLatin1Char('/')))
      kcPath += QLatin1Char('/');
    kcBase.setPath(kcPath + QLatin1String("health/ready"));
  }
  return kcBase;
}

NodeHealthChecker::NodeHealthChecker(QObject *parent)
    : QObject(parent),
      m_networkManager(nullptr),
      m_backendReply(nullptr),
      m_keycloakReply(nullptr),
      m_zlmReply(nullptr),
      m_backendStatus(QStringLiteral("—")),
      m_backendMessage(),
      m_keycloakStatus(QStringLiteral("—")),
      m_keycloakMessage(),
      m_zlmStatus(QStringLiteral("—")),
      m_zlmMessage(),
      m_isChecking(false),
      m_pendingCount(0) {
  m_networkManager = new QNetworkAccessManager(this);
}

void NodeHealthChecker::setBackendStatus(const QString &status, const QString &message) {
  if (m_backendStatus != status || m_backendMessage != message) {
    m_backendStatus = status;
    m_backendMessage = message;
    emit backendStatusChanged();
    emit backendMessageChanged();
  }
}

void NodeHealthChecker::setKeycloakStatus(const QString &status, const QString &message) {
  if (m_keycloakStatus != status || m_keycloakMessage != message) {
    m_keycloakStatus = status;
    m_keycloakMessage = message;
    emit keycloakStatusChanged();
    emit keycloakMessageChanged();
  }
}

void NodeHealthChecker::setZlmStatus(const QString &status, const QString &message) {
  if (m_zlmStatus != status || m_zlmMessage != message) {
    m_zlmStatus = status;
    m_zlmMessage = message;
    emit zlmStatusChanged();
    emit zlmMessageChanged();
  }
}

void NodeHealthChecker::setChecking(bool v) {
  if (m_isChecking != v) {
    m_isChecking = v;
    emit isCheckingChanged();
  }
}

void NodeHealthChecker::refresh(const QString &serverUrl) {
  qDebug().noquote() << "[Client][节点检测] refresh serverUrl=" << serverUrl;

  if (m_backendReply) {
    m_backendReply->abort();
    m_backendReply->deleteLater();
    m_backendReply = nullptr;
  }
  if (m_keycloakReply) {
    m_keycloakReply->abort();
    m_keycloakReply->deleteLater();
    m_keycloakReply = nullptr;
  }
  if (m_zlmReply) {
    m_zlmReply->abort();
    m_zlmReply->deleteLater();
    m_zlmReply = nullptr;
  }

  setBackendStatus(tr("检测中"), QString());
  setKeycloakStatus(tr("检测中"), QString());
  setZlmStatus(tr("检测中"), QString());
  setChecking(true);
  m_pendingCount = 0;

  QUrl baseUrl(serverUrl);
  if (!baseUrl.isValid() || baseUrl.scheme().isEmpty()) {
    qDebug().noquote() << "[Client][节点检测] serverUrl 无效，跳过";
    setBackendStatus(tr("未检测"), tr("服务器地址无效"));
    setKeycloakStatus(tr("未检测"), tr("服务器地址无效"));
    setZlmStatus(tr("未检测"), tr("服务器地址无效"));
    setChecking(false);
    emit checkFinished();
    return;
  }

  const QString backendBase = backendBaseForProbes(serverUrl);
  // Backend: 基地址 GET /health
  QUrl backendHealthUrl(backendBase);
  if (!backendHealthUrl.path().endsWith(QLatin1String("health"))) {
    QString p = backendHealthUrl.path();
    backendHealthUrl.setPath(
        (p.isEmpty() || p == QLatin1String("/"))
            ? QLatin1String("/health")
            : (p + (p.endsWith(QLatin1Char('/')) ? QLatin1String("") : QLatin1String("/")) +
               QLatin1String("health")));
  }
  m_pendingCount++;
  m_backendReply = m_networkManager->get(QNetworkRequest(backendHealthUrl));
  m_backendReply->setProperty("node", QStringLiteral("Backend"));
  QTimer::singleShot(REQUEST_TIMEOUT_MS, m_backendReply, [this]() {
    if (m_backendReply && m_backendReply->isRunning()) {
      m_backendReply->abort();
    }
  });
  connect(m_backendReply, &QNetworkReply::finished, this, &NodeHealthChecker::onBackendFinished);

  const QUrl kcHealth = keycloakHealthUrlFromServer(serverUrl);
  m_pendingCount++;
  m_keycloakReply = m_networkManager->get(QNetworkRequest(kcHealth));
  m_keycloakReply->setProperty("node", QStringLiteral("Keycloak"));
  QTimer::singleShot(REQUEST_TIMEOUT_MS, m_keycloakReply, [this]() {
    if (m_keycloakReply && m_keycloakReply->isRunning()) {
      m_keycloakReply->abort();
    }
  });
  connect(m_keycloakReply, &QNetworkReply::finished, this, &NodeHealthChecker::onKeycloakFinished);

  // ZLM: 优先环境变量 ZLM_VIDEO_URL，否则同 host 端口 80，路径 /index/api/getServerConfig
  QString zlmBase = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ZLM_VIDEO_URL"));
  if (zlmBase.isEmpty()) {
    QUrl zlmUrl(serverUrl);
    zlmUrl.setPort(80);
    zlmUrl.setPath(QStringLiteral("/index/api/getServerConfig"));
    zlmBase = zlmUrl.toString();
  } else {
    if (!zlmBase.endsWith(QLatin1Char('/')))
      zlmBase += QLatin1Char('/');
    zlmBase += QLatin1String("index/api/getServerConfig");
  }
  m_pendingCount++;
  m_zlmReply = m_networkManager->get(QNetworkRequest(QUrl(zlmBase)));
  m_zlmReply->setProperty("node", QStringLiteral("ZLM"));
  QTimer::singleShot(REQUEST_TIMEOUT_MS, m_zlmReply, [this]() {
    if (m_zlmReply && m_zlmReply->isRunning()) {
      m_zlmReply->abort();
    }
  });
  connect(m_zlmReply, &QNetworkReply::finished, this, &NodeHealthChecker::onZlmFinished);
}

void NodeHealthChecker::onBackendFinished() {
  QNetworkReply *reply = m_backendReply;
  m_backendReply = nullptr;
  if (!reply)
    return;
  reply->deleteLater();

  int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (reply->error() != QNetworkReply::NoError) {
    qDebug().noquote() << "[Client][节点检测] Backend 请求失败:" << reply->errorString()
                       << "url=" << reply->url().toString();
    setBackendStatus(tr("不可达"), reply->errorString());
  } else if (code >= 200 && code < 300) {
    qDebug().noquote() << "[Client][节点检测] Backend 正常 HTTP" << code;
    setBackendStatus(tr("正常"), QString());
  } else {
    setBackendStatus(tr("异常"), QStringLiteral("HTTP %1").arg(code));
  }
  tryFinish();
}

void NodeHealthChecker::onKeycloakFinished() {
  QNetworkReply *reply = m_keycloakReply;
  m_keycloakReply = nullptr;
  if (!reply)
    return;
  reply->deleteLater();

  int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (reply->error() != QNetworkReply::NoError) {
    qDebug().noquote() << "[Client][节点检测] Keycloak 请求失败:" << reply->errorString()
                       << "url=" << reply->url().toString();
    setKeycloakStatus(tr("不可达"), reply->errorString());
  } else if (code >= 200 && code < 300) {
    qDebug().noquote() << "[Client][节点检测] Keycloak 正常 HTTP" << code;
    setKeycloakStatus(tr("正常"), QString());
  } else {
    setKeycloakStatus(tr("异常"), QStringLiteral("HTTP %1").arg(code));
  }
  tryFinish();
}

void NodeHealthChecker::onZlmFinished() {
  QNetworkReply *reply = m_zlmReply;
  m_zlmReply = nullptr;
  if (!reply)
    return;
  reply->deleteLater();

  int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (reply->error() != QNetworkReply::NoError) {
    qDebug().noquote() << "[Client][节点检测] ZLM 请求失败:" << reply->errorString()
                       << "url=" << reply->url().toString();
    setZlmStatus(tr("不可达"), reply->errorString());
  } else if (code >= 200 && code < 300) {
    qDebug().noquote() << "[Client][节点检测] ZLM 正常 HTTP" << code;
    setZlmStatus(tr("正常"), QString());
  } else {
    setZlmStatus(tr("异常"), QStringLiteral("HTTP %1").arg(code));
  }
  tryFinish();
}

void NodeHealthChecker::tryFinish() {
  m_pendingCount--;
  if (m_pendingCount <= 0) {
    setChecking(false);
    qDebug().noquote() << "[Client][节点检测] 全部完成 Backend=" << m_backendStatus
                       << " Keycloak=" << m_keycloakStatus << " ZLM=" << m_zlmStatus;
    emit checkFinished();
  }
}
