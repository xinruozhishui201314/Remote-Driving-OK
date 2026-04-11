#include "client_platform_gate.h"

#include "app/client_app_bootstrap.h"

#include <QFile>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQuickWindow>
#include <QScreen>

#include <cstdio>

namespace ClientApp {
namespace {

bool envTruthy(const QString &v) {
  const QString s = v.trimmed().toLower();
  return s == QLatin1String("1") || s == QLatin1String("true") || s == QLatin1String("yes") ||
         s == QLatin1String("on");
}

bool isHeadlessPlatformPlugin(const QString &plat) {
  const QString p = plat.toLower();
  return p == QLatin1String("offscreen") || p == QLatin1String("minimal");
}

bool willLikelyUseXcbOnLinux(const QProcessEnvironment &env) {
  const QString qpa = env.value(QStringLiteral("QT_QPA_PLATFORM")).trimmed().toLower();
  if (qpa == QLatin1String("offscreen") || qpa == QLatin1String("minimal")) {
    return false;
  }
  if (qpa.contains(QLatin1String("wayland")) && !qpa.contains(QLatin1String("xcb"))) {
    return false;
  }
  if (qpa.isEmpty() || qpa == QLatin1String("xcb") || qpa.startsWith(QLatin1String("xcb:"))) {
    return true;
  }
  return false;
}

bool willLikelyUsePureWaylandOnLinux(const QProcessEnvironment &env) {
  const QString qpa = env.value(QStringLiteral("QT_QPA_PLATFORM")).trimmed().toLower();
  return qpa.contains(QLatin1String("wayland")) && !qpa.contains(QLatin1String("xcb"));
}

void printPreDisplayFailureHelp() {
  std::fprintf(
      stderr,
      "\n"
      "══════════════════════════════════════════════════════════════════════\n"
      "[Client][StartupGate] FATAL exit=81 (PRE_XCB_DISPLAY_MISSING)\n"
      "原因: 已判定将使用 X11/xcb 平台插件，但环境变量 DISPLAY 未设置或为空。\n"
      "      无 DISPLAY 时无法连接 X Server，Qt Quick 窗口无法创建。\n"
      "建议:\n"
      "  1) 图形会话: export DISPLAY=:0 或 :1（与宿主机 echo \\$DISPLAY 一致）\n"
      "  2) Docker+X11: 挂载 /tmp/.X11-unix、设置 -e DISPLAY，宿主 xhost +local:docker 等\n"
      "  3) 无头/CI: export QT_QPA_PLATFORM=offscreen\n"
      "  4) 仅调试绕过: CLIENT_ALLOW_MISSING_DISPLAY=1 或 CLIENT_SKIP_PLATFORM_GATE=1\n"
      "══════════════════════════════════════════════════════════════════════\n\n");
}

void printPreWaylandFailureHelp() {
  std::fprintf(
      stderr,
      "\n"
      "══════════════════════════════════════════════════════════════════════\n"
      "[Client][StartupGate] FATAL exit=82 (PRE_WAYLAND_DISPLAY_MISSING)\n"
      "原因: QT_QPA_PLATFORM 指向纯 Wayland，但 WAYLAND_DISPLAY 未设置或为空。\n"
      "      无法连接 Wayland compositor，GUI 无法启动。\n"
      "建议:\n"
      "  1) 在 Wayland 会话中运行，或 export WAYLAND_DISPLAY=wayland-0 等\n"
      "  2) 改用 X11: QT_QPA_PLATFORM=xcb 并配置 DISPLAY\n"
      "  3) 仅调试: CLIENT_ALLOW_MISSING_WAYLAND_DISPLAY=1 或 CLIENT_SKIP_PLATFORM_GATE=1\n"
      "══════════════════════════════════════════════════════════════════════\n\n");
}

void printPostPlatformEmptyHelp() {
  std::fprintf(stderr,
               "\n"
               "══════════════════════════════════════════════════════════════════════\n"
               "[Client][StartupGate] FATAL exit=83 (POST_PLATFORM_NAME_EMPTY)\n"
               "原因: QGuiApplication::platformName() 为空，Qt 平台插件未正确加载或初始化失败。\n"
               "建议: 检查 QT_QPA_PLATFORM、QT_PLUGIN_PATH、依赖库（libxcb*、libwayland*）。\n"
               "══════════════════════════════════════════════════════════════════════\n\n");
}

void printPostNoScreensHelp() {
  std::fprintf(
      stderr,
      "\n"
      "══════════════════════════════════════════════════════════════════════\n"
      "[Client][StartupGate] FATAL exit=84 (POST_NO_SCREEN_INTERACTIVE)\n"
      "原因: 当前为交互式 GUI 平台（非 offscreen/minimal），但 QGuiApplication::screens()\n"
      "      为空或 primaryScreen() 为空，无法布局窗口。\n"
      "建议: 启动虚拟显示(Xvfb)、接入物理显示器/远程桌面，或改用 QT_QPA_PLATFORM=offscreen。\n"
      "      应急: CLIENT_SKIP_PLATFORM_GATE=1（不推荐生产）\n"
      "══════════════════════════════════════════════════════════════════════\n\n");
}

void printQmlNoWindowHelp() {
  std::fprintf(stderr,
               "[Client][StartupGate] FATAL exit=87 — 见日志 [Client][PlatformGate] "
               "POST_QML_NO_QUICK_WINDOW\n");
}

void printQmlFrameMismatchHelp() {
  std::fprintf(stderr,
               "[Client][StartupGate] FATAL exit=86 — 见日志 POST_QML_FRAMELESS_POLICY_MISMATCH\n");
}

}  // namespace

void logStartupMandatoryGatesBrief() {
  qInfo().noquote()
      << "[Client][StartupGate] 必过清单（任一项失败即退出，见对应 exit 码与 stderr 框线）:\n"
      << "  [PRE]  Linux+xcb → DISPLAY 已设置 (81)；纯 Wayland → WAYLAND_DISPLAY (82)\n"
      << "  [POST] platformName 非空 (83)；交互式 GUI → 至少一块屏幕且 primaryScreen (84)\n"
      << "  [GL]   交互式且未跳过探测 → OpenGL 基本探测成功 (88)；硬件门禁见 75–78\n"
      << "  [QML]  main.qml 可解析 (91)；load 成功 (93)；根对象创建成功 (94)；顶层 QQuickWindow "
         "(87)；"
         "窗框策略一致 (86)\n"
      << "  [CFG]  配置 URL 可解析（95）；档位见 CLIENT_STARTUP_READINESS_PROFILE（容器默认 full→校验 "
         "KC+ZLM）；见 client_startup_readiness_gate.h\n"
      << "  [TCP]  默认目标随档位；显式 CLIENT_STARTUP_TCP_TARGETS=…（96）；见 "
         "client_startup_tcp_gate.h\n"
      << "  [X11]  QML 后自动 [Client][X11Visual]（depth/visual）；关闭 "
         "CLIENT_X11_VISUAL_PROBE=0；见 client_x11_visual_probe.h\n"
      << "  绕过: CLIENT_SKIP_PLATFORM_GATE=1 / CLIENT_ALLOW_MISSING_DISPLAY=1 / "
         "CLIENT_SKIP_CONFIG_READINESS_GATE=1 / CLIENT_STARTUP_TCP_GATE=0 等"
         "（见 client_platform_gate.h）";
}

void logStartupAllMandatoryGatesOk() {
  qInfo().noquote() << "[Client][StartupGate] 全部必过项已通过，进入事件循环 (app.exec)";
}

int runPreQGuiApplicationPlatformGate() {
#if !defined(Q_OS_LINUX)
  return 0;
#else
  if (qEnvironmentVariableIntValue("CLIENT_SKIP_PLATFORM_GATE") == 1) {
    std::fprintf(stderr, "[Client][StartupGate] CLIENT_SKIP_PLATFORM_GATE=1 — 跳过 PRE_QGUI\n");
    return 0;
  }

  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

  if (willLikelyUsePureWaylandOnLinux(env)) {
    if (envTruthy(env.value(QStringLiteral("CLIENT_ALLOW_MISSING_WAYLAND_DISPLAY")))) {
      std::fprintf(stderr,
                   "[Client][StartupGate][WARN] CLIENT_ALLOW_MISSING_WAYLAND_DISPLAY=1 — 跳过 "
                   "WAYLAND 检查\n");
      return 0;
    }
    const QByteArray wd = qgetenv("WAYLAND_DISPLAY");
    if (wd.trimmed().isEmpty()) {
      printPreWaylandFailureHelp();
      return 82;
    }
    return 0;
  }

  if (!willLikelyUseXcbOnLinux(env)) {
    return 0;
  }

  if (envTruthy(env.value(QStringLiteral("CLIENT_ALLOW_MISSING_DISPLAY")))) {
    std::fprintf(
        stderr, "[Client][StartupGate][WARN] CLIENT_ALLOW_MISSING_DISPLAY=1 — 跳过 DISPLAY 检查\n");
    return 0;
  }

  const QByteArray disp = qgetenv("DISPLAY");
  if (disp.trimmed().isEmpty()) {
    printPreDisplayFailureHelp();
    return 81;
  }
  return 0;
#endif
}

int runPostQGuiApplicationPlatformGate(QGuiApplication &app) {
  Q_UNUSED(app);
  if (qEnvironmentVariableIntValue("CLIENT_SKIP_PLATFORM_GATE") == 1) {
    qWarning().noquote() << "[Client][StartupGate] CLIENT_SKIP_PLATFORM_GATE=1 — 跳过 POST_QGUI";
    return 0;
  }

  const QString plat = QGuiApplication::platformName();
  if (plat.isEmpty()) {
    printPostPlatformEmptyHelp();
    qCritical().noquote() << "[Client][StartupGate] platformName 为空 exit=83";
    return 83;
  }

  if (isHeadlessPlatformPlugin(plat)) {
    qInfo().noquote() << "[Client][StartupGate] 无头平台" << plat << "— 跳过「必须有屏幕」检查";
    return 0;
  }

  const QList<QScreen *> screens = QGuiApplication::screens();
  if (screens.isEmpty() || QGuiApplication::primaryScreen() == nullptr) {
    printPostNoScreensHelp();
    qCritical().noquote() << "[Client][StartupGate] 交互式 GUI 无可用屏幕 exit=84 screens="
                          << screens.size();
    return 84;
  }

  qInfo().noquote() << "[Client][StartupGate] POST_QGUI 通过 platform=" << plat
                    << " screens=" << screens.size();
  return 0;
}

int enforceQmlLoadedPlatformGate(const QList<QObject *> &rootObjects) {
  if (qEnvironmentVariableIntValue("CLIENT_SKIP_PLATFORM_GATE") == 1) {
    qWarning().noquote()
        << "[Client][StartupGate] CLIENT_SKIP_PLATFORM_GATE=1 — 跳过 QML 后窗口门禁";
    return 0;
  }

  const QString plat = QGuiApplication::platformName();
  const bool headless = isHeadlessPlatformPlugin(plat);

  const bool policyUseWindowFrame = lastWindowFramePolicyUseWindowFrame();
  const QString policyReason = lastWindowFramePolicyReasonString();

  int winCount = 0;
  bool anyMismatch = false;
  for (QObject *o : rootObjects) {
    auto *w = qobject_cast<QQuickWindow *>(o);
    if (!w) {
      continue;
    }
    ++winCount;
    const Qt::WindowFlags wf = w->flags();
    const bool hasFrameless = (wf & Qt::FramelessWindowHint) != 0;
    const bool expectFrameless = !policyUseWindowFrame;
    const bool bindingMatchesPolicy = (hasFrameless == expectFrameless);
    if (!bindingMatchesPolicy) {
      anyMismatch = true;
    }

    qInfo().noquote() << "[Client][WindowPolicy][PostLoadVerify] idx=" << winCount
                      << " title=" << w->title() << " flags=0x"
                      << QString::number(static_cast<quint64>(static_cast<int>(wf)), 16)
                      << " hasFramelessHint="
                      << (hasFrameless ? QLatin1String("true") : QLatin1String("false"))
                      << " policyUseWindowFrame="
                      << (policyUseWindowFrame ? QLatin1String("true") : QLatin1String("false"))
                      << " expectFramelessHint="
                      << (expectFrameless ? QLatin1String("true") : QLatin1String("false"))
                      << " qmlFlagsMatchPolicy="
                      << (bindingMatchesPolicy ? QLatin1String("OK") : QLatin1String("MISMATCH"));
  }

  if (winCount == 0) {
    printQmlNoWindowHelp();
    qCritical().noquote()
        << "\n══════════════════════════════════════════════════════════════════════\n"
        << "[Client][StartupGate] FATAL exit=87 (POST_QML_NO_QUICK_WINDOW)\n"
        << "原因: engine.load 成功后根对象中未找到 QQuickWindow。\n"
        << "      本客户端必须以 ApplicationWindow（或 QQuickWindow）为根才能呈现 UI。\n"
        << "建议: 检查 main.qml 根类型与加载路径。\n"
        << "══════════════════════════════════════════════════════════════════════\n";
    return 87;
  }

  if (!headless && policyUseWindowFrame && anyMismatch) {
    printQmlFrameMismatchHelp();
    qCritical().noquote()
        << "\n══════════════════════════════════════════════════════════════════════\n"
        << "[Client][StartupGate] FATAL exit=86 (POST_QML_FRAMELESS_POLICY_MISMATCH)\n"
        << "原因: 策略要求系统窗框 (useWindowFrame=true)，但窗口仍带 Qt::FramelessWindowHint。\n"
        << "      Docker+X11 下易出现客户区透出宿桌面。策略原因: " << policyReason << "\n"
        << "建议: 见日志；或 CLIENT_USE_WINDOW_FRAME=1；应急 CLIENT_SKIP_PLATFORM_GATE=1\n"
        << "══════════════════════════════════════════════════════════════════════\n";
    return 86;
  }

  if (anyMismatch && headless) {
    qWarning().noquote() << "[Client][StartupGate][WARN] 无头平台下 Frameless/策略不一致，不判失败";
  }

  logX11ClientAreaTransparencyFiveWhyHint();
  qInfo().noquote() << "[Client][StartupGate] QML 窗口门禁通过 QuickRoot 数=" << winCount
                    << " reason=" << policyReason;
  return 0;
}

}  // namespace ClientApp
