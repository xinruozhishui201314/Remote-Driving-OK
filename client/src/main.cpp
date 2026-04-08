#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QImage>
#include <QProcessEnvironment>
#include <cstdio>
#include <memory>

#include "app/client_app_bootstrap.h"
#include "app/client_display_runtime_policy.h"
#include "app/client_crash_diagnostics.h"
#include "app/client_x11_deep_diag.h"
#include "app/client_logging_setup.h"
#include "authmanager.h"
#include "core/eventbus.h"
#include "core/healthchecker.h"
#include "core/metricscollector.h"
#include "core/networkqualityaggregator.h"
#include "core/plugincontext.h"
#include "core/pluginmanager.h"
#include "core/systemstatemachine.h"
#include "core/tracing.h"
#include "infrastructure/controlloopticker.h"
#include "infrastructure/mqtttransportadapter.h"
#include "mqttcontroller.h"
#include "nodehealthchecker.h"
#include "services/degradationmanager.h"
#include "services/safetymonitorservice.h"
#include "services/sessionmanager.h"
#include "services/vehiclecontrolservice.h"
#include "vehiclemanager.h"
#include "vehiclestatus.h"
#include "webrtcclient.h"
#include "webrtcstreammanager.h"

#if defined(__linux__) && defined(__GLIBC__)
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

static void segfaultHandler(int sig)
{
    (void)sig;
    static const char header[] = "\n=== Segmentation fault - backtrace ===\n";
    static const char footer[] = "=====================================\n";
    (void)write(STDERR_FILENO, header, sizeof(header) - 1);
    void *array[64];
    int size = backtrace(array, 64);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    (void)write(STDERR_FILENO, footer, sizeof(footer) - 1);
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
}
#endif

int main(int argc, char *argv[])
{
#if defined(__linux__) && defined(__GLIBC__)
    signal(SIGSEGV, segfaultHandler);
#endif
    ClientCrashDiagnostics::installEarlyPlatformHooks();
    // 须在 QGuiApplication 之前：平台插件加载后部分规则无法再完整生效（见 Qt QLoggingCategory 文档）
    {
        const QProcessEnvironment envPre = QProcessEnvironment::systemEnvironment();
        QString filterRules;
        if (envPre.value(QStringLiteral("CLIENT_QPA_XCB_DEBUG")) == QStringLiteral("1")) {
            filterRules += QStringLiteral("qt.qpa.xcb.debug=true\n");
        }
        if (envPre.value(QStringLiteral("CLIENT_QPA_DEBUG")) == QStringLiteral("1")) {
            filterRules += QStringLiteral("qt.qpa.*.debug=true\n");
        }
        ClientX11DeepDiag::mergePreAppLoggingRules(filterRules);
        if (!filterRules.isEmpty()) {
            QLoggingCategory::setFilterRules(filterRules);
            fprintf(stderr, "[Client][PlatformDiag] QLoggingCategory::setFilterRules applied (pre-QGuiApplication)\n%s",
                    qPrintable(filterRules));
        }
    }

    ClientDisplayRuntimePolicy::applyPreQGuiApplicationDisplayPolicy();

    QGuiApplication app(argc, argv);

    ClientLogging::init();

    ClientCrashDiagnostics::installAfterQGuiApplication(app);

    qRegisterMetaType<QImage>("QImage");

    QString chineseFont;
    ClientApp::setupApplicationChrome(app, chineseFont);
    ClientApp::logGuiPlatformDiagnostics();

    ClientApp::registerQmlTypes();

    bool resetLogin = false;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString resetLoginEnv = env.value("CLIENT_RESET_LOGIN", "").toLower();
    if (resetLoginEnv == "1" || resetLoginEnv == "true" || resetLoginEnv == "yes") {
        resetLogin = true;
        qDebug() << "CLIENT_RESET_LOGIN environment variable set, will reset login state";
    }
    QStringList args = QCoreApplication::arguments();
    if (args.contains("--reset-login") || args.contains("--clear-login") || args.contains("-r")) {
        resetLogin = true;
        qDebug() << "Command line argument detected, will reset login state";
    }

    Tracing::instance().setCurrentTraceId(Tracing::generateTraceId());
    qInfo().noquote() << "[Client][Main] process traceId="
                      << Tracing::instance().currentTraceId().left(16);

    qDebug() << "Creating C++ objects...";
    auto metricsCollector = std::make_unique<MetricsCollector>();
    auto healthChecker = std::make_unique<HealthChecker>(metricsCollector.get());
    auto authManager = std::make_unique<AuthManager>(&app, !resetLogin);
    if (resetLogin) {
        authManager->clearCredentials();
    }
    auto vehicleManager = std::make_unique<VehicleManager>(&app);
    auto vehicleStatus = std::make_unique<VehicleStatus>(&app);
    auto nodeHealthChecker = std::make_unique<NodeHealthChecker>(&app);
    auto webrtcClient = std::make_unique<WebRtcClient>(&app);
    auto webrtcStreamManager = std::make_unique<WebRtcStreamManager>(&app);
    auto mqttController = std::make_unique<MqttController>(&app);
    QString mqttBrokerEnv = QProcessEnvironment::systemEnvironment().value("MQTT_BROKER_URL");
    if (!mqttBrokerEnv.isEmpty()) {
        mqttController->setBrokerUrl(mqttBrokerEnv);
    }
    mqttController->setWebRtcClient(webrtcStreamManager->frontClient());
    QString controlChannelPreferred =
        QProcessEnvironment::systemEnvironment().value("CONTROL_CHANNEL_PREFERRED");
    if (!controlChannelPreferred.isEmpty()) {
        mqttController->setPreferredChannel(controlChannelPreferred);
    }

    EventBus &eventBus = EventBus::instance();
    auto systemStateMachine = std::make_unique<SystemStateMachine>(&eventBus, &app);
    auto networkQuality = std::make_unique<NetworkQualityAggregator>(
        vehicleStatus.get(), nodeHealthChecker.get(), &app);
    auto teleopSession = std::make_unique<SessionManager>(
        authManager.get(), vehicleManager.get(), mqttController.get(),
        webrtcStreamManager.get(), systemStateMachine.get(), &app);
    auto vehicleControl = std::make_unique<VehicleControlService>(
        mqttController.get(), vehicleManager.get(), webrtcStreamManager.get(), &app);
    teleopSession->setVehicleControl(vehicleControl.get());

    auto safetyMonitor =
        std::make_unique<SafetyMonitorService>(vehicleStatus.get(), systemStateMachine.get(), &app);
    vehicleControl->setSafetyMonitor(safetyMonitor.get());
    auto degradationManager = std::make_unique<DegradationManager>(systemStateMachine.get(), &app);
    MqttTransportAdapter mqttTransportAdapter(mqttController.get());
    (void)mqttTransportAdapter;
    ControlLoopTicker controlLoopTicker;
    if (QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLIENT_ENABLE_CONTROL_TICKER"))
        == QStringLiteral("1")) {
        controlLoopTicker.setIntervalMs(20);
        controlLoopTicker.start();
    }

    QObject::connect(webrtcStreamManager.get(), &WebRtcStreamManager::anyConnectedChanged,
                     vehicleStatus.get(), [vs = vehicleStatus.get(), wsm = webrtcStreamManager.get()]() {
                         vs->setVideoConnected(wsm->anyConnected());
                     });
    vehicleStatus->setVideoConnected(webrtcStreamManager->anyConnected());
    QObject::connect(mqttController.get(), &MqttController::connectionStatusChanged,
                     vehicleStatus.get(), &VehicleStatus::setMqttConnected);

    QObject::connect(authManager.get(), &AuthManager::loginSucceeded, teleopSession.get(),
                     &SessionManager::onLoginSucceeded);
    QObject::connect(authManager.get(), &AuthManager::loginStatusChanged, teleopSession.get(),
                     &SessionManager::onLoginStatusChanged);

    QObject::connect(vehicleManager.get(), &VehicleManager::currentVinChanged, mqttController.get(),
                     &MqttController::setCurrentVin);
    QObject::connect(vehicleManager.get(), &VehicleManager::currentVinChanged, teleopSession.get(),
                     &SessionManager::onVinSelected);
    QObject::connect(vehicleManager.get(), &VehicleManager::sessionCreated, teleopSession.get(),
                     &SessionManager::onSessionCreated);

    QObject::connect(mqttController.get(), &MqttController::statusReceived, vehicleStatus.get(),
                     &VehicleStatus::updateStatus);

    QObject::connect(authManager.get(), &AuthManager::loginStatusChanged,
                     [metrics = metricsCollector.get(), am = authManager.get()]() {
                         if (am->isLoggedIn()) {
                             metrics->increment("client.auth.login_success_total");
                         } else {
                             metrics->increment("client.auth.login_failure_total");
                         }
                     });
    QObject::connect(mqttController.get(), &MqttController::connectionStatusChanged,
                     [metrics = metricsCollector.get(), mc = mqttController.get()]() {
                         if (mc->isConnected()) {
                             metrics->increment("client.mqtt.connection_success_total");
                         } else {
                             metrics->increment("client.mqtt.connection_lost_total");
                         }
                     });
    QObject::connect(webrtcStreamManager.get(), &WebRtcStreamManager::anyConnectedChanged,
                     [metrics = metricsCollector.get(), wsm = webrtcStreamManager.get()]() {
                         if (wsm->anyConnected()) {
                             metrics->increment("client.webrtc.stream_connected_total");
                         } else {
                             metrics->increment("client.webrtc.stream_disconnected_total");
                         }
                     });
    QObject::connect(safetyMonitor.get(), &SafetyMonitorService::safetyWarning,
                     [metrics = metricsCollector.get()](const QString &msg) {
                         Q_UNUSED(msg);
                         metrics->increment("client.safety.warning_total");
                     });

    healthChecker->recordStartTime();
    degradationManager->initialize();
    degradationManager->start();
    QObject::connect(networkQuality.get(), &NetworkQualityAggregator::networkQualityChanged,
                     [dm = degradationManager.get()](double score, double rttMs, double packetLossRate,
                                                      double bandwidthKbps, double jitterMs) {
                         NetworkQuality quality;
                         quality.score = score;
                         quality.rttMs = rttMs;
                         quality.packetLossRate = packetLossRate;
                         quality.bandwidthKbps = bandwidthKbps;
                         quality.jitterMs = jitterMs;
                         dm->updateNetworkQuality(quality);
                     });
    QObject::connect(degradationManager.get(), &DegradationManager::levelChanged,
                     [metrics = metricsCollector.get()](DegradationManager::DegradationLevel newLevel,
                                                        DegradationManager::DegradationLevel oldLevel) {
                         Q_UNUSED(newLevel);
                         Q_UNUSED(oldLevel);
                         metrics->increment("client.degradation.level_change_total");
                     });

    vehicleControl->initialize();

    QQmlApplicationEngine engine;
    PluginContext pluginCtx(&engine, &eventBus, &app);
    {
        using AuthPtr = std::shared_ptr<AuthManager>;
        using VmPtr = std::shared_ptr<VehicleManager>;
        using VcPtr = std::shared_ptr<VehicleControlService>;
        pluginCtx.registerService(QStringLiteral("AuthManager"),
                                  AuthPtr(authManager.get(), [](AuthManager *) {}));
        pluginCtx.registerService(QStringLiteral("VehicleManager"),
                                  VmPtr(vehicleManager.get(), [](VehicleManager *) {}));
        pluginCtx.registerService(QStringLiteral("VehicleControlService"),
                                  VcPtr(vehicleControl.get(), [](VehicleControlService *) {}));
    }
    PluginManager pluginManager(&pluginCtx, &app);
    const QString pluginDir = env.value(QStringLiteral("CLIENT_PLUGIN_DIR"));
    if (!pluginDir.isEmpty()) {
        const QDir dir(pluginDir);
        if (dir.exists()) {
            qInfo().noquote() << "[Client][PluginManager] loading plugins from" << pluginDir;
            pluginManager.loadPluginsFromDirectory(pluginDir);
        } else {
            qWarning().noquote() << "[Client][PluginManager] CLIENT_PLUGIN_DIR does not exist:"
                                 << pluginDir;
        }
    }

    ClientApp::registerContextProperties(
        engine.rootContext(), authManager.get(), vehicleManager.get(), webrtcClient.get(),
        webrtcStreamManager.get(), mqttController.get(), vehicleStatus.get(),
        nodeHealthChecker.get(), &eventBus, systemStateMachine.get(), teleopSession.get(),
        vehicleControl.get(), safetyMonitor.get(), networkQuality.get(), degradationManager.get(),
        &Tracing::instance(), chineseFont);

    QUrl url = ClientApp::resolveQmlMainUrl(&engine);
    if (!url.isValid()) {
        qCritical() << "[QML_LOAD] ❌ 无法找到 main.qml";
        ClientLogging::shutdown();
        return -1;
    }

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                qCritical() << "[QML_LOAD] ❌ QML 对象创建失败，URL:" << objUrl;
                QCoreApplication::exit(-1);
            } else if (obj) {
                qDebug() << "[QML_LOAD] ✓ QML 对象创建成功，URL:" << objUrl;
            }
        },
        Qt::QueuedConnection);

    QObject::connect(&engine, &QQmlEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
                         for (const QQmlError &e : warnings) {
                             qCritical().noquote() << "[QML_LOAD][QQmlEngine::warnings]" << e.toString();
                         }
                     },
                     Qt::DirectConnection);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
        qInfo().noquote() << "[Client][Main] QCoreApplication::aboutToQuit"
                             "（若为突然退出且无本行，多为平台致命错误如 X11/xcb IO）";
    });
    engine.load(url);
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "[QML_LOAD] ❌ engine.load() 失败（QML 文件无法加载或含语法错误），退出码 -1";
        ClientLogging::shutdown();
        return -1;
    }

    // 先于 sceneGraphError 挂接：尽早连接 sceneGraphInitialized，降低错过首帧 GL 上下文信号的概率
    ClientX11DeepDiag::installAfterQmlLoaded(&app);
    ClientCrashDiagnostics::installAfterTopLevelWindowsReady();

    int ret = app.exec();
    ClientLogging::shutdown();
    return ret;
}
