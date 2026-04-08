#include "client_app_bootstrap.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QScreen>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QVariant>

#ifdef ENABLE_QT6_QUICKCONTROLS2
#include <QtQuickControls2/QQuickStyle>
#endif

#include "authmanager.h"
#include "mqttcontroller.h"
#include "nodehealthchecker.h"
#include "vehiclemanager.h"
#include "vehiclestatus.h"
#include "webrtcclient.h"
#include "webrtcstreammanager.h"
#include "core/eventbus.h"
#include "core/systemstatemachine.h"
#include "core/networkqualityaggregator.h"
#include "core/tracing.h"
#include "services/degradationmanager.h"
#include "services/safetymonitorservice.h"
#include "services/sessionmanager.h"
#include "services/vehiclecontrolservice.h"

namespace ClientApp {

QUrl resolveQmlMainUrl(QQmlApplicationEngine *engine)
{
    QStringList possiblePaths = {
        QCoreApplication::applicationDirPath() + "/../qml/main.qml",
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
            engine->addImportPath(qmlDir);
            qDebug() << "[QML_LOAD] ✓ main.qml:" << fileInfo.absoluteFilePath();
            return url;
        }
    }
    return QUrl();
}

void registerQmlTypes()
{
    qmlRegisterType<WebRtcClient>("RemoteDriving", 1, 0, "WebRtcClient");
    qmlRegisterType<MqttController>("RemoteDriving", 1, 0, "MqttController");
    qmlRegisterType<VehicleStatus>("RemoteDriving", 1, 0, "VehicleStatus");
    qmlRegisterType<AuthManager>("RemoteDriving", 1, 0, "AuthManager");
    qmlRegisterType<VehicleManager>("RemoteDriving", 1, 0, "VehicleManager");
}

void setupApplicationChrome(QGuiApplication &app, QString &outChineseFont)
{
    app.setApplicationName("Remote Driving Client");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("RemoteDriving");

    QFontDatabase fontDb;
    QStringList fontFamilies = fontDb.families();
    QStringList preferredFonts = {
        "WenQuanYi Zen Hei", "WenQuanYi Micro Hei", "Noto Sans CJK SC", "Noto Sans CJK TC",
        "Source Han Sans SC", "Droid Sans Fallback", "SimHei", "Microsoft YaHei"};

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

void logGuiPlatformDiagnostics()
{
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
    qInfo().noquote() << "[Client][PlatformDiag] 若遇 X11 断开：设 CLIENT_QPA_XCB_DEBUG=1 打开 qt.qpa.xcb.debug；"
                         "设 CLIENT_X11_DEEP_DIAG=1 额外启用单包上限/窗口像素粗算/GL 字符串/帧采样（见 client_x11_deep_diag）";
}

void registerContextProperties(
    QQmlContext *ctx, AuthManager *authManager, VehicleManager *vehicleManager,
    WebRtcClient *webrtcClient, WebRtcStreamManager *webrtcStreamManager,
    MqttController *mqttController, VehicleStatus *vehicleStatus,
    NodeHealthChecker *nodeHealthChecker, EventBus *eventBus,
    SystemStateMachine *systemStateMachine, SessionManager *teleopSession,
    VehicleControlService *vehicleControl, SafetyMonitorService *safetyMonitor,
    NetworkQualityAggregator *networkQuality, DegradationManager *degradationManager,
    Tracing *tracing, const QString &applicationChineseFont)
{
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
    const bool legacyOnly =
        QProcessEnvironment::systemEnvironment()
            .value(QStringLiteral("CLIENT_LEGACY_CONTROL_ONLY"))
            .toInt()
        == 1;
    ctx->setContextProperty("clientLegacyControlOnly", QVariant(legacyOnly));
    bool autoConnectVideo =
        (QProcessEnvironment::systemEnvironment().value("CLIENT_AUTO_CONNECT_VIDEO").toInt() == 1);
    ctx->setContextProperty("autoConnectVideo", QVariant(autoConnectVideo));
    ctx->setContextProperty("layoutDebugEnabled",
                            QProcessEnvironment::systemEnvironment().value("CLIENT_LAYOUT_DEBUG")
                                == "1");
    QString defaultServerUrlFromEnv =
        QProcessEnvironment::systemEnvironment().value("DEFAULT_SERVER_URL");
    if (defaultServerUrlFromEnv.isEmpty()) {
        defaultServerUrlFromEnv =
            QProcessEnvironment::systemEnvironment().value("REMOTE_DRIVING_SERVER");
    }
    if (defaultServerUrlFromEnv.isEmpty()) {
        defaultServerUrlFromEnv =
            QProcessEnvironment::systemEnvironment().value("BACKEND_URL");
    }
    ctx->setContextProperty("defaultServerUrlFromEnv", defaultServerUrlFromEnv);

    // QML 单例（AppContext）无法依赖 main.qml 的 id: window；用 rd_* 根上下文别名避免与单例属性同名冲突
    ctx->setContextProperty(QStringLiteral("rd_authManager"), authManager);
    ctx->setContextProperty(QStringLiteral("rd_vehicleManager"), vehicleManager);
    ctx->setContextProperty(QStringLiteral("rd_webrtcClient"), webrtcClient);
    ctx->setContextProperty(QStringLiteral("rd_webrtcStreamManager"), webrtcStreamManager);
    ctx->setContextProperty(QStringLiteral("rd_mqttController"), mqttController);
    ctx->setContextProperty(QStringLiteral("rd_vehicleStatus"), vehicleStatus);
    ctx->setContextProperty(QStringLiteral("rd_vehicleControl"), vehicleControl);
    ctx->setContextProperty(QStringLiteral("rd_safetyMonitor"), safetyMonitor);
    ctx->setContextProperty(QStringLiteral("rd_systemStateMachine"), systemStateMachine);
    ctx->setContextProperty(QStringLiteral("rd_nodeHealthChecker"), nodeHealthChecker);
    ctx->setContextProperty(QStringLiteral("rd_applicationChineseFont"), applicationChineseFont);
}

} // namespace ClientApp
