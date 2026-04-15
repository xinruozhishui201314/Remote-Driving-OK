#include "client_crash_diagnostics.h"

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMutex>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPointer>
#include <QProcessEnvironment>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWindow>
#include <QtGlobal>
#include <QtMath>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <utility>

namespace {

// 主线程可读：渲染线程在 sceneGraphInitialized 中写入（mutex）；REQ_LEN_EXCEED 时对照 GL 栈
QMutex g_glMetaMutex;
QByteArray g_glVendorUtf8;
QByteArray g_glRendererUtf8;
QByteArray g_glVersionUtf8;

std::atomic<int> g_qquickFrameSwappedTotal{0};
std::atomic<int> g_qquickSceneGraphInitCount{0};

QString graphicsApiLabel(int apiInt) {
  using G = QSGRendererInterface::GraphicsApi;
  const auto api = static_cast<G>(apiInt);
  switch (api) {
    case G::Unknown:
      return QStringLiteral("Unknown(0)");
    case G::Software:
      return QStringLiteral("Software(1)");
    case G::OpenVG:
      return QStringLiteral("OpenVG(2)");
    case G::OpenGL:
      return QStringLiteral("OpenGL/RHI(3)");
    case G::Direct3D11:
      return QStringLiteral("Direct3D11(4)");
    case G::Vulkan:
      return QStringLiteral("Vulkan(5)");
    case G::Metal:
      return QStringLiteral("Metal(6)");
    case G::Null:
      return QStringLiteral("Null(7)");
    default:
      return QStringLiteral("GraphicsApi(") + QString::number(apiInt) + QLatin1Char(')');
  }
}

void logExtraEnvForX11Gpu(const QProcessEnvironment &env) {
  auto ev = [&env](const char *k) -> QString {
    const QString v = env.value(QString::fromLatin1(k));
    return v.isEmpty() ? QStringLiteral("-") : v;
  };
  qCritical().noquote()
      << "[Client][CrashDiag][xcb][REQ_LEN_EXCEED] Mesa/GL 相关: MESA_LOADER_DRIVER_OVERRIDE="
      << ev("MESA_LOADER_DRIVER_OVERRIDE") << " GALLIUM_DRIVER=" << ev("GALLIUM_DRIVER")
      << " LIBGL_DRIVERS_PATH=" << ev("LIBGL_DRIVERS_PATH") << " MESA_DEBUG=" << ev("MESA_DEBUG");
  qCritical().noquote() << "[Client][CrashDiag][xcb][REQ_LEN_EXCEED] 渲染进度快照: "
                           "QQuickWindow::sceneGraphInitialized 已触发次数="
                        << g_qquickSceneGraphInitCount.load(std::memory_order_relaxed)
                        << " frameSwapped 累计次数="
                        << g_qquickFrameSwappedTotal.load(std::memory_order_relaxed)
                        << " (若为 0 表示断连发生在首次 swap/present 前后)";
  {
    const QMutexLocker locker(&g_glMetaMutex);
    if (!g_glRendererUtf8.isEmpty() || !g_glVendorUtf8.isEmpty()) {
      qCritical().noquote()
          << "[Client][CrashDiag][xcb][REQ_LEN_EXCEED] 上次 sceneGraph GL 字符串: GL_VENDOR="
          << QString::fromUtf8(g_glVendorUtf8)
          << " GL_RENDERER=" << QString::fromUtf8(g_glRendererUtf8)
          << " GL_VERSION=" << QString::fromUtf8(g_glVersionUtf8);
    } else {
      qCritical().noquote() << "[Client][CrashDiag][xcb][REQ_LEN_EXCEED] 尚无 GL "
                               "字符串缓存（sceneGraphInitialized 未跑完或未创建 GL 上下文）";
    }
  }
}

}  // namespace

#if defined(CLIENT_HAVE_XCB)
// 使用 <qnativeinterface.h>：Qt6::Gui 已把 .../include/QtGui 加入包含路径，
// 写 <QtGui/qnativeinterface.h> 会变成 QtGui/QtGui/... 导致编译失败。
#include <qnativeinterface.h>

#include <xcb/xcb.h>
#endif

#if defined(Q_OS_LINUX)
#include <csignal>
#include <unistd.h>
#endif

namespace {

std::terminate_handler s_prevTerminate = nullptr;

#if defined(CLIENT_HAVE_XCB)
// 启动时 xcb_get_maximum_request_length（四字节单位，见 libxcb 文档）；REQ_LEN_EXCEED
// 时连接已坏，只能复用该快照做定量对照
std::atomic<uint32_t> g_xcbBootMaxRequestUnits{0};
#endif

// REQ_LEN_EXCEED 时对照「首帧前后」时间线（毫秒，自 installAfterQGuiApplication 起算）
QElapsedTimer g_sinceGuiAppReady;
std::atomic<bool> g_elapsedStarted{false};

#if defined(Q_OS_LINUX)
struct sigaction s_oldSigabrt {};
#endif

// libxcb 官方枚举数值（xcb_connection_has_error 返回值），与 Qt xcb 插件日志中 error N 一致
// 参考：https://xcb.freedesktop.org/manual/group__XCB__Core__API.html
const char *xcbConnectionErrorName(int err) {
  switch (err) {
    case 1:
      return "XCB_CONN_ERROR (socket/stream)";
    case 2:
      return "XCB_CONN_CLOSED_EXT_NOTSUPPORTED";
    case 3:
      return "XCB_CONN_CLOSED_MEM_INSUFFICIENT";
    case 4:
      return "XCB_CONN_CLOSED_REQ_LEN_EXCEED (request length > server max)";
    case 5:
      return "XCB_CONN_CLOSED_PARSE_ERR";
    case 6:
      return "XCB_CONN_CLOSED_INVALID_SCREEN";
    case 7:
      return "XCB_CONN_CLOSED_FDPASSING_FAILED";
    default:
      return "unknown_code (see libxcb xcb_connection_has_error)";
  }
}

#if defined(CLIENT_HAVE_XCB)
/**
 * libxcb 文档：xcb_get_maximum_request_length 返回值为「四字节单位」；
 * 单条 X 请求若超过该上限，服务器可关闭连接并导致
 * xcb_connection_has_error()==XCB_CONN_CLOSED_REQ_LEN_EXCEED。
 * @see https://xcb.freedesktop.org/manual/group__XCB__Core__API.html
 */
void logReqLenExceedFramebufferHypothesis() {
  const uint32_t units = g_xcbBootMaxRequestUnits.load(std::memory_order_relaxed);
  if (units == 0) {
    qCritical().noquote() << "[Client][CrashDiag][xcb][REQ_LEN_EXCEED][size] boot "
                             "maximum_request_length 未记录（连接在启动阶段已异常？）"
                             " — 跳过整帧 vs 单包上限对照";
    return;
  }
  const quint64 ceilingBytes = quint64(units) * 4u;
  qCritical().nospace().noquote()
      << "[Client][CrashDiag][xcb][REQ_LEN_EXCEED][size] boot xcb_get_maximum_request_length="
      << units << " four-byte units (libxcb) => single-request payload ceiling ~ " << ceilingBytes
      << " bytes; 若某路径把整帧 RGBA32 作为「单个」X 请求发出且未拆片，则可能触发本错误";

  const auto tops = QGuiApplication::topLevelWindows();
  for (QWindow *w : tops) {
    if (!w) {
      continue;
    }
    const int lw = w->width();
    const int lh = w->height();
    const qreal dpr = w->devicePixelRatio();
    qreal edpr = dpr;
    if (auto *qw = qobject_cast<QQuickWindow *>(w)) {
      edpr = qw->effectiveDevicePixelRatio();
    }
    const qint64 pw = qint64(qCeil(qreal(lw) * edpr));
    const qint64 ph = qint64(qCeil(qreal(lh) * edpr));
    const quint64 rgba32 = quint64(qMax(pw, qint64(0))) * quint64(qMax(ph, qint64(0))) * 4u;
    const bool over = rgba32 > ceilingBytes;
    qCritical().nospace().noquote()
        << "[Client][CrashDiag][xcb][REQ_LEN_EXCEED][size] win=\"" << w->title() << "\""
        << " logical=" << lw << "x" << lh << " edpr=" << edpr << " ceilPhys≈" << pw << "x" << ph
        << " fullFrameRGBA32≈" << rgba32 << " B; exceedsSingleRequestCeiling=" << over
        << " (粗算：用于判断「整帧单包」假说；实际 X 请求未必等于整窗 RGBA，但高 DPR 大窗下易超限)";
  }
}
#endif

void writeStderr(const char *s) {
  if (!s) {
    return;
  }
  const size_t n = std::strlen(s);
  if (n > 0) {
    (void)::write(STDERR_FILENO, s, n);
  }
}

void clientTerminateHandler() {
  writeStderr("[Client][CrashDiag] std::terminate() invoked\n");
  {
    if (std::current_exception()) {
      {
        std::rethrow_exception(std::current_exception());
      }  
    } else {
      writeStderr(
          "[Client][CrashDiag] std::current_exception() is null "
          "(typical: noexcept violation, double exception, or direct std::terminate)\n");
    }
  } 
  if (s_prevTerminate) {
    s_prevTerminate();
  } else {
    std::abort();
  }
}

#if defined(Q_OS_LINUX)
// 仅 async-signal-safe：write/raise/sigaction（见 POSIX signal-safety；勿在此调用 qDebug）
static struct sigaction s_oldSigint;
static struct sigaction s_oldSigterm;

void onSigint(int sig) {
  (void)sig;
  constexpr char kMsg[] =
      "[Client][Lifecycle] SIGINT (^C): 预期退出码 130(128+2)，属用户/终端中断，不是 SIGSEGV "
      "崩溃\n";
  (void)::write(STDERR_FILENO, kMsg, sizeof(kMsg) - 1);
  if (sigaction(SIGINT, &s_oldSigint, nullptr) != 0) {
    signal(SIGINT, SIG_DFL);
  }
  raise(SIGINT);
}

void onSigterm(int sig) {
  (void)sig;
  constexpr char kMsg[] =
      "[Client][Lifecycle] SIGTERM: 预期退出码 143(128+15)，多为 systemd/docker/kill；非典型崩溃\n";
  (void)::write(STDERR_FILENO, kMsg, sizeof(kMsg) - 1);
  if (sigaction(SIGTERM, &s_oldSigterm, nullptr) != 0) {
    signal(SIGTERM, SIG_DFL);
  }
  raise(SIGTERM);
}

void onSigabrt(int sig) {
  constexpr char kMsg[] =
      "[Client][CrashDiag] SIGABRT received (often follows assert/qFatal/abort)\n";
  (void)::write(STDERR_FILENO, kMsg, sizeof(kMsg) - 1);
  if (sigaction(SIGABRT, &s_oldSigabrt, nullptr) != 0) {
    signal(SIGABRT, SIG_DFL);
  }
  raise(sig);
}
#endif

#if defined(CLIENT_HAVE_XCB)
class XcbNativeLogFilter final : public QAbstractNativeEventFilter {
 public:
  bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override {
    Q_UNUSED(result);
    if (eventType != QByteArrayLiteral("xcb_generic_event_t") || message == nullptr) {
      return false;
    }
    auto *ev = static_cast<xcb_generic_event_t *>(message);
    // X11 协议错误事件：response_type 为 0（Qt qxcbconnection 同判据）
    if ((ev->response_type & ~0x80) != 0) {
      return false;
    }
    auto *err = reinterpret_cast<xcb_generic_error_t *>(ev);
    qCritical().nospace().noquote()
        << "[Client][CrashDiag][xcb][XErrorEvent] "
        << "error_code=" << static_cast<int>(err->error_code) << " major_op=" << err->major_code
        << " minor_op=" << err->minor_code << " resourceId=" << err->resource_id
        << " sequence=" << err->sequence << " (see X.org XErrorEvent / xcb_generic_error_t)";
    return false;
  }
};

static XcbNativeLogFilter s_xcbNativeFilter;
#endif

}  // namespace

namespace ClientCrashDiagnostics {

QString annotateIfX11Broke(const QString &msg) {
  static const QRegularExpression re(QStringLiteral(R"(The X11 connection broke \(error (\d+)\))"),
                                     QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = re.match(msg);
  if (!m.hasMatch()) {
    return {};
  }
  const int code = m.capturedView(1).toInt();
  return QStringLiteral(
             "\n[Client][CrashDiag][xcb] Qt 打印的 error=%1 来自 xcb_connection_has_error()，"
             "对应 libxcb: %2\n")
      .arg(code)
      .arg(QString::fromUtf8(xcbConnectionErrorName(code)));
}

void installEarlyPlatformHooks() {
  static std::atomic<bool> done{false};
  if (done.exchange(true)) {
    return;
  }
  s_prevTerminate = std::set_terminate(clientTerminateHandler);

#if defined(Q_OS_LINUX)
  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_handler = onSigabrt;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGABRT, &sa, &s_oldSigabrt) != 0) {
    std::perror("[Client][CrashDiag] sigaction(SIGABRT) failed");
  }
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_handler = onSigint;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGINT, &sa, &s_oldSigint) != 0) {
    std::perror("[Client][CrashDiag] sigaction(SIGINT) failed");
  }
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_handler = onSigterm;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGTERM, &sa, &s_oldSigterm) != 0) {
    std::perror("[Client][CrashDiag] sigaction(SIGTERM) failed");
  }
#endif
}

void installAfterQGuiApplication(QGuiApplication &app) {
  if (!g_elapsedStarted.exchange(true)) {
    g_sinceGuiAppReady.start();
  }

#if defined(CLIENT_HAVE_XCB)
  if (app.platformName() == QLatin1String("xcb")) {
    app.installNativeEventFilter(&s_xcbNativeFilter);
    qInfo().noquote() << "[Client][CrashDiag] xcb QAbstractNativeEventFilter 已安装（将记录 X "
                         "protocol error 事件）";

    // libxcb: xcb_get_maximum_request_length — 服务器允许的单条请求长度上限（字节级线索，用于对照
    // REQ_LEN_EXCEED） 文档: https://xcb.freedesktop.org/manual/group__XCB__Core__API.html
    if (auto *x11Boot = app.nativeInterface<QNativeInterface::QX11Application>()) {
      if (xcb_connection_t *bootConn = x11Boot->connection()) {
        if (xcb_connection_has_error(bootConn) == 0) {
          const uint32_t maxReq = xcb_get_maximum_request_length(bootConn);
          g_xcbBootMaxRequestUnits.store(maxReq, std::memory_order_relaxed);
          const QByteArray disp =
              QProcessEnvironment::systemEnvironment().value(QStringLiteral("DISPLAY")).toUtf8();
          const quint64 approxCeilingBytes = quint64(maxReq) * 4u;
          qInfo().nospace().noquote()
              << "[Client][CrashDiag][xcb] boot DISPLAY=" << disp
              << " xcb_get_maximum_request_length=" << maxReq << " four-byte units (libxcb doc) ~ "
              << approxCeilingBytes
              << " B max single-request payload; exceeding => XCB_CONN_CLOSED_REQ_LEN_EXCEED";
        }
      }
    }

    auto *poll = new QTimer(&app);
    QObject::connect(poll, &QTimer::timeout, &app, []() {
      auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
      if (!x11) {
        return;
      }
      xcb_connection_t *conn = x11->connection();
      if (!conn) {
        return;
      }
      const int err = xcb_connection_has_error(conn);
      static int s_lastLogged = 0;
      if (err != 0 && err != s_lastLogged) {
        s_lastLogged = err;
        qCritical().nospace().noquote()
            << "[Client][CrashDiag][xcb] xcb_connection_has_error()=" << err << " "
            << xcbConnectionErrorName(err)
            << " (libxcb: 连接已不可用；Qt 将 qWarning 并 exit，见 qtbase qxcbconnection.cpp "
               "processXcbEvents)";
        // REQ_LEN_EXCEED：单条 X 请求超过服务器上限（见 xcb_connection_has_error 文档）。
        // 记录窗口几何便于对照 Scene Graph / GL 首帧上传与布局异常（如 width=0 后突变）。
        if (err == 4) {
          const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
          if (g_elapsedStarted.load(std::memory_order_relaxed)) {
            qCritical().nospace().noquote()
                << "[Client][CrashDiag][xcb][REQ_LEN_EXCEED] msSinceGuiAppReady="
                << g_sinceGuiAppReady.elapsed() << " (用于对照首帧/布局稳定与 xcb 断连时刻)";
          }
          qCritical().noquote() << "[Client][CrashDiag][xcb][REQ_LEN_EXCEED] 现场 DISPLAY="
                                << env.value(QStringLiteral("DISPLAY")) << " QT_XCB_GL_INTEGRATION="
                                << env.value(QStringLiteral("QT_XCB_GL_INTEGRATION"))
                                << " LIBGL_ALWAYS_SOFTWARE="
                                << env.value(QStringLiteral("LIBGL_ALWAYS_SOFTWARE"));
          const auto tops = QGuiApplication::topLevelWindows();
          qCritical().noquote() << "[Client][CrashDiag][xcb][REQ_LEN_EXCEED] topLevelWindows count="
                                << tops.size();
          for (QWindow *w : tops) {
            if (!w) {
              continue;
            }
            const QRect g = w->geometry();
            qCritical().nospace().noquote()
                << "[Client][CrashDiag][xcb][REQ_LEN_EXCEED] win title=" << w->title()
                << " visible=" << w->isVisible() << " geom=" << g.x() << ',' << g.y() << ' '
                << g.width() << 'x' << g.height() << " dpr=" << w->devicePixelRatio();
          }
          logReqLenExceedFramebufferHypothesis();
          logExtraEnvForX11Gpu(env);
          qCritical().noquote()
              << "[Client][CrashDiag][xcb][REQ_LEN_EXCEED] 下一步：主机侧 x11trace/xtrace 抓包，或 "
                 "QT_LOGGING_RULES=\"qt.qpa.xcb.debug=true\"（见 Qt QLoggingCategory "
                 "文档）定位过大请求来源；"
                 "或 CLIENT_X11_DEEP_DIAG=1 启用 frameSwapped/GL 字符串采样（见 "
                 "client_x11_deep_diag.h）";
        }
      }
    });
    poll->start(250);
    qInfo().noquote() << "[Client][CrashDiag] xcb 连接轮询已启动 intervalMs=250";
  } else {
    qInfo().noquote() << "[Client][CrashDiag] 非 xcb 平台 name=" << app.platformName()
                      << " 跳过 xcb 过滤器/轮询";
  }
#else
  Q_UNUSED(app);
  qInfo().noquote() << "[Client][CrashDiag] 未编译 CLIENT_HAVE_XCB，跳过 xcb 原生过滤与轮询";
#endif
}

void installAfterTopLevelWindowsReady() {
  const auto windows = QGuiApplication::topLevelWindows();
  int hooked = 0;
  for (QWindow *w : windows) {
    auto *qw = qobject_cast<QQuickWindow *>(w);
    if (!qw) {
      continue;
    }
    QPointer<QQuickWindow> weak(qw);
    QObject::connect(
        qw, &QQuickWindow::sceneGraphError, qGuiApp,
        [weak](QQuickWindow::SceneGraphError error, const QString &message) {
          qCritical().nospace().noquote()
              << "[Client][CrashDiag][SceneGraph] SceneGraphError=" << static_cast<int>(error)
              << " message=" << message << " window=" << (weak ? weak.data() : nullptr);
        },
        Qt::DirectConnection);

    QObject::connect(
        qw, &QQuickWindow::frameSwapped, qw,
        []() { g_qquickFrameSwappedTotal.fetch_add(1, std::memory_order_relaxed); },
        Qt::DirectConnection);

    // 渲染线程：对照 libxcb REQ_LEN_EXCEED 与「整窗 RGBA 单包」假说（Khronos: glGet
    // GL_MAX_TEXTURE_SIZE 等）
    QObject::connect(
        qw, &QQuickWindow::sceneGraphInitialized, qw,
        [qw]() {
          g_qquickSceneGraphInitCount.fetch_add(1, std::memory_order_relaxed);
          int maxTex = 0;
          int vpDims[2] = {0, 0};
          int apiInt = -1;
          QString fmtStr = QStringLiteral("(no GL context)");
          QOpenGLContext *ctx = QOpenGLContext::currentContext();
          if (ctx) {
            if (QOpenGLFunctions *f = ctx->functions()) {
              f->initializeOpenGLFunctions();
              // GL_MAX_TEXTURE_SIZE / GL_MAX_VIEWPORT_DIMS（Khronos OpenGL Registry 常量）
              f->glGetIntegerv(0x0D33, &maxTex);
              f->glGetIntegerv(0x0D3A, vpDims);
            }
            const QSurfaceFormat fmt = ctx->format();
            fmtStr =
                QStringLiteral("rgba=%1/%2/%3/%4 depth=%5 stencil=%6 samples=%7 swapInterval=%8")
                    .arg(fmt.redBufferSize())
                    .arg(fmt.greenBufferSize())
                    .arg(fmt.blueBufferSize())
                    .arg(fmt.alphaBufferSize())
                    .arg(fmt.depthBufferSize())
                    .arg(fmt.stencilBufferSize())
                    .arg(fmt.samples())
                    .arg(fmt.swapInterval());
            if (QOpenGLFunctions *f = ctx->functions()) {
              const char *vendor =
                  reinterpret_cast<const char *>(f->glGetString(0x1F00));  // GL_VENDOR
              const char *renderer =
                  reinterpret_cast<const char *>(f->glGetString(0x1F01));  // GL_RENDERER
              const char *version =
                  reinterpret_cast<const char *>(f->glGetString(0x1F02));  // GL_VERSION
              const QMutexLocker locker(&g_glMetaMutex);
              g_glVendorUtf8 = vendor ? QByteArray(vendor) : QByteArray();
              g_glRendererUtf8 = renderer ? QByteArray(renderer) : QByteArray();
              g_glVersionUtf8 = version ? QByteArray(version) : QByteArray();
            }
          }
          if (auto *ri = qw->rendererInterface()) {
            apiInt = static_cast<int>(ri->graphicsApi());
          }
          const qreal edpr = qw->effectiveDevicePixelRatio();
          const qint64 pw = qint64(qCeil(qreal(qw->width()) * edpr));
          const qint64 ph = qint64(qCeil(qreal(qw->height()) * edpr));
          const quint64 rgbaEst = quint64(qMax(pw, qint64(0))) * quint64(qMax(ph, qint64(0))) * 4u;
          qInfo().nospace().noquote()
              << "[Client][CrashDiag][RenderBudget] sceneGraphInitialized win=\"" << qw->title()
              << "\""
              << " (render thread; 不读取 QElapsedTimer 以避免跨线程)"
              << " QSGRendererInterface::GraphicsApi=" << apiInt << " (" << graphicsApiLabel(apiInt)
              << ")"
              << " isApiRhiBased="
              << (apiInt >= 0 ? QSGRendererInterface::isApiRhiBased(
                                    static_cast<QSGRendererInterface::GraphicsApi>(apiInt))
                              : false)
              << " GL_MAX_TEXTURE_SIZE=" << maxTex << " GL_MAX_VIEWPORT_DIMS=" << vpDims[0] << "x"
              << vpDims[1] << " surfaceFormat(" << fmtStr << ")"
              << " logical=" << qw->width() << "x" << qw->height() << " edpr=" << edpr
              << " ceilPhys≈" << pw << "x" << ph << " fullFrameRGBA32≈" << rgbaEst << " B";
        },
        Qt::DirectConnection);
    ++hooked;
  }
  if (hooked == 0) {
    qWarning().noquote() << "[Client][CrashDiag] 未发现 QQuickWindow，sceneGraphError 未连接（检查 "
                            "QML 根是否为 Window）";
  } else {
    qInfo().noquote() << "[Client][CrashDiag] QQuickWindow::sceneGraphError 已连接 count="
                      << hooked;
  }
}

}  // namespace ClientCrashDiagnostics
