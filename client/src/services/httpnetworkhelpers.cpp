#include "httpnetworkhelpers.h"

#include <QTimer>

namespace ClientHttp {

static void armReplyTimeout(QNetworkReply *reply, int timeoutMs, const char *verbLabel) {
  auto *timeoutTimer = new QTimer(reply);
  timeoutTimer->setSingleShot(true);
  timeoutTimer->setInterval(timeoutMs);
  QObject::connect(timeoutTimer, &QTimer::timeout, reply, [reply, verbLabel]() {
    qWarning().noquote() << "[Client][HTTP]" << verbLabel << "request timeout, aborting";
    reply->abort();
  });
  timeoutTimer->start();
}

QNetworkReply *getWithTimeout(QNetworkAccessManager *nam, const QNetworkRequest &request,
                              int timeoutMs) {
  QNetworkReply *reply = nam->get(request);
  armReplyTimeout(reply, timeoutMs, "GET");
  return reply;
}

QNetworkReply *postWithTimeout(QNetworkAccessManager *nam, const QNetworkRequest &request,
                               const QByteArray &data, int timeoutMs) {
  QNetworkReply *reply = nam->post(request, data);
  armReplyTimeout(reply, timeoutMs, "POST");
  return reply;
}

}  // namespace ClientHttp
