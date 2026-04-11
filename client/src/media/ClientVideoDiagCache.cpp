#include "ClientVideoDiagCache.h"

#include <QByteArray>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>

namespace {

QMutex g_mu;
QString g_x11NetWmName;

}  // namespace

namespace ClientVideoDiagCache {

void setX11NetWmName(const QString &name) {
  if (name.isEmpty())
    return;
  QMutexLocker lock(&g_mu);
  g_x11NetWmName = name;
}

QString x11NetWmName() {
  QMutexLocker lock(&g_mu);
  return g_x11NetWmName;
}

QString renderStackSummaryLine(const QQuickWindow *win) {
  const int api = win ? static_cast<int>(win->graphicsApi()) : -1;
  const QByteArray rhiRaw = qgetenv("QSG_RHI_BACKEND");
  const QString rhi = rhiRaw.isEmpty() ? QStringLiteral("-") : QString::fromLocal8Bit(rhiRaw);
  const QString wm = x11NetWmName();
  const QString wmOut = wm.isEmpty() ? QStringLiteral("-") : wm;
  return QStringLiteral("graphicsApi=%1 QSG_RHI_BACKEND=\"%2\" x11NetWmName=\"%3\"")
      .arg(api)
      .arg(rhi)
      .arg(wmOut);
}

QString videoPipelineEnvFingerprint() {
  auto esc = [](const QByteArray &raw) -> QString {
    if (raw.isEmpty())
      return QStringLiteral("-");
    return QString::fromLocal8Bit(raw);
  };
  return QStringLiteral(
             "fp[LIBGL_ALWAYS_SOFTWARE=\"%1\",QSG_RHI_BACKEND=\"%2\",CLIENT_ASSUME_SOFTWARE_GL=\"%3\","
             "QT_XCB_GL_INTEGRATION=\"%4\"]")
      .arg(esc(qgetenv("LIBGL_ALWAYS_SOFTWARE")))
      .arg(esc(qgetenv("QSG_RHI_BACKEND")))
      .arg(esc(qgetenv("CLIENT_ASSUME_SOFTWARE_GL")))
      .arg(esc(qgetenv("QT_XCB_GL_INTEGRATION")));
}

}  // namespace ClientVideoDiagCache
