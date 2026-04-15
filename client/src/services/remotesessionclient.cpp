#include "remotesessionclient.h"

#include "httpnetworkhelpers.h"
#include "vehicle_api_parsers.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

RemoteSessionClient::RemoteSessionClient(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent), m_nam(nam), m_reply(nullptr), m_requestVin() {}

void RemoteSessionClient::abortCurrent() {
  if (m_reply) {
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
  }
  m_requestVin.clear();
}

void RemoteSessionClient::requestCreateSession(const QString &serverUrl, const QString &authToken,
                                               const QString &vin) {
  abortCurrent();
  m_requestVin = vin;

  QUrl sessionUrl(serverUrl + QStringLiteral("/api/v1/vins/") + vin + QStringLiteral("/sessions"));
  qDebug() << "[CLIENT][Session] 创建会话 vin=" << vin << " url=" << sessionUrl.toString();

  QNetworkRequest request(sessionUrl);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  if (!authToken.isEmpty())
    request.setRawHeader("Authorization", (QStringLiteral("Bearer ") + authToken).toUtf8());

  m_reply = ClientHttp::postWithTimeout(m_nam, request, QByteArray());
  connect(m_reply, &QNetworkReply::finished, this, &RemoteSessionClient::onReplyFinished);
}

void RemoteSessionClient::onReplyFinished() {
  QNetworkReply *reply = m_reply;
  if (!reply)
    return;
  const QString requestVin = m_requestVin;
  m_reply = nullptr;
  m_requestVin.clear();

  if (reply->error() != QNetworkReply::NoError) {
    int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray body = reply->readAll();
    qWarning() << "[Client][Session] create session failed requestVin=" << requestVin
               << " err=E_BACKEND_UNREACHABLE cause=" << reply->errorString()
               << " httpCode=" << code << " body="
               << (body.isEmpty() ? "(empty)" : QString::fromUtf8(body.constData(), body.size()));
    emit sessionFailed(QStringLiteral("创建会话失败: %1").arg(reply->errorString()));
    reply->deleteLater();
    return;
  }

  QByteArray data = reply->readAll();
  reply->deleteLater();

  rd_client_api::SessionCreateParseOutcome parsed =
      rd_client_api::parseSessionCreateHttpBody(data, requestVin);
  if (!parsed.ok) {
    if (parsed.error == QStringLiteral("服务器返回的 VIN 与请求不一致")) {
      qCritical() << "[Client][Session] ★ 响应 VIN 与请求路径不一致 ★ requestVin=" << requestVin
                  << " responseVin mismatch sessionId=(parse aborted)";
    }
    emit sessionFailed(parsed.error);
    return;
  }

  emit sessionSucceeded(requestVin, parsed.canonicalVin, parsed.sessionId, parsed.whipUrl,
                        parsed.whepUrl, parsed.controlConfig);
}
