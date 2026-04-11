#include "client_present_composition_diag.h"

#include "app/client_present_health_auto_env.h"

#include <QDebug>
#include <QGuiApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQuickWindow>
#include <QTimer>
#include <QWindow>

#include <atomic>

namespace {

std::atomic<int> g_frameSwappedLocal{0};
std::atomic<int> g_sgInitLocal{0};

bool envOn(const QProcessEnvironment &env, const char *k) {
  return env.value(QString::fromUtf8(k)) == QStringLiteral("1");
}

bool isInteractiveGuiPlatform(QGuiApplication *app) {
  if (!app) {
    return false;
  }
  const QString plat = app->platformName().toLower();
  if (plat == QLatin1String("offscreen") || plat == QLatin1String("minimal")) {
    return false;
  }
  return QGuiApplication::primaryScreen() != nullptr;
}

QString redactTitle(const QString &text, const QString &title) {
  if (title.isEmpty()) {
    return text;
  }
  QString o = text;
  o.replace(title, QStringLiteral("<redacted-title>"));
  return o;
}

void hookQuickWindowCounters(QQuickWindow *qw) {
  if (!qw) {
    return;
  }
  QObject::connect(
      qw, &QQuickWindow::sceneGraphInitialized, qw,
      []() { g_sgInitLocal.fetch_add(1, std::memory_order_relaxed); }, Qt::DirectConnection);
  QObject::connect(
      qw, &QQuickWindow::frameSwapped, qw,
      []() { g_frameSwappedLocal.fetch_add(1, std::memory_order_relaxed); },
      Qt::DirectConnection);
}

void hookAllQuickWindows() {
  const auto tops = QGuiApplication::topLevelWindows();
  for (QWindow *w : tops) {
    if (auto *qw = qobject_cast<QQuickWindow *>(w)) {
      hookQuickWindowCounters(qw);
    }
  }
}

void emitPresentHealthTick() {
  const int total = g_frameSwappedLocal.load(std::memory_order_relaxed);
  static int s_prev = 0;
  static bool s_armed = false;
  int delta = 0;
  if (!s_armed) {
    s_armed = true;
    delta = total;
  } else {
    delta = total - s_prev;
  }
  s_prev = total;
  const int sg = g_sgInitLocal.load(std::memory_order_relaxed);
  qInfo().nospace().noquote()
      << "[Client][PresentHealth][1Hz] frameSwapped_observed_total=" << total << " delta_1s=" << delta
      << " sceneGraphInitialized_observed=" << sg
      << " (delta≈0 且界面应刷新时 ⇒ 呈现未 swap 或未上屏；对照 xwininfo / CLIENT_X11_DEEP_DIAG)";
}

#if defined(Q_OS_LINUX)
void tryXwininfoSnapshotImpl(QObject *ctx, int attempt) {
  auto *app = qGuiApp;
  if (!app) {
    return;
  }
  if (app->platformName() != QLatin1String("xcb")) {
    qInfo().noquote() << "[Client][PresentDiag][xwininfo] skip platform=" << app->platformName();
    return;
  }

  QQuickWindow *qw = nullptr;
  for (QWindow *w : QGuiApplication::topLevelWindows()) {
    if (auto *q = qobject_cast<QQuickWindow *>(w)) {
      qw = q;
      break;
    }
  }
  if (!qw) {
    qWarning().noquote() << "[Client][PresentDiag][xwininfo] no QQuickWindow";
    return;
  }

  const WId wid = qw->winId();
  if (!wid) {
    if (attempt < 3) {
      qInfo().noquote() << "[Client][PresentDiag][xwininfo] winId=0 将在 1500ms 后重试 attempt="
                        << (attempt + 1);
      QTimer::singleShot(1500, ctx, [ctx, attempt]() { tryXwininfoSnapshotImpl(ctx, attempt + 1); });
    } else {
      qWarning().noquote() << "[Client][PresentDiag][xwininfo] winId 仍为 0，放弃";
    }
    return;
  }

  const QString title = qw->title();
  const QString hex = QStringLiteral("0x") + QString::number(static_cast<quintptr>(wid), 16);

  QProcess proc;
  proc.setProcessChannelMode(QProcess::MergedChannels);
  proc.setProgram(QStringLiteral("xwininfo"));
  proc.setArguments({QStringLiteral("-id"), hex});
  proc.start();
  if (!proc.waitForStarted(3000)) {
    qWarning().noquote()
        << "[Client][PresentDiag][xwininfo] 无法启动 xwininfo（是否已安装并在 PATH？）";
    return;
  }
  if (!proc.waitForFinished(8000)) {
    qWarning().noquote() << "[Client][PresentDiag][xwininfo] 超时";
    proc.kill();
    proc.waitForFinished(1000);
    return;
  }

  const QString mixed = QString::fromUtf8(proc.readAllStandardOutput());
  const QString redacted = redactTitle(mixed, title);
  qInfo().nospace().noquote() << "[Client][PresentDiag][xwininfo] exit=" << proc.exitCode() << " id=" << hex;
  const auto lines = redacted.split(QLatin1Char('\n'));
  for (const QString &ln : lines) {
    qInfo().noquote() << "[Client][PresentDiag][xwininfo] |" << ln;
  }
}
#endif

}  // namespace

namespace ClientPresentCompositionDiag {

void installIfEnabled(QObject *parentForTimers) {
  if (!parentForTimers) {
    return;
  }
  auto *guiApp = qobject_cast<QGuiApplication *>(parentForTimers);
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const bool bundle = envOn(env, "CLIENT_PRESENT_COMP_DIAG");
  const bool explicitHealth = bundle || envOn(env, "CLIENT_PRESENT_HEALTH_1HZ");
  bool xinfo = bundle || envOn(env, "CLIENT_XWININFO_SNAPSHOT");

  bool autoHealth15s = false;
  if (!explicitHealth) {
    const QString autoOpt = env.value(QStringLiteral("CLIENT_AUTO_PRESENT_HEALTH")).trimmed();
    if (autoOpt != QLatin1String("0") && !ClientPresentHealthAutoEnv::looksLikeCiEnvironment(env) &&
        ClientPresentHealthAutoEnv::isSoftwareGlEnv(env) &&
        ClientPresentHealthAutoEnv::likelyContainerRuntimeEnv(env) && guiApp &&
        isInteractiveGuiPlatform(guiApp)) {
      autoHealth15s = true;
    }
  }

  const bool health = explicitHealth || autoHealth15s;
  if (!health && !xinfo) {
    return;
  }

  qInfo().nospace().noquote()
      << "[Client][PresentDiag] 呈现钉因观测已启用（写入 ClientLogging 异步文件，≤~50ms 落盘） "
         "CLIENT_PRESENT_COMP_DIAG="
      << (bundle ? "1" : "0") << " CLIENT_PRESENT_HEALTH_1HZ=" << (explicitHealth ? "1" : "0")
      << " CLIENT_AUTO_PRESENT_HEALTH_15s=" << (autoHealth15s ? "1" : "0")
      << " CLIENT_XWININFO_SNAPSHOT=" << (xinfo ? "1" : "0");

  hookAllQuickWindows();

#if defined(Q_OS_LINUX)
  if (xinfo) {
    QTimer::singleShot(500, parentForTimers, [parentForTimers]() {
      tryXwininfoSnapshotImpl(parentForTimers, 0);
    });
  }
#else
  if (xinfo) {
    qInfo().noquote() << "[Client][PresentDiag][xwininfo] skip (非 Linux 构建)";
  }
#endif

  if (health) {
    auto *t = new QTimer(parentForTimers);
    t->setObjectName(QStringLiteral("ClientPresentHealth1Hz"));
    t->setInterval(1000);
    QObject::connect(t, &QTimer::timeout, parentForTimers, emitPresentHealthTick);
    t->start();
    emitPresentHealthTick();
    if (autoHealth15s) {
      constexpr int kAutoPresentHealthMs = 15000;
      QTimer::singleShot(kAutoPresentHealthMs, parentForTimers, [t]() {
        if (t) {
          t->stop();
        }
        qInfo().noquote()
            << "[Client][PresentHealth][Auto15s] 15s 采样已结束（软件GL+容器+交互式默认开启）；"
               "全程采样请设 CLIENT_PRESENT_HEALTH_1HZ=1；关闭自动请设 CLIENT_AUTO_PRESENT_HEALTH=0";
      });
    }
  }
}

}  // namespace ClientPresentCompositionDiag
