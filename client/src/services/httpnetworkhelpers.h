#ifndef HTTPNETWORKHELPERS_H
#define HTTPNETWORKHELPERS_H

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace ClientHttp {

QNetworkReply *getWithTimeout(QNetworkAccessManager *nam, const QNetworkRequest &request,
                              int timeoutMs = 10000);
QNetworkReply *postWithTimeout(QNetworkAccessManager *nam, const QNetworkRequest &request,
                               const QByteArray &data, int timeoutMs = 10000);

}  // namespace ClientHttp

#endif
