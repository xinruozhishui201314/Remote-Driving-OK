#include "client_system_stall_diag.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPointer>
#include <QProcessEnvironment>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <QWindow>

namespace ClientSystemStallDiag {
namespace {

bool envTruthy(const QString &v) {
  const QString t = v.trimmed().toLower();
  return t == QLatin1String("1") || t == QLatin1String("true") || t == QLatin1String("yes") ||
         t == QLatin1String("on");
}

bool mainThreadDiagEnabled() {
  static const bool on = envTruthy(QProcessEnvironment::systemEnvironment().value(
      QStringLiteral("CLIENT_MAIN_THREAD_STALL_DIAG")));
  return on;
}

bool sceneGlLogEnabled() {
  static const bool on = envTruthy(
      QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLIENT_VIDEO_SCENE_GL_LOG")));
  return on;
}

class MainThreadWatchdog final : public QObject {
  Q_OBJECT
 public:
  explicit MainThreadWatchdog(QGuiApplication *app, int intervalMs, int warnExtraMs)
      : QObject(app),
        m_intervalMs(qBound(5, intervalMs, 100)),
        m_warnExtraMs(qBound(5, warnExtraMs, 500)) {
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &MainThreadWatchdog::onTick);
    m_timer->start(m_intervalMs);
  }

  MainThreadWatchdogSecondStats drainSecond() {
    MainThreadWatchdogSecondStats out;
    out.maxTickGapMs = m_maxGapThisSecond;
    out.stallEvents = m_stallCountThisSecond;
    m_maxGapThisSecond = 0;
    m_stallCountThisSecond = 0;
    return out;
  }

  static MainThreadWatchdog *instance() { return s_instance; }

  static void createIfNeeded(QGuiApplication *app, int intervalMs, int warnExtraMs) {
    if (!mainThreadDiagEnabled() || s_instance)
      return;
    s_instance = new MainThreadWatchdog(app, intervalMs, warnExtraMs);
    qInfo().noquote() << QStringLiteral(
                             "[Client][SysStall] CLIENT_MAIN_THREAD_STALL_DIAG=1 主线程 watchdog "
                             "已启动 intervalMs=%1 "
                             "stallThreshold≈interval+%2ms ★ tickGap "
                             "持续偏大=事件循环未按时运行（系统级阻塞/长槽/排队）")
                             .arg(s_instance->m_intervalMs)
                             .arg(s_instance->m_warnExtraMs);
  }

  static void destroyInstance() {
    delete s_instance;
    s_instance = nullptr;
  }

 private slots:
  void onTick() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastFireMs > 0) {
      const int gap = int(qMin<qint64>(now - m_lastFireMs, 60000));
      m_maxGapThisSecond = qMax(m_maxGapThisSecond, gap);
      const int threshold = m_intervalMs + m_warnExtraMs;
      if (gap > threshold) {
        ++m_stallCountThisSecond;
        qWarning().noquote()
            << QStringLiteral(
                   "[Client][SysStall][MainThread][instant] tickGapMs=%1 thresholdMs=%2 "
                   "intervalMs=%3 ★ 主线程未及时调度定时器；查同步阻塞/长槽/巨量日志/软渲染")
                   .arg(gap)
                   .arg(threshold)
                   .arg(m_intervalMs);
      }
    }
    m_lastFireMs = now;
  }

 private:
  QTimer *m_timer = nullptr;
  const int m_intervalMs;
  const int m_warnExtraMs;
  qint64 m_lastFireMs = 0;
  int m_maxGapThisSecond = 0;
  int m_stallCountThisSecond = 0;

  static MainThreadWatchdog *s_instance;
};

MainThreadWatchdog *MainThreadWatchdog::s_instance = nullptr;

void hookOneQuickWindow(QQuickWindow *qw) {
  if (!qw || !sceneGlLogEnabled())
    return;

  static QSet<QQuickWindow *> s_hooked;
  if (s_hooked.contains(qw))
    return;
  s_hooked.insert(qw);

  const QPointer<QQuickWindow> weakQw(qw);
  QObject::connect(
      qw, &QQuickWindow::sceneGraphInitialized, qw,
      [weakQw]() {
        if (!weakQw)
          return;
        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        if (!ctx) {
          qInfo().noquote()
              << "[Client][VideoPresent][SceneGraphGL] sceneGraphInitialized win=\""
              << weakQw->title()
              << "\" no QOpenGLContext current (可能为 RHI 非 GL 后端) — 跳过 GL 字符串";
          return;
        }
        QOpenGLFunctions *gl = ctx->functions();
        if (!gl) {
          qInfo().noquote() << "[Client][VideoPresent][SceneGraphGL] functions()=null";
          return;
        }
        gl->initializeOpenGLFunctions();
        const char *vendor = reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR));
        const char *renderer = reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER));
        const char *version = reinterpret_cast<const char *>(gl->glGetString(GL_VERSION));
        qInfo().nospace().noquote()
            << "[Client][VideoPresent][SceneGraphGL] win=\"" << weakQw->title() << "\""
            << " GL_VENDOR=" << (vendor ? vendor : "(null)")
            << " GL_RENDERER=" << (renderer ? renderer : "(null)")
            << " GL_VERSION=" << (version ? version : "(null)")
            << " ★ llvmpipe/softpipe=软件光栅；对照 LIBGL_ALWAYS_SOFTWARE 与 Mesa 文档";
      },
      Qt::DirectConnection);
}

void scanAndHookQuickWindows() {
  if (!sceneGlLogEnabled())
    return;
  const auto tops = QGuiApplication::topLevelWindows();
  for (QWindow *w : tops) {
    if (auto *qw = qobject_cast<QQuickWindow *>(w))
      hookOneQuickWindow(qw);
  }
}

}  // namespace

void installMainThreadWatchdogIfEnabled(QGuiApplication *app) {
  if (!app || !mainThreadDiagEnabled())
    return;
  bool okI = false;
  const int interval = qEnvironmentVariableIntValue("CLIENT_MAIN_THREAD_STALL_INTERVAL_MS", &okI);
  const int intervalMs = okI ? interval : 20;
  bool okE = false;
  const int extra = qEnvironmentVariableIntValue("CLIENT_MAIN_THREAD_STALL_WARN_EXTRA_MS", &okE);
  const int extraMs = okE ? extra : 45;
  MainThreadWatchdog::createIfNeeded(app, intervalMs, extraMs);
  QObject::connect(app, &QCoreApplication::aboutToQuit, app,
                   []() { MainThreadWatchdog::destroyInstance(); });
}

void hookQuickWindowsSceneGlIfEnabled() {
  if (!sceneGlLogEnabled())
    return;
  qInfo().noquote() << "[Client][VideoPresent][SceneGraphGL] CLIENT_VIDEO_SCENE_GL_LOG=1 — "
                       "将在各 QQuickWindow sceneGraphInitialized 打印 GL_RENDERER";
  scanAndHookQuickWindows();
  QTimer::singleShot(0, qGuiApp, []() { scanAndHookQuickWindows(); });
  QTimer::singleShot(500, qGuiApp, []() { scanAndHookQuickWindows(); });
}

bool isMainThreadWatchdogEnabled() { return mainThreadDiagEnabled(); }

MainThreadWatchdogSecondStats drainMainThreadWatchdogSecondStats() {
  if (MainThreadWatchdog *w = MainThreadWatchdog::instance())
    return w->drainSecond();
  return {};
}

}  // namespace ClientSystemStallDiag

#include "client_system_stall_diag.moc"
