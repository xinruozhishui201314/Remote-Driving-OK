#include "client_app_bootstrap.h"

#include "core/metricscollector.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIODevice>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QProcessEnvironment>
#include <QScreen>
#include <QSurfaceFormat>
#include <QVariant>
#include <QtGlobal>

#include <atomic>
#include <cstdio>

static bool g_lastHardwarePresentationOk = false;
/** 最近一次 OpenGL 探测的 GL_RENDERER（或 skipped/empty），供透底类故障一行指纹日志使用。 */
static QString g_lastGlProbeRendererForDiag;
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QVariant>

#ifdef ENABLE_QT6_QUICKCONTROLS2
#include <QtQuickControls2/QQuickStyle>
#endif

#include "app/client_present_composition_diag.h"

#include "authmanager.h"
#include "core/eventbus.h"
#include "core/faultmanager.h"
#include "core/networkqualityaggregator.h"
#include "core/systemstatemachine.h"
#include "core/tracing.h"
#include "mqttcontroller.h"
#include "nodehealthchecker.h"
#include "services/degradationmanager.h"
#include "services/safetymonitorservice.h"
#include "services/sessionmanager.h"
#include "services/vehiclecontrolservice.h"
#include "vehiclemanager.h"
#include "vehiclestatus.h"
#include "webrtcstreammanager.h"

namespace ClientApp {
namespace {

WindowFramePolicyResult g_lastWindowFramePolicyResult{};

WindowFramePolicyInputs captureWindowFramePolicyInputs() {
  WindowFramePolicyInputs in;
  in.platformName = QGuiApplication::platformName();
  in.dockerEnvFileExists = QFile::exists(QStringLiteral("/.dockerenv"));
  QFile cg(QStringLiteral("/proc/self/cgroup"));
  if (cg.open(QIODevice::ReadOnly)) {
    const QByteArray raw = cg.read(16384);
    in.procSelfCgroupSnippet = QString::fromUtf8(raw);
  }
  in.environment = QProcessEnvironment::systemEnvironment();
  return in;
}

void logWindowFramePolicyVerbose(const WindowFramePolicyInputs &wfIn,
                                 const WindowFramePolicyResult &wfRes) {
  const QString cgPreview = wfIn.procSelfCgroupSnippet.left(220).simplified();
  qInfo().noquote() << "[Client][WindowPolicy][Inputs] platform=" << wfIn.platformName
                    << " /.dockerenv="
                    << (wfIn.dockerEnvFileExists ? QLatin1String("true") : QLatin1String("false"))
                    << " cgroupHit="
                    << (wfRes.cgroupHit ? QLatin1String("true") : QLatin1String("false"))
                    << " likelyContainer="
                    << (wfRes.likelyContainerRuntime ? QLatin1String("true")
                                                     : QLatin1String("false"))
                    << " cgroupPreview=" << cgPreview;
  const QProcessEnvironment &e = wfIn.environment;
  qInfo().noquote() << "[Client][WindowPolicy][Env] CLIENT_USE_WINDOW_FRAME="
                    << e.value(QStringLiteral("CLIENT_USE_WINDOW_FRAME"))
                    << " CLIENT_DISABLE_FRAMELESS="
                    << e.value(QStringLiteral("CLIENT_DISABLE_FRAMELESS"))
                    << " CLIENT_FORCE_FRAMELESS="
                    << e.value(QStringLiteral("CLIENT_FORCE_FRAMELESS"))
                    << " CLIENT_IN_CONTAINER=" << e.value(QStringLiteral("CLIENT_IN_CONTAINER"))
                    << " CLIENT_AUTO_WINDOW_FRAME_FOR_CONTAINER="
                    << e.value(QStringLiteral("CLIENT_AUTO_WINDOW_FRAME_FOR_CONTAINER"));
  qInfo().noquote() << "[Client][WindowPolicy][Decision] useWindowFrame=" << wfRes.useWindowFrame
                    << " reason=" << wfRes.decisionReason;
  if (wfRes.useWindowFrame) {
    qInfo().noquote() << "[Client][WindowPolicy][Mitigation] 系统窗口装饰已启用（降低 Docker+X11 "
                         "下无框窗客户区透出宿桌面风险）";
  } else {
    qWarning().noquote() << "[Client][WindowPolicy][Risk] FramelessWindowHint "
                            "将启用。Docker+X11/部分合成器下客户区可能透出宿桌面。"
                            " 规避: CLIENT_USE_WINDOW_FRAME=1 ；容器内关闭自动: "
                            "CLIENT_AUTO_WINDOW_FRAME_FOR_CONTAINER=0 ；"
                            " 强制无框: CLIENT_FORCE_FRAMELESS=1";
  }
}

}  // namespace

QUrl resolveQmlMainUrl(QQmlApplicationEngine *engine) {
  QStringList possiblePaths = {QCoreApplication::applicationDirPath() + "/../qml/main.qml",
                               QCoreApplication::applicationDirPath() + "/../../qml/main.qml",
                               QCoreApplication::applicationDirPath() + "/qml/main.qml",
                               "qml/main.qml",
                               "../qml/main.qml",
                               "/workspaces/Remote-Driving/client/qml/main.qml",
                               "/workspace/client/qml/main.qml"};
  for (const QString &qmlPath : possiblePaths) {
    if (QFile::exists(qmlPath)) {
      QFileInfo fileInfo(qmlPath);
      QUrl url = QUrl::fromLocalFile(fileInfo.absoluteFilePath());
      QString qmlDir = fileInfo.absolutePath();
      // QML 根目录须在 import path 中；子目录组件引用 RemoteDriving 单例（AppContext
      // 等）时还需在对应 .qml 首行 import ".." / "../.."（见 client/qml/qmldir 注释）
      engine->addImportPath(qmlDir);
      qDebug() << "[QML_LOAD] ✓ main.qml:" << fileInfo.absoluteFilePath();
      return url;
    }
  }
  return QUrl();
}

void registerQmlTypes() {
  // WebRtcClient / RemoteVideoSurface / MqttController / VehicleStatus / AuthManager /
  // VehicleManager：由 CMake qt_add_qml_module(URI RemoteDriving) + QML_ELEMENT 静态注册。
}

void setupApplicationChrome(QGuiApplication &app, QString &outChineseFont) {
  app.setApplicationName("Remote Driving Client");
  app.setApplicationVersion("1.0.0");
  app.setOrganizationName("RemoteDriving");

  QFontDatabase fontDb;
  QStringList fontFamilies = fontDb.families();
  QStringList preferredFonts = {
      "WenQuanYi Zen Hei",  "WenQuanYi Micro Hei", "Noto Sans CJK SC", "Noto Sans CJK TC",
      "Source Han Sans SC", "Droid Sans Fallback", "SimHei",           "Microsoft YaHei"};

  for (const QString &font : preferredFonts) {
    if (fontFamilies.contains(font)) {
      outChineseFont = font;
      qDebug() << "Using Chinese font:" << font;
      break;
    }
  }
  if (outChineseFont.isEmpty()) {
    qWarning() << "No Chinese font found, Chinese text may not display correctly";
  } else {
    QFont defaultFont(outChineseFont, 12);
    app.setFont(defaultFont);
    qDebug() << "Default font set to:" << outChineseFont;
  }

#ifdef ENABLE_QT6_QUICKCONTROLS2
  QQuickStyle::setStyle("Material");
#endif
}

void logGuiPlatformDiagnostics() {
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  auto envOrDash = [&env](const char *key) -> QString {
    const QString v = env.value(QString::fromLatin1(key));
    return v.isEmpty() ? QStringLiteral("-") : v;
  };

  qInfo().noquote() << "[Client][PlatformDiag] QGuiApplication::platformName()="
                    << QGuiApplication::platformName();
  if (QScreen *ps = QGuiApplication::primaryScreen()) {
    const QRect g = ps->geometry();
    qInfo().noquote() << "[Client][PlatformDiag] primaryScreen name=" << ps->name()
                      << " geometry=" << g.x() << g.y() << g.width() << 'x' << g.height()
                      << " dpr=" << ps->devicePixelRatio();
  } else {
    qInfo().noquote() << "[Client][PlatformDiag] primaryScreen=null";
  }
  qInfo().noquote() << "[Client][PlatformDiag] screens=" << QGuiApplication::screens().size();

  // 显示栈环境（对照 Qt xcb 插件 / libxcb：xcb_connection_has_error 等）
  qInfo().noquote() << "[Client][PlatformDiag] DISPLAY=" << envOrDash("DISPLAY")
                    << " WAYLAND_DISPLAY=" << envOrDash("WAYLAND_DISPLAY")
                    << " XDG_SESSION_TYPE=" << envOrDash("XDG_SESSION_TYPE");
  qInfo().noquote() << "[Client][PlatformDiag] QT_QPA_PLATFORM=" << envOrDash("QT_QPA_PLATFORM")
                    << " QT_XCB_GL_INTEGRATION=" << envOrDash("QT_XCB_GL_INTEGRATION");
  qInfo().noquote() << "[Client][PlatformDiag] LIBGL_ALWAYS_SOFTWARE="
                    << envOrDash("LIBGL_ALWAYS_SOFTWARE")
                    << " MESA_GL_VERSION_OVERRIDE=" << envOrDash("MESA_GL_VERSION_OVERRIDE");
  qInfo().noquote() << "[Client][PlatformDiag] __GLX_VENDOR_LIBRARY_NAME="
                    << envOrDash("__GLX_VENDOR_LIBRARY_NAME");
  // Qt Quick / RHI（对照 Scene Graph 走 GL 还是 Vulkan 等；与 X11 REQ_LEN_EXCEED 根因排查相关）
  // 环境变量说明见 Qt 文档：Qt Quick Scene Graph、QSG_RHI_BACKEND、Running on Vulkan 等章节。
  qInfo().noquote() << "[Client][PlatformDiag] QSG_RHI_BACKEND=" << envOrDash("QSG_RHI_BACKEND")
                    << " QSG_INFO=" << envOrDash("QSG_INFO")
                    << " QT_QUICK_BACKEND=" << envOrDash("QT_QUICK_BACKEND");
  qInfo().noquote()
      << "[Client][PlatformDiag] 若遇 X11 断开：设 CLIENT_QPA_XCB_DEBUG=1 打开 qt.qpa.xcb.debug；"
         "设 CLIENT_X11_DEEP_DIAG=1 额外启用单包上限/窗口像素粗算/GL 字符串/帧采样（见 "
         "client_x11_deep_diag）";
}

namespace {

QString glGetStringLatin1(QOpenGLFunctions *f, GLenum name) {
  if (!f)
    return QStringLiteral("(no QOpenGLFunctions)");
  const auto *raw = reinterpret_cast<const char *>(f->glGetString(name));
  if (!raw || !raw[0])
    return QStringLiteral("(null)");
  return QString::fromLatin1(raw);
}

bool envTruthyUi(const QString &v) {
  const QString s = v.trimmed().toLower();
  return s == QLatin1String("1") || s == QLatin1String("true") || s == QLatin1String("yes") ||
         s == QLatin1String("on");
}

bool rendererStringLooksLikeSoftwareRaster(const QString &renderer) {
  const QString r = renderer.toLower();
  return r.contains(QLatin1String("llvmpipe")) || r.contains(QLatin1String("softpipe")) ||
         r.contains(QLatin1String("software rasterizer")) ||
         r.contains(QLatin1String("lavapipe")) || r.contains(QLatin1String("swrast"));
}

bool hasInteractiveDisplaySession() {
#if !defined(Q_OS_LINUX)
  return true;
#else
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (!env.value(QStringLiteral("DISPLAY")).trimmed().isEmpty())
    return true;
  if (!env.value(QStringLiteral("WAYLAND_DISPLAY")).trimmed().isEmpty())
    return true;
  return false;
#endif
}

/**
 * 远控台默认：Linux 下 X11(xcb) 交互会话要求硬件呈现，避免默认误用 llvmpipe 跑四路视频。
 * Wayland / 离屏 / CI：不满足条件则不要求。
 */
bool defaultInteractiveLinuxXcbHardwarePresentationRequired() {
#if !defined(Q_OS_LINUX)
  return false;
#else
  if (!hasInteractiveDisplaySession())
    return false;
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const QString plat = env.value(QStringLiteral("QT_QPA_PLATFORM")).trimmed().toLower();
  if (!plat.isEmpty() && plat != QLatin1String("xcb"))
    return false;
  return true;
#endif
}

}  // namespace

void applyPresentationSurfaceFormatDefaults() {
  if (qEnvironmentVariableIntValue("CLIENT_GL_DEFAULT_FORMAT_SKIP") == 1) {
    std::fprintf(stderr,
                 "[Client][DisplayPolicy] CLIENT_GL_DEFAULT_FORMAT_SKIP=1 — 跳过 "
                 "QSurfaceFormat::setDefaultFormat\n");
    return;
  }

  QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
  if (fmt.renderableType() == QSurfaceFormat::DefaultRenderableType)
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
  if (fmt.majorVersion() == 0) {
    fmt.setMajorVersion(2);
    fmt.setMinorVersion(0);
  }

  bool okInterval = false;
  const int interval = qEnvironmentVariableIntValue("CLIENT_GL_SWAP_INTERVAL", &okInterval);
  fmt.setSwapInterval(okInterval ? interval : 1);

  // 不透明默认帧缓冲：减少 X11/xcb 下与无框窗、合成器叠加时的「客户区透底」风险（与 WindowPolicy
  // 自动系统窗框互补）。 覆盖：CLIENT_GL_ALPHA_BUFFER_SIZE=-1 保留 Qt 默认；0（默认）关闭 alpha
  // 缓冲。
  {
    bool okAlpha = false;
    const int alphaSz = qEnvironmentVariableIntValue("CLIENT_GL_ALPHA_BUFFER_SIZE", &okAlpha);
    if (okAlpha && alphaSz < 0) {
      // 不调用 setAlphaBufferSize：使用 Qt 默认
    } else if (okAlpha) {
      fmt.setAlphaBufferSize(alphaSz);
    } else {
      fmt.setAlphaBufferSize(0);
    }
  }

  QSurfaceFormat::setDefaultFormat(fmt);
  std::fprintf(stderr,
               "[Client][DisplayPolicy] QSurfaceFormat 默认已设置: swapInterval=%d "
               "alphaBufferSize=%d（驱动可覆盖；"
               "CLIENT_GL_SWAP_INTERVAL / CLIENT_GL_ALPHA_BUFFER_SIZE(-1=Qt默认) "
               "覆盖，CLIENT_GL_DEFAULT_FORMAT_SKIP=1 跳过）\n",
               fmt.swapInterval(), fmt.alphaBufferSize());
}

OpenGlFramebufferProbeResult probeOpenGlDefaultFramebuffer() {
  OpenGlFramebufferProbeResult out;
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (env.value(QStringLiteral("CLIENT_SKIP_OPENGL_PROBE")).trimmed() == QStringLiteral("1")) {
    out.skipped = true;
    return out;
  }

  QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
  if (fmt.renderableType() == QSurfaceFormat::DefaultRenderableType)
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
  if (fmt.majorVersion() == 0) {
    fmt.setMajorVersion(2);
    fmt.setMinorVersion(0);
  }

  QOpenGLContext ctx;
  ctx.setFormat(fmt);
  if (!ctx.create())
    return out;

  QOffscreenSurface surface;
  surface.setFormat(ctx.format());
  surface.create();
  if (!surface.isValid())
    return out;

  if (!ctx.makeCurrent(&surface))
    return out;

  QOpenGLFunctions *const f = ctx.functions();
  if (f)
    f->initializeOpenGLFunctions();

  out.vendor = glGetStringLatin1(f, GL_VENDOR);
  out.renderer = glGetStringLatin1(f, GL_RENDERER);
  out.version = glGetStringLatin1(f, GL_VERSION);
  out.glslVersion = glGetStringLatin1(f, GL_SHADING_LANGUAGE_VERSION);
  out.rendererLooksSoftware = rendererStringLooksLikeSoftwareRaster(out.renderer);
  out.success = true;

  ctx.doneCurrent();
  return out;
}

namespace {

void logOpenGlHardwareRemediationHints() {
  qInfo().noquote()
      << "[Client][GLProbe][Remedy] 硬件 GL / 非 llvmpipe 路径："
      << " TELEOP_CLIENT_NVIDIA_GL=1 前须通过 scripts/verify-client-nvidia-gl-prereqs.sh（Xauthority/"
      << "DISPLAY/docker --gpus/compose）；overlay 见 docker-compose.client-nvidia-gl.yml 或 .deploy.yml；"
      << " 开发对照 bash scripts/run-client-on-host.sh；"
      << " 取证 CLIENT_X11_DEEP_DIAG=1 CLIENT_XWININFO_SNAPSHOT=1 CLIENT_QPA_XCB_DEBUG=1；"
      << " CLIENT_GL_ALPHA_BUFFER_SIZE 默认勿设或 0（与 applyPresentationSurfaceFormatDefaults）";
}

}  // namespace

void logOpenGlProbeResult(const OpenGlFramebufferProbeResult &r) {
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (r.skipped) {
    qInfo().noquote() << "[Client][GLProbe] skipped CLIENT_SKIP_OPENGL_PROBE=1";
    return;
  }
  if (!r.success) {
    qWarning().noquote()
        << "[Client][GLProbe] QOpenGLContext::create / QOffscreenSurface / makeCurrent 失败"
        << " platform=" << QGuiApplication::platformName()
        << " ★ 无可用 GL、无显示、或驱动拒绝离屏上下文；对照 DISPLAY/WAYLAND、Mesa/Vulkan";
    logOpenGlHardwareRemediationHints();
    return;
  }

  const QString libglSw = env.value(QStringLiteral("LIBGL_ALWAYS_SOFTWARE")).trimmed();
  const QSurfaceFormat df = QSurfaceFormat::defaultFormat();
  bool okAlphaEnv = false;
  const int alphaEnv = qEnvironmentVariableIntValue("CLIENT_GL_ALPHA_BUFFER_SIZE", &okAlphaEnv);
  const QString alphaEnvStr =
      okAlphaEnv ? QString::number(alphaEnv) : QStringLiteral("(unset→代码默认 opaque=0)");
  qInfo().noquote() << "[Client][GLProbe] GL_VENDOR=" << r.vendor;
  qInfo().noquote() << "[Client][GLProbe] GL_RENDERER=" << r.renderer;
  qInfo().noquote() << "[Client][GLProbe] GL_VERSION=" << r.version;
  qInfo().noquote() << "[Client][GLProbe] GL_SHADING_LANGUAGE_VERSION=" << r.glslVersion;
  qInfo().noquote() << "[Client][GLProbe] contextFormat=" << df.majorVersion() << '.'
                    << df.minorVersion() << " renderableType=" << int(df.renderableType())
                    << " defaultSwapInterval=" << df.swapInterval()
                    << " defaultFormat.alphaBufferSize(effective)=" << df.alphaBufferSize()
                    << " CLIENT_GL_ALPHA_BUFFER_SIZE(env)=" << alphaEnvStr
                    << " LIBGL_ALWAYS_SOFTWARE(env)="
                    << (libglSw.isEmpty() ? QStringLiteral("(unset)") : libglSw);
  if (df.alphaBufferSize() > 0) {
    qWarning().noquote() << "[Client][GLProbe] ★ alphaBufferSize>0 易与 X11 合成/透底问题混淆；"
                            "推荐默认 0（不设 CLIENT_GL_ALPHA_BUFFER_SIZE 或显式 0；-1 保留 Qt 默认）";
  }
  if (r.rendererLooksSoftware) {
    const QString xcbGl = env.value(QStringLiteral("QT_XCB_GL_INTEGRATION")).trimmed().toLower();
    const QString plat = QGuiApplication::platformName();
    qWarning().noquote() << "[Client][GLProbe] ★ GL_RENDERER 指示软件光栅（如 llvmpipe）→ 多路 "
                            "VideoOutput 易卡顿/闪烁；"
                            "优先硬件驱动或取消 LIBGL_ALWAYS_SOFTWARE；对照 Mesa envvars 文档";
    logOpenGlHardwareRemediationHints();
    if (plat == QLatin1String("xcb") && (xcbGl.isEmpty() || xcbGl == QLatin1String("xcb_egl"))) {
      qCritical().noquote()
          << "[Client][GLProbe][X11] ★ 软件光栅 + QT_XCB_GL_INTEGRATION="
          << (xcbGl.isEmpty() ? QStringLiteral("(默认/空→常为 xcb_egl)") : xcbGl)
          << " → 高 DPR 大窗首帧可能触发 libxcb XCB_CONN_CLOSED_REQ_LEN_EXCEED（单条 X 请求超过"
             " xcb_get_maximum_request_length）。须在重启进程前设 QT_XCB_GL_INTEGRATION=glx，"
             "或修正容器 GPU/DRI 使 EGL 可用真实硬件。参考 Qt 环境变量 QT_XCB_GL_INTEGRATION、"
             "libxcb xcb_connection_has_error 文档。";
    }
  }
}

void recordDisplayProbeMetrics(const OpenGlFramebufferProbeResult &r) {
  MetricsCollector &mc = MetricsCollector::instance();
  if (r.skipped) {
    mc.set(QStringLiteral("client_display_gl_probe_skipped"), 1.0);
    mc.set(QStringLiteral("client_display_gl_probe_success"), 0.0);
    mc.set(QStringLiteral("client_display_hw_presentation_ok"), 0.0);
    return;
  }
  if (!r.success) {
    mc.set(QStringLiteral("client_display_gl_probe_skipped"), 0.0);
    mc.set(QStringLiteral("client_display_gl_probe_success"), 0.0);
    mc.set(QStringLiteral("client_display_software_raster_detected"), 0.0);
    mc.set(QStringLiteral("client_display_hw_presentation_ok"), 0.0);
    return;
  }

  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const bool libglSw = envTruthyUi(env.value(QStringLiteral("LIBGL_ALWAYS_SOFTWARE")));

  mc.set(QStringLiteral("client_display_gl_probe_skipped"), 0.0);
  mc.set(QStringLiteral("client_display_gl_probe_success"), 1.0);
  mc.set(QStringLiteral("client_display_libgl_always_software"), libglSw ? 1.0 : 0.0);
  mc.set(QStringLiteral("client_display_software_raster_detected"),
         r.rendererLooksSoftware ? 1.0 : 0.0);
  const bool hwOk = !libglSw && !r.rendererLooksSoftware;
  mc.set(QStringLiteral("client_display_hw_presentation_ok"), hwOk ? 1.0 : 0.0);
}

int enforceHardwarePresentationGate(const OpenGlFramebufferProbeResult &r) {
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (envTruthyUi(env.value(QStringLiteral("CLIENT_ALLOW_SOFTWARE_PRESENTATION"))))
    return 0;
  if (envTruthyUi(env.value(QStringLiteral("CLIENT_GPU_PRESENTATION_OPTIONAL"))))
    return 0;

  const bool require =
      envTruthyUi(env.value(QStringLiteral("CLIENT_REQUIRE_HARDWARE_PRESENTATION"))) ||
      envTruthyUi(env.value(QStringLiteral("CLIENT_TELOP_STATION"))) ||
      defaultInteractiveLinuxXcbHardwarePresentationRequired();
  if (!require)
    return 0;

  if (!hasInteractiveDisplaySession()) {
    qInfo().noquote() << "[Client][DisplayGate] 无 DISPLAY/WAYLAND — 跳过硬件呈现门禁（离屏/CI）";
    return 0;
  }

  if (r.skipped) {
    qCritical().noquote()
        << "[Client][DisplayGate] 硬件呈现已要求（显式 TELOP_STATION / "
           "REQUIRE_HARDWARE_PRESENTATION，或 Linux+xcb 交互会话默认）"
           " 与 CLIENT_SKIP_OPENGL_PROBE=1 同时生效，无法验证 GL_RENDERER，拒绝启动。"
           " 请 unset CLIENT_SKIP_OPENGL_PROBE，或设 CLIENT_GPU_PRESENTATION_OPTIONAL=1 / "
           "CLIENT_ALLOW_SOFTWARE_PRESENTATION=1（仅调试）。";
    std::fprintf(stderr,
                 "[Client][DisplayGate] exit=78 ★ 硬件门禁需要 OpenGL 探测；见上条 qCritical。\n");
    return 78;
  }

  if (!r.success) {
    qCritical().noquote()
        << "[Client][DisplayGate] 硬件呈现门禁：OpenGL 离屏上下文不可用（探测失败）。"
           " 请检查显示服务器、GPU 驱动与 Qt 平台插件；或临时 "
           "CLIENT_ALLOW_SOFTWARE_PRESENTATION=1（仅调试）。";
    std::fprintf(stderr, "[Client][DisplayGate] exit=77\n");
    return 77;
  }

  if (envTruthyUi(env.value(QStringLiteral("LIBGL_ALWAYS_SOFTWARE")))) {
    qCritical().noquote()
        << "[Client][DisplayGate] 硬件呈现门禁：LIBGL_ALWAYS_SOFTWARE 已启用。"
           " 四路实时视频需要硬件合成链；请 unset 该变量并确保 glxinfo/驱动为硬件渲染器，"
           "或设置 CLIENT_ASSUME_HARDWARE_GL=1（Linux 预 QGuiApplication 策略），"
           "调试可设 CLIENT_ALLOW_SOFTWARE_PRESENTATION=1。";
    std::fprintf(stderr, "[Client][DisplayGate] exit=76\n");
    return 76;
  }

  if (r.rendererLooksSoftware) {
    qCritical().noquote() << "[Client][DisplayGate] 硬件呈现门禁：GL_RENDERER 判定为软件光栅（"
                          << r.renderer
                          << "）。请安装/启用 GPU 驱动，或按 client_display_runtime_policy 使用 "
                             "CLIENT_ASSUME_HARDWARE_GL=1；"
                             "调试可设 CLIENT_ALLOW_SOFTWARE_PRESENTATION=1。";
    std::fprintf(stderr, "[Client][DisplayGate] exit=75\n");
    return 75;
  }

  qInfo().noquote() << "[Client][DisplayGate] 硬件呈现门禁通过 GL_RENDERER=" << r.renderer;
  return 0;
}

int runDisplayEnvironmentCheck() {
  const OpenGlFramebufferProbeResult r = probeOpenGlDefaultFramebuffer();
  if (r.skipped) {
    g_lastGlProbeRendererForDiag = QStringLiteral("(probe_skipped)");
  } else {
    g_lastGlProbeRendererForDiag =
        r.renderer.isEmpty() ? QStringLiteral("(empty)") : r.renderer;
  }
  logOpenGlProbeResult(r);
  recordDisplayProbeMetrics(r);

  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

  if (!r.skipped && r.success) {
    const bool libglSw = envTruthyUi(env.value(QStringLiteral("LIBGL_ALWAYS_SOFTWARE")));
    g_lastHardwarePresentationOk = !libglSw && !r.rendererLooksSoftware;
  } else {
    g_lastHardwarePresentationOk = false;
  }

  // 交互式 GUI：OpenGL 基本探测必须成功（Quick SceneGraph 依赖 GL）；无头/offscreen 不强制
  {
    const QString plat = QGuiApplication::platformName();
    const bool headlessPlat = plat.compare(QLatin1String("offscreen"), Qt::CaseInsensitive) == 0 ||
                              plat.compare(QLatin1String("minimal"), Qt::CaseInsensitive) == 0;
    const bool allowFailed = envTruthyUi(env.value(QStringLiteral("CLIENT_ALLOW_FAILED_GL_PROBE")));
    if (!headlessPlat && !r.skipped && !r.success && !allowFailed) {
      qCritical().noquote()
          << "\n══════════════════════════════════════════════════════════════════════\n"
          << "[Client][StartupGate] FATAL exit=88 (GL_PROBE_REQUIRED_FAILED)\n"
          << "原因: 当前为交互式 GUI 平台 (" << plat << ")，OpenGL 默认帧缓冲探测失败。\n"
          << "      无法创建/激活离屏 GL 上下文，Qt Quick 通常无法正常渲染。\n"
          << "建议:\n"
          << "  1) 检查 GPU 驱动、libGL/EGL、DISPLAY/WAYLAND 与容器设备挂载(/dev/dri)\n"
          << "  2) CI/无 GPU: QT_QPA_PLATFORM=offscreen + CLIENT_SKIP_OPENGL_PROBE=1\n"
          << "  3) 仅调试: CLIENT_ALLOW_FAILED_GL_PROBE=1（不推荐生产）\n"
          << "══════════════════════════════════════════════════════════════════════\n";
      std::fprintf(stderr, "[Client][StartupGate] exit=88 (GL_PROBE_REQUIRED_FAILED)\n");
      return 88;
    }
  }

  return enforceHardwarePresentationGate(r);
}

bool lastHardwarePresentationEnvironmentOk() { return g_lastHardwarePresentationOk; }

void logOpenGlDefaultFramebufferProbe() { (void)runDisplayEnvironmentCheck(); }

void registerContextProperties(QQmlContext *ctx, AuthManager *authManager,
                               VehicleManager *vehicleManager, WebRtcClient *webrtcClient,
                               WebRtcStreamManager *webrtcStreamManager,
                               MqttController *mqttController, VehicleStatus *vehicleStatus,
                               NodeHealthChecker *nodeHealthChecker, EventBus *eventBus,
                               SystemStateMachine *systemStateMachine,
                               SessionManager *teleopSession, VehicleControlService *vehicleControl,
                               SafetyMonitorService *safetyMonitor,
                               NetworkQualityAggregator *networkQuality,
                               DegradationManager *degradationManager, Tracing *tracing,
                               const QString &applicationChineseFont,
                               QObject *videoIntegrityBannerBridge) {
  ctx->setContextProperty("authManager", authManager);
  ctx->setContextProperty("vehicleManager", vehicleManager);
  ctx->setContextProperty("webrtcClient", webrtcClient);
  ctx->setContextProperty("webrtcStreamManager", webrtcStreamManager);
  ctx->setContextProperty("mqttController", mqttController);
  ctx->setContextProperty("vehicleStatus", vehicleStatus);
  ctx->setContextProperty("nodeHealthChecker", nodeHealthChecker);
  ctx->setContextProperty("eventBus", eventBus);
  ctx->setContextProperty("systemStateMachine", systemStateMachine);
  ctx->setContextProperty("teleopSession", teleopSession);
  ctx->setContextProperty("vehicleControl", vehicleControl);
  ctx->setContextProperty("safetyMonitor", safetyMonitor);
  ctx->setContextProperty("networkQuality", networkQuality);
  ctx->setContextProperty("degradationManager", degradationManager);
  if (tracing) {
    ctx->setContextProperty("tracing", tracing);
  }
  bool autoConnectVideo =
      (QProcessEnvironment::systemEnvironment().value("CLIENT_AUTO_CONNECT_VIDEO").toInt() == 1);
  ctx->setContextProperty("autoConnectVideo", QVariant(autoConnectVideo));
  QString autoConnectTestVin = QProcessEnvironment::systemEnvironment()
                                   .value(QStringLiteral("CLIENT_AUTO_CONNECT_TEST_VIN"))
                                   .trimmed();
  if (autoConnectTestVin.isEmpty())
    autoConnectTestVin = QStringLiteral("123456789");
  ctx->setContextProperty(QStringLiteral("autoConnectTestVin"), autoConnectTestVin);
  ctx->setContextProperty("faultManager", &FaultManager::instance());
  ctx->setContextProperty("layoutDebugEnabled", QProcessEnvironment::systemEnvironment().value(
                                                    "CLIENT_LAYOUT_DEBUG") == "1");
  QString defaultServerUrlFromEnv =
      QProcessEnvironment::systemEnvironment().value("DEFAULT_SERVER_URL");
  if (defaultServerUrlFromEnv.isEmpty()) {
    defaultServerUrlFromEnv =
        QProcessEnvironment::systemEnvironment().value("REMOTE_DRIVING_SERVER");
  }
  if (defaultServerUrlFromEnv.isEmpty()) {
    defaultServerUrlFromEnv = QProcessEnvironment::systemEnvironment().value("BACKEND_URL");
  }
  ctx->setContextProperty("defaultServerUrlFromEnv", defaultServerUrlFromEnv);

  // QML 单例（AppContext）无法依赖 main.qml 的 id: window；用 rd_*
  // 根上下文别名避免与单例属性同名冲突
  ctx->setContextProperty(QStringLiteral("rd_authManager"), authManager);
  ctx->setContextProperty(QStringLiteral("rd_vehicleManager"), vehicleManager);
  ctx->setContextProperty(QStringLiteral("rd_webrtcClient"), webrtcClient);
  ctx->setContextProperty(QStringLiteral("rd_webrtcStreamManager"), webrtcStreamManager);
  ctx->setContextProperty(QStringLiteral("rd_mqttController"), mqttController);
  ctx->setContextProperty(QStringLiteral("rd_vehicleStatus"), vehicleStatus);
  ctx->setContextProperty(QStringLiteral("rd_vehicleControl"), vehicleControl);
  ctx->setContextProperty(QStringLiteral("rd_safetyMonitor"), safetyMonitor);
  ctx->setContextProperty(QStringLiteral("rd_networkQuality"), networkQuality);
  ctx->setContextProperty(QStringLiteral("rd_systemStateMachine"), systemStateMachine);
  ctx->setContextProperty(QStringLiteral("rd_teleopSession"), teleopSession);
  ctx->setContextProperty(QStringLiteral("rd_nodeHealthChecker"), nodeHealthChecker);
  ctx->setContextProperty(QStringLiteral("rd_applicationChineseFont"), applicationChineseFont);
  ctx->setContextProperty(QStringLiteral("rd_hardwarePresentationOk"),
                          QVariant(ClientApp::lastHardwarePresentationEnvironmentOk()));
  const WindowFramePolicyInputs wfIn = captureWindowFramePolicyInputs();
  g_lastWindowFramePolicyResult = evaluateWindowFramePolicy(wfIn);
  logWindowFramePolicyVerbose(wfIn, g_lastWindowFramePolicyResult);
  ctx->setContextProperty(QStringLiteral("rd_useWindowFrame"),
                          QVariant(g_lastWindowFramePolicyResult.useWindowFrame));
  ctx->setContextProperty(QStringLiteral("rd_windowFramePolicyReason"),
                          QVariant(g_lastWindowFramePolicyResult.decisionReason));
  ctx->setContextProperty(QStringLiteral("rd_videoIntegrityBannerBridge"),
                          videoIntegrityBannerBridge);
  // 遥操作排障：QML 键盘/焦点与 C++ 控制环对齐日志；调试阶段默认开启，CLIENT_TELEOP_TRACE=0 关闭
  {
    const QByteArray teleopTraceEnv = qgetenv("CLIENT_TELEOP_TRACE");
    const bool teleopTraceOn =
        teleopTraceEnv.isEmpty() ? true : (qEnvironmentVariableIntValue("CLIENT_TELEOP_TRACE") != 0);
    ctx->setContextProperty(QStringLiteral("rd_teleopTraceEnabled"), QVariant(teleopTraceOn));
  }
}

void logQmlEngineImportPaths(const QQmlEngine *engine) {
  if (!engine) {
    qWarning().noquote() << "[Client][QML][ImportPathList] engine=null";
    return;
  }
  const QStringList paths = engine->importPathList();
  qInfo().noquote() << "[Client][QML][ImportPathList] count=" << paths.size();
  for (int i = 0; i < paths.size(); ++i) {
    qInfo().noquote() << "[Client][QML][ImportPathList][" << i << "]=" << paths.at(i);
  }
}

void logQmlRootContextRdSnapshot(const QQmlContext *root) {
  if (!root) {
    qWarning().noquote() << "[Client][QML][rd_*] rootContext=null";
    return;
  }
  static const QStringList kRdObjectKeys = {
      QStringLiteral("rd_authManager"),        QStringLiteral("rd_vehicleManager"),
      QStringLiteral("rd_webrtcClient"),       QStringLiteral("rd_webrtcStreamManager"),
      QStringLiteral("rd_mqttController"),     QStringLiteral("rd_vehicleStatus"),
      QStringLiteral("rd_vehicleControl"),     QStringLiteral("rd_safetyMonitor"),
      QStringLiteral("rd_systemStateMachine"), QStringLiteral("rd_teleopSession"),
      QStringLiteral("rd_nodeHealthChecker"),
  };
  qInfo().noquote() << "[Client][QML][rd_*] snapshot begin ( QObject* 期望非空 )";
  for (const QString &key : kRdObjectKeys) {
    const QVariant v = root->contextProperty(key);
    if (!v.isValid()) {
      qCritical().noquote() << "[Client][QML][rd_*]" << key << "=INVALID";
      continue;
    }
    QObject *o = qvariant_cast<QObject *>(v);
    qInfo().noquote() << "[Client][QML][rd_*]" << key << "=" << (o ? "ok" : "null_QObject");
  }
  {
    const QVariant v = root->contextProperty(QStringLiteral("rd_applicationChineseFont"));
    qInfo().noquote() << "[Client][QML][rd_*] rd_applicationChineseFont valid=" << v.isValid()
                      << " len=" << (v.isValid() ? v.toString().size() : -1);
  }
  {
    const QVariant v = root->contextProperty(QStringLiteral("rd_hardwarePresentationOk"));
    qInfo().noquote() << "[Client][QML][rd_*] rd_hardwarePresentationOk=" << v.toBool()
                      << " valid=" << v.isValid();
  }
  {
    const QVariant v = root->contextProperty(QStringLiteral("rd_useWindowFrame"));
    qInfo().noquote() << "[Client][QML][rd_*] rd_useWindowFrame=" << v.toBool()
                      << " valid=" << v.isValid();
  }
  {
    const QVariant v = root->contextProperty(QStringLiteral("rd_windowFramePolicyReason"));
    qInfo().noquote() << "[Client][QML][rd_*] rd_windowFramePolicyReason=" << v.toString()
                      << " valid=" << v.isValid();
  }
  qInfo().noquote() << "[Client][QML][rd_*] snapshot end";
}

void installQmlEngineDiagnosticHooks(QQmlEngine *engine, QObject *lifecycleOwner) {
  if (!engine || !lifecycleOwner) {
    qWarning().noquote() << "[Client][QML][Hooks] skip install (null engine or owner)";
    return;
  }
  static std::atomic_int s_batchIndex{0};
  static std::atomic_int s_warningTotal{0};

  QObject::connect(
      engine, &QQmlEngine::warnings, lifecycleOwner,
      [](const QList<QQmlError> &warnings) {
        const int batch = ++s_batchIndex;
        const int n = warnings.size();
        s_warningTotal += n;
        qWarning().noquote() << "[Client][QML][Warnings] batch=" << batch << " count=" << n
                             << " sessionTotal=" << s_warningTotal.load();
        for (const QQmlError &e : warnings) {
          const QString msg = e.toString();
          const bool refApp = msg.contains(QLatin1String("ReferenceError"), Qt::CaseInsensitive) &&
                              msg.contains(QLatin1String("AppContext"), Qt::CaseInsensitive);
          if (refApp) {
            qCritical().noquote()
                << "[Client][QML][ReferenceError][AppContext] ★ 修复：子目录 QML 须在文件首增加 "
                   "import "
                   "\"..\"（shell、components）或 import \"../..\"（components/driving）。"
                   " 见 client/qml/qmldir 注释。 full="
                << msg;
          } else if (msg.contains(QLatin1String("ReferenceError"), Qt::CaseInsensitive)) {
            qCritical().noquote() << "[Client][QML][ReferenceError] full=" << msg;
          } else {
            qWarning().noquote() << "[Client][QML][Warning] url=" << e.url().toString()
                                 << " line=" << e.line() << " column=" << e.column()
                                 << " description=" << e.description();
          }
        }
      },
      Qt::DirectConnection);
}

void logQmlPostLoadSummary(const QQmlApplicationEngine *engine, const QUrl &mainUrl) {
  if (!engine) {
    qWarning().noquote() << "[Client][QML][PostLoad] engine=null";
    return;
  }
  const QList<QObject *> roots = engine->rootObjects();
  qInfo().noquote() << "[Client][QML][PostLoad] mainUrl=" << mainUrl.toString()
                    << " rootObjects.count=" << roots.size();
  for (int i = 0; i < roots.size(); ++i) {
    QObject *o = roots.at(i);
    const char *cls = o && o->metaObject() ? o->metaObject()->className() : "?";
    qInfo().noquote() << "[Client][QML][PostLoad] root[" << i << "] class=" << cls
                      << " objectName=" << (o ? o->objectName() : QString());
  }
  if (roots.isEmpty()) {
    qCritical().noquote() << "[Client][QML][PostLoad] rootObjects 为空（不应在 load 成功后调用）";
  }

  ClientPresentCompositionDiag::installIfEnabled(qGuiApp);

  // 「仅系统标题栏、客户区透出宿桌面」类故障发生在 X11/GL 合成层；QML 与 StartupGate 常为绿色但像素未正确上屏。
  if (qgetenv("LIBGL_ALWAYS_SOFTWARE") == QByteArrayLiteral("1")) {
    qInfo().noquote()
        << "[Client][PresentDiag][Hint] 软件光栅(LIBGL_ALWAYS_SOFTWARE=1)+xcb：容器+交互式下默认 15s "
           "[PresentHealth][1Hz]（总开关 CLIENT_AUTO_PRESENT_HEALTH=0 可关）；全程采样 "
           "CLIENT_PRESENT_HEALTH_1HZ=1；深度/visual CLIENT_XWININFO_SNAPSHOT=1 或 CLIENT_X11_DEEP_DIAG=1。"
           "异步日志每轮 drain 后刷盘；SIGINT/SIGTERM 经主线程 shutdown 落盘（仍建议正常退出）。";
  }
}

bool lastWindowFramePolicyUseWindowFrame() { return g_lastWindowFramePolicyResult.useWindowFrame; }

QString lastWindowFramePolicyReasonString() { return g_lastWindowFramePolicyResult.decisionReason; }

void logX11ClientAreaTransparencyFiveWhyHint() {
  auto *gui = qobject_cast<QGuiApplication *>(QGuiApplication::instance());
  if (!gui || gui->platformName() != QLatin1String("xcb")) {
    return;
  }
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const bool inDocker = QFile::exists(QStringLiteral("/.dockerenv"));
  const bool libglSw =
      env.value(QStringLiteral("LIBGL_ALWAYS_SOFTWARE")).trimmed() == QStringLiteral("1");
  const bool hwPres = lastHardwarePresentationEnvironmentOk();
  const bool framed = lastWindowFramePolicyUseWindowFrame();
  const QString reason = lastWindowFramePolicyReasonString();
  const int alpha = QSurfaceFormat::defaultFormat().alphaBufferSize();

  qInfo().nospace().noquote()
      << "[Client][CompositingRoot][5Why] 若见「仅窗框/标题栏、客户区像宿桌面」按链对照 — "
      << "Why1 视效=客户区未遮挡背后像素(X11 合成/缓冲未按预期呈现); "
      << "Why2 机制=Qt Quick SceneGraph(OpenGL)→Xcb 与混成器/驱动路径交互; "
      << "Why3 当前环境 docker=" << (inDocker ? QLatin1String("Y") : QLatin1String("N"))
      << " libgl_sw=" << (libglSw ? QLatin1String("Y") : QLatin1String("N"))
      << " hwPresentationOk=" << (hwPres ? QLatin1String("Y") : QLatin1String("N"))
      << " defaultFormatAlphaBuf=" << alpha << "; "
      << "Why4 已启用窗框缓解=" << (framed ? QLatin1String("Y") : QLatin1String("N"))
      << " (" << reason << "); "
      << "Why5 根因类属=显示栈/容器 GL(如 llvmpipe、宿主 NVIDIA 未进容器) 非业务 QML 未加载 — "
      << "处置: docker-compose.client-nvidia-gl.yml+Toolkit 或 bash scripts/run-client-on-host.sh; "
         "日志 [Client][X11Visual]/[Client][GLProbe]; 取证 CLIENT_X11_DEEP_DIAG=1 "
         "CLIENT_XWININFO_SNAPSHOT=1 CLIENT_QPA_XCB_DEBUG=1; CLIENT_GL_ALPHA_BUFFER_SIZE 勿设非 0";

  // 高危组合：与现场「只见窗框、客户区像宿桌面」强相关；单独 WARN 便于 grep，避免仅看 PresentHealth 误判。
  if (inDocker && libglSw && !hwPres) {
    qWarning().nospace().noquote()
        << "[Client][CompositingRoot][SymptomMap] 当前为 容器 + LIBGL_ALWAYS_SOFTWARE + hwPresentationOk=N。"
        << " 若肉眼见客户区透底/像桌面壁纸：属 X11+混成器+软件光栅合成路径问题，不是 QML 未加载（StartupGate/QML 已通过）。"
        << " [PresentHealth] 首秒可有 frameSwapped 增量，静态界面随后 delta_1s=0 为常态，不能据此否定上屏。"
        << " 处置优先序: (1) docker-compose.client-nvidia-gl.yml + 宿主 GPU 进容器 (2) scripts/run-client-on-host.sh "
        << "(3) CLIENT_X11_DEEP_DIAG=1 + CLIENT_XWININFO_SNAPSHOT=1 取证。";

    // 单行指纹：grep ROOT_CAUSE_CLASS_CLIENT_AREA / ClientAreaComposite 即可与「QML 坏了」区分
    qCritical().nospace().noquote()
        << "[Client][ClientAreaComposite][ROOT_CAUSE_CLASS_CLIENT_AREA] "
        << "若肉眼仅窗框正常、客户区像宿桌面壁纸：根因类属=环境(容器无硬件GL+X11混成+QtQuick SceneGraph GL)，"
        << "不是 main.qml 未加载。指纹: docker=Y LIBGL_ALWAYS_SOFTWARE=1 hwPresentationOk=N "
        << "GL_RENDERER=" << g_lastGlProbeRendererForDiag
        << " | 对策: (1) GPU+驱动进容器+TELEOP_CLIENT_NVIDIA_GL / docker-compose.client-nvidia-gl.yml "
        << "(2) bash scripts/run-client-on-host.sh (3) 取证 CLIENT_X11_DEEP_DIAG=1 CLIENT_XWININFO_SNAPSHOT=1";
  }
}

}  // namespace ClientApp
