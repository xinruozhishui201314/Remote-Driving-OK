#ifndef VEHICLECATALOGCLIENT_H
#define VEHICLECATALOGCLIENT_H

#include <QJsonArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * HTTP GET /api/v1/vins — vehicle list fetch only (no local VIN state).
 * Emits parsed array; VehicleManager applies updateVehicleList + signals.
 */
class VehicleCatalogClient final : public QObject {
  Q_OBJECT
 public:
  explicit VehicleCatalogClient(QNetworkAccessManager *nam, QObject *parent = nullptr);

  void abortCurrent();
  void requestVehicleList(const QString &serverUrl, const QString &authToken);

 signals:
  void listSucceeded(const QJsonArray &vehicles);
  void listFailed(const QString &message);

 private slots:
  void onReplyFinished();

 private:
  QNetworkAccessManager *m_nam = nullptr;
  QNetworkReply *m_reply = nullptr;
};

#endif
