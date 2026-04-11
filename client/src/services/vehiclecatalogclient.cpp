#include "vehiclecatalogclient.h"

#include "httpnetworkhelpers.h"
#include "vehicle_api_parsers.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

VehicleCatalogClient::VehicleCatalogClient(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent), m_nam(nam) {}

void VehicleCatalogClient::abortCurrent() {
  if (m_reply) {
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
  }
}

void VehicleCatalogClient::requestVehicleList(const QString &serverUrl, const QString &authToken) {
  abortCurrent();

  QUrl vehiclesUrl(serverUrl + QStringLiteral("/api/v1/vins"));
  qDebug().noquote() << "[Client][车辆列表] 请求 GET" << vehiclesUrl.toString()
                     << " hasToken=" << !authToken.isEmpty() << " tokenLen=" << authToken.size();

  QNetworkRequest request(vehiclesUrl);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  if (!authToken.isEmpty())
    request.setRawHeader("Authorization", (QStringLiteral("Bearer ") + authToken).toUtf8());

  m_reply = ClientHttp::getWithTimeout(m_nam, request);
  connect(m_reply, &QNetworkReply::finished, this, &VehicleCatalogClient::onReplyFinished);
}

void VehicleCatalogClient::onReplyFinished() {
  QNetworkReply *reply = m_reply;
  if (!reply)
    return;
  m_reply = nullptr;

  int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (reply->error() != QNetworkReply::NoError) {
    qDebug().noquote() << "[Client][车辆列表] HTTP error statusCode=" << statusCode
                       << " err=E_BACKEND_UNREACHABLE cause=" << reply->errorString();
    emit listFailed(QStringLiteral("获取车辆列表失败: %1").arg(reply->errorString()));
    reply->deleteLater();
    return;
  }

  QByteArray data = reply->readAll();
  reply->deleteLater();
  qDebug().noquote() << "[Client][车辆列表] onVehicleListReply: HTTP" << statusCode
                     << " bodySize=" << data.size();

  QJsonArray vehicles;
  QString err;
  if (!rd_client_api::parseVehicleListHttpBody(data, &vehicles, &err)) {
    qDebug().noquote() << "[Client][车辆列表] parse failed err=" << err;
    emit listFailed(err);
    return;
  }

  emit listSucceeded(vehicles);
}
