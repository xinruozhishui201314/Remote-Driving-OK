#include "WebRtcUrlResolve.h"

#include <QUrl>
#include <QUrlQuery>

namespace WebRtcUrlResolve {

QString baseUrlFromWhep(const QString &whepUrl) {
  if (whepUrl.isEmpty())
    return {};
  QUrl u(whepUrl);
  if (u.scheme() == QLatin1String("whep"))
    u.setScheme(QStringLiteral("http"));
  if (!u.isValid() || u.host().isEmpty())
    return {};
  QString base = QStringLiteral("%1://%2").arg(
      u.scheme().isEmpty() ? QStringLiteral("http") : u.scheme(), u.host());
  if (u.port() > 0)
    base += QStringLiteral(":%1").arg(u.port());
  return base;
}

QString resolveBaseUrl(const QString &whepUrl, const QString &envZlmVideoUrl) {
  if (!whepUrl.isEmpty())
    return baseUrlFromWhep(whepUrl);
  return envZlmVideoUrl;
}

QString appFromWhepQuery(const QString &whepUrl, const QString &defaultApp) {
  if (whepUrl.isEmpty())
    return defaultApp;
  QUrl u(whepUrl);
  QUrlQuery q(u.query());
  const QString a = q.queryItemValue(QStringLiteral("app"));
  return a.isEmpty() ? defaultApp : a;
}

}  // namespace WebRtcUrlResolve
