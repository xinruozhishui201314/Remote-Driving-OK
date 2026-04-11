#include "client_x11_deep_diag.h"

#include <QDebug>
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QProcessEnvironment>
#include <QQuickWindow>
#include <QTimer>
#include <QWindow>

#include <memory>

#if defined(CLIENT_HAVE_XCB)
#include <qnativeinterface.h>

#include <xcb/xcb.h>
#endif

namespace {

bool deepDiagEnabled() {
  static const bool on = QProcessEnvironment::systemEnvironment().value(
                             QStringLiteral("CLIENT_X11_DEEP_DIAG")) == QStringLiteral("1");
  return on;
}

void logXcbRequestBudget(const char *tag) {
#if defined(CLIENT_HAVE_XCB)
  auto *app = qGuiApp;
  if (!app || app->platformName() != QLatin1String("xcb")) {
    qInfo().noquote() << "[Client][X11Diag][" << tag
                      << "] skip xcb budget (no app or platform!=xcb)";
    return;
  }
  auto *x11 = app->nativeInterface<QNativeInterface::QX11Application>();
  if (!x11) {
    qInfo().noquote() << "[Client][X11Diag][" << tag << "] QNativeInterface::QX11Application=null";
    return;
  }
  xcb_connection_t *conn = x11->connection();
  if (!conn) {
    qInfo().noquote() << "[Client][X11Diag][" << tag << "] xcb connection=null";
    return;
  }
  const int err = xcb_connection_has_error(conn);
  if (err != 0) {
    qWarning().noquote() << "[Client][X11Diag][" << tag << "] xcb_connection_has_error=" << err
                         << " (连接已坏，无法再读 maximum_request_length)";
    return;
  }
  const uint32_t maxReqUnits = xcb_get_maximum_request_length(conn);
  const quint64 maxPayloadApprox = quint64(maxReqUnits) * 4u;
  qInfo().nospace().noquote()
      << "[Client][X11Diag][" << tag << "] xcb_get_maximum_request_length=" << maxReqUnits
      << " (four-byte units, libxcb doc) => single-request payload ceiling ~ " << maxPayloadApprox
      << " bytes; exceeding this closes connection with XCB_CONN_CLOSED_REQ_LEN_EXCEED";
#else
  Q_UNUSED(tag);
  qInfo().noquote() << "[Client][X11Diag] CLIENT_HAVE_XCB off — no xcb_get_maximum_request_length";
#endif
}

void logTopLevelSurfaceEstimates(const char *tag) {
  const auto tops = QGuiApplication::topLevelWindows();
  qInfo().noquote() << "[Client][X11Diag][" << tag << "] topLevelWindows count=" << tops.size();
  for (QWindow *w : tops) {
    if (!w) {
      continue;
    }
    const int lw = w->width();
    const int lh = w->height();
    const qreal dpr = w->devicePixelRatio();
    const qint64 pw = qint64(qCeil(qreal(lw) * dpr));
    const qint64 ph = qint64(qCeil(qreal(lh) * dpr));
    const quint64 rgba32 = quint64(pw) * quint64(ph) * 4u;

    qreal edpr = dpr;
    if (auto *qw = qobject_cast<QQuickWindow *>(w)) {
      edpr = qw->effectiveDevicePixelRatio();
    }
    const qint64 pw2 = qint64(qCeil(qreal(lw) * edpr));
    const qint64 ph2 = qint64(qCeil(qreal(lh) * edpr));
    const quint64 rgba32e = quint64(pw2) * quint64(ph2) * 4u;

    qInfo().nospace().noquote()
        << "[Client][X11Diag][" << tag << "] win title=\"" << w->title() << "\""
        << " visible=" << w->isVisible() << " logical=" << lw << "x" << lh << " dpr=" << dpr
        << " edpr=" << edpr << " ceilPhys≈" << pw << "x" << ph << " RGBA32≈" << rgba32
        << " B; edprPhys≈" << pw2 << "x" << ph2 << " RGBA32≈" << rgba32e
        << " B (若错误路径整帧单包 PutImage 且不拆片，易触发 REQ_LEN_EXCEED)";
  }
}

void hookQuickWindow(QQuickWindow *qw) {
  if (!qw || !deepDiagEnabled()) {
    return;
  }

  const auto frameTag = std::make_shared<int>(0);
  QObject::connect(
      qw, &QQuickWindow::sceneGraphInitialized, qw,
      [qw]() {
        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        if (!ctx) {
          qInfo().noquote()
              << "[Client][X11Diag][GL] sceneGraphInitialized: no QOpenGLContext current "
                 "(可能为 RHI Vulkan/SW 等，非 GL 后端 — 跳过 GL 字符串)";
          return;
        }
        QOpenGLFunctions *gl = ctx->functions();
        if (!gl) {
          qInfo().noquote() << "[Client][X11Diag][GL] sceneGraphInitialized: functions()=null";
          return;
        }
        // Qt 6: initializeOpenGLFunctions() returns void (Qt5 部分版本曾返回 bool)。
        gl->initializeOpenGLFunctions();
        const char *vendor = reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR));
        const char *renderer = reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER));
        const char *version = reinterpret_cast<const char *>(gl->glGetString(GL_VERSION));
        qInfo().nospace().noquote()
            << "[Client][X11Diag][GL] sceneGraphInitialized win=\"" << qw->title() << "\""
            << " GL_VENDOR=" << (vendor ? vendor : "(null)")
            << " GL_RENDERER=" << (renderer ? renderer : "(null)")
            << " GL_VERSION=" << (version ? version : "(null)");
      },
      Qt::DirectConnection);

  QObject::connect(
      qw, &QQuickWindow::frameSwapped, qw,
      [qw, frameTag]() {
        const int n = ++(*frameTag);
        if (n > 8) {
          return;
        }
        qInfo().nospace().noquote() << "[Client][X11Diag][Frame] frameSwapped n=" << n << " win=\""
                                    << qw->title() << "\" size=" << qw->width() << "x"
                                    << qw->height() << " edpr=" << qw->effectiveDevicePixelRatio();
      },
      Qt::DirectConnection);
}

void scheduleSamples(QObject *parent) {
  if (!parent) {
    return;
  }
  const int delaysMs[] = {0, 50, 120, 250, 400, 700};
  for (int d : delaysMs) {
    QTimer::singleShot(d, parent, [d]() {
      const QByteArray tag = QByteArray("t+") + QByteArray::number(d) + "ms";
      logXcbRequestBudget(tag.constData());
      logTopLevelSurfaceEstimates(tag.constData());
    });
  }
}

}  // namespace

namespace ClientX11DeepDiag {

void mergePreAppLoggingRules(QString &inOutFilterRules) {
  if (!deepDiagEnabled()) {
    return;
  }
  inOutFilterRules += QStringLiteral("qt.qpa.xcb.debug=true\n");
  fprintf(stderr,
          "[Client][X11Diag] CLIENT_X11_DEEP_DIAG=1: enabling qt.qpa.xcb.debug (see Qt "
          "QLoggingCategory)\n");
}

void installAfterQmlLoaded(QObject *appParentForTimers) {
  if (!deepDiagEnabled()) {
    return;
  }
  qInfo().noquote() << "[Client][X11Diag] CLIENT_X11_DEEP_DIAG=1 已启用 — 周期性采样 X "
                       "单包上限与窗口像素缓冲粗算";
  logXcbRequestBudget("initial");
  logTopLevelSurfaceEstimates("initial");

  const auto tops = QGuiApplication::topLevelWindows();
  for (QWindow *w : tops) {
    if (auto *qw = qobject_cast<QQuickWindow *>(w)) {
      hookQuickWindow(qw);
    }
  }
  scheduleSamples(appParentForTimers);
}

}  // namespace ClientX11DeepDiag
