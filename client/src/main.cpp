#include "app/client_app_bootstrap.h"
#include "app/client_crash_diagnostics.h"
#include "app/client_display_runtime_policy.h"
#include "app/client_logging_setup.h"
#include "app/client_perf_diag_env.h"
#include "app/client_platform_gate.h"
#include "app/client_startup_readiness_gate.h"
#include "app/client_startup_tcp_gate.h"
#include "app/client_system_stall_diag.h"
#include "app/client_x11_deep_diag.h"
#include "app/client_x11_visual_probe.h"
#include "authmanager.h"
#include "core/eventbus.h"
#include "core/healthchecker.h"
#include "core/metricscollector.h"
#include "core/networkqualityaggregator.h"
#include "core/plugincontext.h"
#include "core/pluginmanager.h"
#include "core/systemstatemachine.h"
#include "core/tracing.h"
#include "core/BlackBoxService.h"
#include "core/TimeSyncService.h"
#include "core/memorymanager.h"
#include "infrastructure/controlloopticker.h"
#include "infrastructure/mqtttransportadapter.h"
#include <infrastructure/network/TransportAggregator.h>
#include <adapters/network/WebRTCChannel.h>
#include "mqttcontroller.h"
#include "nodehealthchecker.h"
#include <infrastructure/hardware/InputSampler.h>
#include <adapters/hardware/KeyboardMouseInput.h>
#include "services/degradationmanager.h"
#include "services/safetymonitorservice.h"
#include "services/sessionmanager.h"
#include "services/vehiclecontrolservice.h"
#include "vehiclemanager.h"
#include "vehiclestatus.h"
#include "media/ClientVideoStreamHealth.h"
#include "ui/video_integrity_banner_bridge.h"
#include "webrtcclient.h"
#include "webrtcstreammanager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QLoggingCategory>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QThread>
#include <QTimer>

#include <cstdio>
#include <exception>
#include <memory>

#if defined(__linux__) && defined(__GLIBC__)
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

static void segfaultHandler(int sig) {
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

int main(int argc, char *argv[]) {
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
      fprintf(stderr,
              "[Client][PlatformDiag] QLoggingCategory::setFilterRules applied "
              "(pre-QGuiApplication)\n%s",
              qPrintable(filterRules));
    }
  }

  ClientPerfDiagEnv::applyBeforeQGuiApplication();
  ClientDisplayRuntimePolicy::applyPreQGuiApplicationDisplayPolicy();
  ClientApp::applyPresentationSurfaceFormatDefaults();

  {
    const int platPre = ClientApp::runPreQGuiApplicationPlatformGate();
    if (platPre != 0) {
      return platPre;
    }
  }

  QGuiApplication app(argc, argv);

  ClientSystemStallDiag::installMainThreadWatchdogIfEnabled(&app);

  ClientLogging::init();
  ClientLogging::installUnixSignalLogFlushAndQuit(app);

  // 进程级视频策略一行（含 G[…] bracket），早于首路 WebRTC 连接，便于 grep 对齐四路契约
  ClientVideoStreamHealth::logGlobalEnvOnce();

#if !defined(ENABLE_FFMPEG)
  qCritical().noquote()
      << "[Client][VideoHealth][ERROR] 构建未启用 ENABLE_FFMPEG：H.264/WebRTC 视频解码不可用。"
         "请安装 libavcodec-dev libavutil-dev libswscale-dev libavformat-dev 后重新 cmake。"
         "（若 CMake 已允许无 FFmpeg 链接，本行用于启动期强提示。）";
#endif

  ClientApp::logStartupMandatoryGatesBrief();

  {
    const int platPost = ClientApp::runPostQGuiApplicationPlatformGate(app);
    if (platPost != 0) {
      ClientLogging::shutdown();
      return platPost;
    }
  }

  ClientCrashDiagnostics::installAfterQGuiApplication(app);

  qRegisterMetaType<QImage>("QImage");

  QString chineseFont;
  ClientApp::setupApplicationChrome(app, chineseFont);
  ClientApp::logGuiPlatformDiagnostics();
  ClientX11VisualProbe::logX11WindowManagerInfo();

  MetricsCollector::instance();
  {
    const int displayRc = ClientApp::runDisplayEnvironmentCheck();
    if (displayRc != 0) {
      qCritical().noquote() << "[Client][Main] 显示/GL 启动门禁未通过 exit=" << displayRc
                            << "（88=交互式 GL 探测失败；75–78=硬件呈现门禁；见 "
                               "[Client][StartupGate]/[DisplayGate]）";
      ClientLogging::shutdown();
      return displayRc;
    }
  }

  {
    const int cfgRc = ClientApp::runMandatoryConfigurationReadinessGate();
    if (cfgRc != 0) {
      qCritical().noquote() << "[Client][Main] 配置就绪门禁未通过 exit=" << cfgRc
                            << "（95=服务 URL 无效；见 [Client][StartupGate][Readiness]）";
      ClientLogging::shutdown();
      return cfgRc;
    }
  }

  {
    const int tcpRc = ClientApp::runMandatoryTcpConnectivityGate();
    if (tcpRc != 0) {
      qCritical().noquote() << "[Client][Main] TCP 启动门禁未通过 exit=" << tcpRc
                            << "（96=配置端点不可达；见 [Client][StartupGate][Tcp]）";
      ClientLogging::shutdown();
      return tcpRc;
    }
  }

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
  // MetricsCollector::instance() 已在显示环境检查前构造，供 client_display_* 门禁指标写入。
  auto healthChecker = std::make_unique<HealthChecker>(&app);
  auto authManager = std::make_unique<AuthManager>(&app, !resetLogin);
  if (resetLogin) {
    authManager->clearCredentials();
  }
  auto vehicleManager = std::make_unique<VehicleManager>(&app);
  auto vehicleStatus = std::make_unique<VehicleStatus>(&app);
  auto nodeHealthChecker = std::make_unique<NodeHealthChecker>(&app);
  auto webrtcClient = std::make_unique<WebRtcClient>(&app);
  auto webrtcStreamManager = std::make_unique<WebRtcStreamManager>(&app);
  webrtcStreamManager->setVehicleManager(vehicleManager.get());
  auto mqttController = std::make_unique<MqttController>(&app);
  QObject::connect(mqttController.get(), &MqttController::remoteControlRequested,
                   vehicleStatus.get(), &VehicleStatus::setLocalIntentRemoteControl);
  QString mqttBrokerEnv = QProcessEnvironment::systemEnvironment().value("MQTT_BROKER_URL");
  if (!mqttBrokerEnv.isEmpty()) {
    mqttController->setBrokerUrl(mqttBrokerEnv);
  }
  mqttController->setWebRtcClient(webrtcStreamManager->frontClient());
  webrtcStreamManager->connectEncoderHintMqttRelay(mqttController.get());
  QString controlChannelPreferred =
      QProcessEnvironment::systemEnvironment().value("CONTROL_CHANNEL_PREFERRED");
  if (!controlChannelPreferred.isEmpty()) {
    mqttController->setPreferredChannel(controlChannelPreferred);
  }

  EventBus &eventBus = EventBus::instance();
  auto systemStateMachine = std::make_unique<SystemStateMachine>(&eventBus, &app);
  auto networkQuality = std::make_unique<NetworkQualityAggregator>(vehicleStatus.get(),
                                                                   nodeHealthChecker.get(), &app);
  for (WebRtcClient *wc : {webrtcStreamManager->frontClient(), webrtcStreamManager->rearClient(),
                           webrtcStreamManager->leftClient(), webrtcStreamManager->rightClient()}) {
    if (!wc)
      continue;
    QObject::connect(wc, &WebRtcClient::videoPresentationDegraded, networkQuality.get(),
                     &NetworkQualityAggregator::noteMediaPresentationDegraded);
  }
  auto teleopSession = std::make_unique<SessionManager>(
      authManager.get(), vehicleManager.get(), mqttController.get(), webrtcStreamManager.get(),
      systemStateMachine.get(), &app);

  // ─── Safety Thread Decoupling ──────────────────────────────────────────
  // 将安全监控与控车逻辑移出主线程，确保 UI 卡死时仍能执行安全防护
  auto safetyThread = std::make_unique<QThread>(&app);
  safetyThread->setObjectName(QStringLiteral("SafetyServiceThread"));
  safetyThread->start(QThread::HighPriority);

  // 析构顺序：后声明先析构。SafetyMonitor 必须在 VehicleControlService 之后析构
  auto safetyMonitor =
      std::make_unique<SafetyMonitorService>(vehicleStatus.get(), systemStateMachine.get(), nullptr);
  safetyMonitor->attachToSafetyThread(safetyThread.get());

  auto inputSampler = std::make_unique<InputSampler>(&app);
  auto hidDevice = std::make_unique<KeyboardMouseInput>(&app);
  inputSampler->setDevice(hidDevice.get());
  inputSampler->start(200); // 200Hz 采样速率

  auto vehicleControl = std::make_unique<VehicleControlService>(
      mqttController.get(), vehicleManager.get(), webrtcStreamManager.get(), inputSampler.get(), nullptr);
  vehicleControl->attachToSafetyThread(safetyThread.get());

  // ★ 核心修复：连接异步 DataChannel 发送信号
  // 必须使用 Qt::QueuedConnection，确保在 WebRTC 线程（或主线程）执行，不阻塞安全控制线程
  QObject::connect(vehicleControl.get(), &VehicleControlService::requestDataChannelSend,
                   webrtcStreamManager.get(), [wsm = webrtcStreamManager.get()](const QByteArray& data) {
                     if (auto* fc = wsm->frontClient()) {
                       fc->sendDataChannelMessage(data);
                     }
                   }, Qt::QueuedConnection);

  // UI 线程心跳定时器：由主线程向安全线程「报平安」
  auto uiHeartbeatTimer = std::make_unique<QTimer>(&app);
  uiHeartbeatTimer->setInterval(50); // 50ms 频率报平安，宽放至 500ms 触发急停
  QObject::connect(uiHeartbeatTimer.get(), &QTimer::timeout, &app, [sm = safetyMonitor.get()]() {
    sm->noteUiHeartbeat();
  });
  uiHeartbeatTimer->start();

  teleopSession->setSafetyMonitor(safetyMonitor.get());
  teleopSession->setVehicleControl(vehicleControl.get());
  vehicleControl->setSafetyMonitor(safetyMonitor.get());
  // ────────────────────────────────────────────────────────────────────────
  auto degradationManager = std::make_unique<DegradationManager>(systemStateMachine.get(), &app);
  
  // ─── 黑匣子与高精度同步 ───────────────────────────────────────────
  BlackBoxService::instance().start();

  // ─── 传输层聚合与双链路冗余 ───────────────────────────────────────────
  auto transportAggregator = std::make_unique<TransportAggregator>(&app);
  
  auto webrtcChannel = new WebRTCChannel(transportAggregator.get());
  webrtcChannel->injectInstances(webrtcStreamManager.get(), webrtcStreamManager->frontClient());
  auto mqttAdapter = new MqttTransportAdapter(mqttController.get(), transportAggregator.get());
  
  transportAggregator->addTransport(QStringLiteral("WebRTC"), webrtcChannel, true);
  transportAggregator->addTransport(QStringLiteral("MQTT"), mqttAdapter, false);
  
  TransportConfig tcfg;
  transportAggregator->initialize(tcfg);
  
  vehicleControl->setTransport(transportAggregator.get());
  // ────────────────────────────────────────────────────────────────────────
  
  ControlLoopTicker controlLoopTicker;
  if (QProcessEnvironment::systemEnvironment().value(
          QStringLiteral("CLIENT_ENABLE_CONTROL_TICKER")) == QStringLiteral("1")) {
    controlLoopTicker.setIntervalMs(20);
    controlLoopTicker.start();
  }

  QObject::connect(webrtcStreamManager.get(), &WebRtcStreamManager::anyConnectedChanged,
                   vehicleStatus.get(),
                   [vs = vehicleStatus.get(), wsm = webrtcStreamManager.get()]() {
                     vs->setVideoConnected(wsm->anyConnected());
                   });
  // ★ 传输反馈：将车端 streaming_ready 同步至视频管理器，实现「车端就绪后立即尝试拉流」
  QObject::connect(vehicleStatus.get(), &VehicleStatus::streamingReadyChanged,
                   webrtcStreamManager.get(), &WebRtcStreamManager::setStreamingReady);
  
  vehicleStatus->setVideoConnected(webrtcStreamManager->anyConnected());
  QObject::connect(mqttController.get(), &MqttController::mqttBrokerConnectionChanged,
                   vehicleStatus.get(), &VehicleStatus::setMqttConnected);
  vehicleStatus->setMqttConnected(mqttController->mqttBrokerConnected());

  // 车端 MQTT 确认远驾后：PRE_FLIGHT → DRIVING，安全模块仅在 DRIVING/DEGRADED 要求 status 心跳
  QObject::connect(
      vehicleStatus.get(), &VehicleStatus::remoteControlEnabledChanged, &app,
      [fsm = systemStateMachine.get(), vs = vehicleStatus.get()](bool enabled) {
        {
          if (!enabled || !fsm || !vs)
            return;
          if (fsm->stateEnum() != SystemStateMachine::SystemState::PRE_FLIGHT)
            return;
          if (!fsm->fire(SystemStateMachine::Trigger::PREFLIGHT_OK)) {
            qWarning().noquote() << "[Client][Session] PREFLIGHT_OK 未应用（FSM 状态已变）";
          } else {
            qInfo().noquote() << "[Client][Session] PREFLIGHT_OK：车端已确认远驾，进入 DRIVING";
          }
        }  
      });
  QObject::connect(systemStateMachine.get(), &SystemStateMachine::stateChanged, &app,
                   [fsm = systemStateMachine.get(),
                    vs = vehicleStatus.get(),
                    am = authManager.get()](const QString &newName, const QString & /*old*/) {
                     {
                       // ★ 架构增强：将驾驶状态同步至 AuthManager，开启令牌保护
                       if (am) {
                         bool driving = (newName == QLatin1String("DRIVING") || newName == QLatin1String("DEGRADED"));
                         am->setDrivingActive(driving);
                       }

                       if (newName != QLatin1String("PRE_FLIGHT") || !fsm || !vs)
                         return;
                       if (!vs->remoteControlEnabled())
                         return;
                       if (!fsm->fire(SystemStateMachine::Trigger::PREFLIGHT_OK)) {
                         qWarning().noquote()
                             << "[Client][Session] PRE_FLIGHT 入场时 PREFLIGHT_OK 失败（状态竞态）";
                       } else {
                         qInfo().noquote() << "[Client][Session] PRE_FLIGHT：远驾已先确认，进入 DRIVING";
                       }
                     }  
                   });

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
  // 车端 status 下行即视为心跳：否则 onHeartbeatReceived 从未接线，READY 态看视频也会误触发急停。
  QObject::connect(mqttController.get(), &MqttController::statusReceived, safetyMonitor.get(),
                   &SafetyMonitorService::onHeartbeatReceived, Qt::QueuedConnection);

  QObject::connect(authManager.get(), &AuthManager::loginStatusChanged, [am = authManager.get()]() {
    if (am->isLoggedIn()) {
      MetricsCollector::instance().increment("client.auth.login_success_total");
    } else {
      MetricsCollector::instance().increment("client.auth.login_failure_total");
    }
  });
  QObject::connect(
      mqttController.get(), &MqttController::mqttBrokerConnectionChanged,
      [](bool brokerUp) {
        if (brokerUp) {
          MetricsCollector::instance().increment("client.mqtt.connection_success_total");
        } else {
          MetricsCollector::instance().increment("client.mqtt.connection_lost_total");
        }
      });
  QObject::connect(
      webrtcStreamManager.get(), &WebRtcStreamManager::anyConnectedChanged,
      [wsm = webrtcStreamManager.get()]() {
        if (wsm->anyConnected()) {
          MetricsCollector::instance().increment("client.webrtc.stream_connected_total");
        } else {
          MetricsCollector::instance().increment("client.webrtc.stream_disconnected_total");
        }
      });
  QObject::connect(safetyMonitor.get(), &SafetyMonitorService::safetyWarning,
                   [](const QString &msg) {
                     Q_UNUSED(msg);
                     MetricsCollector::instance().increment("client.safety.warning_total");
                   });

  healthChecker->recordStartTime();
  degradationManager->initialize();
  degradationManager->start();
  QObject::connect(
      networkQuality.get(), &NetworkQualityAggregator::networkQualityChanged,
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
  QObject::connect(
      degradationManager.get(), &DegradationManager::levelChanged,
      [](DegradationManager::DegradationLevel newLevel,
         DegradationManager::DegradationLevel oldLevel) {
        Q_UNUSED(newLevel);
        Q_UNUSED(oldLevel);
        MetricsCollector::instance().increment("client.degradation.level_change_total");
      });

  vehicleControl->initialize();
  safetyMonitor->initialize();

  QQmlApplicationEngine engine;
  VideoIntegrityBannerBridge videoIntegrityBannerBridge(&app);
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
      webrtcStreamManager.get(), mqttController.get(), vehicleStatus.get(), nodeHealthChecker.get(),
      &eventBus, systemStateMachine.get(), teleopSession.get(), vehicleControl.get(),
      safetyMonitor.get(), networkQuality.get(), degradationManager.get(), &Tracing::instance(),
      chineseFont, &videoIntegrityBannerBridge);
  engine.rootContext()->setContextProperty(QStringLiteral("rd_keyboardInput"), hidDevice.get());
  ClientApp::logQmlRootContextRdSnapshot(engine.rootContext());

  QUrl url = ClientApp::resolveQmlMainUrl(&engine);
  if (!url.isValid()) {
    qCritical().noquote()
        << "\n══════════════════════════════════════════════════════════════════════\n"
        << "[Client][StartupGate] FATAL exit=91 (QML_MAIN_NOT_FOUND)\n"
        << "原因: 在约定搜索路径中未找到 main.qml，无法加载 UI。\n"
        << "建议: 确认与可执行文件相对路径的 qml/ 目录存在且已部署；见 resolveQmlMainUrl "
           "搜索列表。\n"
        << "══════════════════════════════════════════════════════════════════════\n";
    ClientLogging::shutdown();
    return 91;
  }
  ClientApp::logQmlEngineImportPaths(&engine);

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreated, &app,
      [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
          qCritical().noquote()
              << "\n══════════════════════════════════════════════════════════════════════\n"
              << "[Client][StartupGate] FATAL exit=94 (QML_ROOT_OBJECT_CREATE_FAILED)\n"
              << "原因: QQmlApplicationEngine 创建根对象失败，URL=" << objUrl << "\n"
              << "建议: 检查 QML 语法、import、缺失组件及 engine 报错批次。\n"
              << "══════════════════════════════════════════════════════════════════════\n";
          QCoreApplication::exit(94);
        } else if (obj) {
          qDebug() << "[QML_LOAD] ✓ QML 对象创建成功，URL:" << objUrl;
        }
      },
      Qt::QueuedConnection);

  ClientApp::installQmlEngineDiagnosticHooks(&engine, &app);
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
    qInfo().noquote() << "[Client][Main] QCoreApplication::aboutToQuit"
                         "（若为突然退出且无本行，多为平台致命错误如 X11/xcb IO）";
  });
  engine.load(url);
  if (engine.rootObjects().isEmpty()) {
    qCritical().noquote()
        << "\n══════════════════════════════════════════════════════════════════════\n"
        << "[Client][StartupGate] FATAL exit=93 (QML_LOAD_EMPTY_ROOT)\n"
        << "原因: engine.load() 后 rootObjects 为空（常见：QML "
           "语法错误、运行时错误、根组件未实例化）。\n"
        << "建议: 查看上方 [Client][QML][Warnings] / QQmlEngine::warnings 输出。\n"
        << "══════════════════════════════════════════════════════════════════════\n";
    ClientLogging::shutdown();
    return 93;
  }
  {
    const int platQml = ClientApp::enforceQmlLoadedPlatformGate(engine.rootObjects());
    if (platQml != 0) {
      ClientLogging::shutdown();
      return platQml;
    }
  }
  ClientApp::logQmlPostLoadSummary(&engine, url);

  ClientX11VisualProbe::scheduleLogTopLevelQuickWindowVisuals();

  ClientSystemStallDiag::hookQuickWindowsSceneGlIfEnabled();

  // 先于 sceneGraphError 挂接：尽早连接 sceneGraphInitialized，降低错过首帧 GL 上下文信号的概率
  ClientX11DeepDiag::installAfterQmlLoaded(&app);
  ClientCrashDiagnostics::installAfterTopLevelWindowsReady();

  ClientApp::logStartupAllMandatoryGatesOk();

  // 无头/CI 生命周期冒烟：覆盖 app.exec() → aboutToQuit → ClientLogging::shutdown()（与 Qt
  // Test「自包含快速用例」互补） 参考常见 headless smoke：定时 QCoreApplication::quit()；见
  // scripts/verify-client-headless-lifecycle.sh
  {
    const int headlessSmokeMs = qEnvironmentVariableIntValue("CLIENT_HEADLESS_SMOKE_MS");
    if (headlessSmokeMs > 0) {
      QTimer::singleShot(headlessSmokeMs, &app, []() {
        qInfo().noquote()
            << "[Client][Main] CLIENT_HEADLESS_SMOKE_MS 到期，主动 quit（无头生命周期/门禁验证）";
        QCoreApplication::quit();
      });
    }
  }

  int ret = app.exec();

  // 退出前清理：停止安全线程
  if (safetyThread->isRunning()) {
    safetyThread->quit();
    if (!safetyThread->wait(3000)) {
      safetyThread->terminate();
      safetyThread->wait(1000);
    }
  }

  ClientLogging::shutdown();
  return ret;
}
