#ifndef REMOTESESSIONCLIENT_H
#define REMOTESESSIONCLIENT_H

#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * HTTP POST /api/v1/vins/{vin}/sessions — session creation only.
 * VehicleManager stores lastWhip/lastWhep and emits sessionCreated.
 */
class RemoteSessionClient final : public QObject {
  Q_OBJECT
 public:
  explicit RemoteSessionClient(QNetworkAccessManager *nam, QObject *parent = nullptr);

  void abortCurrent();
  void requestCreateSession(const QString &serverUrl, const QString &authToken, const QString &vin);

 signals:
  /** requestVin = path VIN at POST time; used by VehicleManager to drop stale responses after
   * re-select. */
  void sessionSucceeded(const QString &requestVin, const QString &canonicalVin,
                        const QString &sessionId, const QString &whipUrl, const QString &whepUrl,
                        const QJsonObject &controlConfig);
  void sessionFailed(const QString &message);

 private slots:
  void onReplyFinished();

 private:
  QNetworkAccessManager *m_nam = nullptr;
  QNetworkReply *m_reply = nullptr;
  QString m_requestVin;
};

#endif
