#include "vehicle_api_parsers.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace rd_client_api {

bool parseVehicleListHttpBody(const QByteArray &data, QJsonArray *vehiclesOut, QString *errorOut) {
  if (!vehiclesOut || !errorOut)
    return false;
  *vehiclesOut = QJsonArray();
  errorOut->clear();

  QJsonParseError jerr;
  QJsonDocument doc = QJsonDocument::fromJson(data, &jerr);
  if (jerr.error != QJsonParseError::NoError) {
    *errorOut = QStringLiteral("服务器响应格式错误");
    return false;
  }

  QJsonObject json = doc.object();
  QJsonArray vehicles;

  if (json.contains(QStringLiteral("vins")) && json[QStringLiteral("vins")].isArray()) {
    QJsonArray vinsArray = json[QStringLiteral("vins")].toArray();
    for (const QJsonValue &v : vinsArray) {
      if (v.isString()) {
        QJsonObject obj;
        obj[QStringLiteral("vin")] = v.toString();
        vehicles.append(obj);
      }
    }
  } else if (json.contains(QStringLiteral("data")) && json[QStringLiteral("data")].isArray()) {
    vehicles = json[QStringLiteral("data")].toArray();
  } else if (json.contains(QStringLiteral("vehicles")) &&
             json[QStringLiteral("vehicles")].isArray()) {
    vehicles = json[QStringLiteral("vehicles")].toArray();
  } else if (doc.isArray()) {
    vehicles = doc.array();
  } else {
    *errorOut = QStringLiteral("无效的车辆列表格式");
    return false;
  }

  *vehiclesOut = vehicles;
  return true;
}

SessionCreateParseOutcome parseSessionCreateHttpBody(const QByteArray &data,
                                                     const QString &requestVin) {
  SessionCreateParseOutcome out;
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError) {
    out.error = QStringLiteral("服务器响应格式错误");
    return out;
  }

  QJsonObject json = doc.object();
  QString sessionId = json[QStringLiteral("sessionId")].toString();
  if (sessionId.isEmpty()) {
    out.error = QStringLiteral("响应中缺少 sessionId");
    return out;
  }

  const QString responseVin = json[QStringLiteral("vin")].toString();
  const QString canonicalVin = responseVin.isEmpty() ? requestVin : responseVin;
  if (!responseVin.isEmpty() && responseVin != requestVin) {
    out.error = QStringLiteral("服务器返回的 VIN 与请求不一致");
    return out;
  }

  QString whipUrl;
  QString whepUrl;
  QJsonObject controlConfig;
  if (json.contains(QStringLiteral("media")) && json[QStringLiteral("media")].isObject()) {
    QJsonObject media = json[QStringLiteral("media")].toObject();
    whipUrl = media[QStringLiteral("whip")].toString();
    whepUrl = media[QStringLiteral("whep")].toString();
  }
  if (json.contains(QStringLiteral("control")) && json[QStringLiteral("control")].isObject()) {
    controlConfig = json[QStringLiteral("control")].toObject();
  }

  out.ok = true;
  out.sessionId = sessionId;
  out.canonicalVin = canonicalVin;
  out.whipUrl = whipUrl;
  out.whepUrl = whepUrl;
  out.controlConfig = controlConfig;
  return out;
}

}  // namespace rd_client_api
