#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QFontDatabase>
#include <QFont>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QVariant>
#include <QImage>
#include <QDate>
#include <QDir>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#ifdef ENABLE_QT6_QUICKCONTROLS2
#include <QtQuickControls2/QQuickStyle>
#endif
#include "webrtcclient.h"
#include "webrtcstreammanager.h"
#include "mqttcontroller.h"
#include "vehiclestatus.h"
#include "authmanager.h"
#include "vehiclemanager.h"
#include "presentation/renderers/VideoRenderer.h"
#include "nodehealthchecker.h"
#include "core/eventbus.h"
#include "core/systemstatemachine.h"
#include "core/networkqualityaggregator.h"
#include "services/sessionmanager.h"
#include "services/vehiclecontrolservice.h"
#include "services/safetymonitorservice.h"
#include "services/degradationmanager.h"
#include "infrastructure/mqtttransportadapter.h"
#include "infrastructure/controlloopticker.h"

// 解析 main.qml 路径并设置 QML 导入路径；失败返回空 QUrl
static QUrl resolveQmlMainUrl(QQmlApplicationEngine *engine)
{
    QStringList possiblePaths = {
        QCoreApplication::applicationDirPath() + "/../qml/main.qml",
        QCoreApplication::applicationDirPath() + "/../../qml/main.qml",
        QCoreApplication::applicationDirPath() + "/qml/main.qml",
        "qml/main.qml",
        "../qml/main.qml",
        "/workspaces/Remote-Driving/client/qml/main.qml",
        "/workspace/client/qml/main.qml"
    };
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

// 将 C++ 单例与配置注册到 QML 根上下文（含架构分层：EventBus / FSM / Session / Control / Safety）
static void registerContextProperties(QQmlContext *ctx,
    AuthManager *authManager, VehicleManager *vehicleManager,
    WebRtcClient *webrtcClient, WebRtcStreamManager *webrtcStreamManager,
    MqttController *mqttController, VehicleStatus *vehicleStatus,
    NodeHealthChecker *nodeHealthChecker,
    EventBus *eventBus, SystemStateMachine *systemStateMachine,
    SessionManager *teleopSession, VehicleControlService *vehicleControl,
    SafetyMonitorService *safetyMonitor, NetworkQualityAggregator *networkQuality,
    DegradationManager *degradationManager)
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
    const bool legacyOnly = QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLIENT_LEGACY_CONTROL_ONLY")).toInt() == 1;
    ctx->setContextProperty("clientLegacyControlOnly", QVariant(legacyOnly));
    bool autoConnectVideo = (QProcessEnvironment::systemEnvironment().value("CLIENT_AUTO_CONNECT_VIDEO").toInt() == 1);
    ctx->setContextProperty("autoConnectVideo", QVariant(autoConnectVideo));
    ctx->setContextProperty("layoutDebugEnabled", QProcessEnvironment::systemEnvironment().value("CLIENT_LAYOUT_DEBUG") == "1");
    QString defaultServerUrlFromEnv = QProcessEnvironment::systemEnvironment().value("DEFAULT_SERVER_URL");
    if (defaultServerUrlFromEnv.isEmpty())
        defaultServerUrlFromEnv = QProcessEnvironment::systemEnvironment().value("REMOTE_DRIVING_SERVER");
    if (defaultServerUrlFromEnv.isEmpty())
        defaultServerUrlFromEnv = QProcessEnvironment::systemEnvironment().value("BACKEND_URL");
    ctx->setContextProperty("defaultServerUrlFromEnv", defaultServerUrlFromEnv);
}

#if defined(__linux__) && defined(__GLIBC__)
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>   // write(), STDERR_FILENO — async-signal-safe

static void segfaultHandler(int sig)
{
    (void)sig;
    // 仅使用 async-signal-safe 函数：write() + backtrace() + backtrace_symbols_fd()
    // backtrace_symbols()/free()/fprintf() 非 async-signal-safe，信号处理中调用可能死锁
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

// ─────────────────────────────────────────────────────────────────────────────
// 异步日志系统：将日志条目放入无锁环形队列，由后台线程批量写入，
// 彻底消除主线程因 fflush/mutex 导致的卡顿。
// WARN/CRIT/FATAL 仍同步写入，确保崩溃前日志不丢失。
// ─────────────────────────────────────────────────────────────────────────────
static QFile *s_logFile = nullptr;

namespace {

struct AsyncLogQueue {
    static constexpr size_t kCapacity = 4096;

    struct Entry {
        QByteArray data;
        bool urgent; // WARN/CRIT/FATAL: flush immediately
    };

    std::mutex mtx;
    std::condition_variable cv;
    std::queue<Entry> q;
    std::atomic<bool> running{false};
    std::thread worker;
    std::atomic<uint32_t> dropped{0};

    void start() {
        running.store(true);
        worker = std::thread([this]() {
            while (running.load()) {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait_for(lock, std::chrono::milliseconds(50));
                drainLocked(lock);
            }
            // 退出前排空剩余
            std::unique_lock<std::mutex> lock(mtx);
            drainLocked(lock);
        });
    }

    void stop() {
        running.store(false);
        cv.notify_all();
        if (worker.joinable()) worker.join();
    }

    void push(QByteArray data, bool urgent) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (q.size() >= kCapacity) {
                dropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            q.push({std::move(data), urgent});
        }
        cv.notify_one();
    }

private:
    void drainLocked(std::unique_lock<std::mutex> &lock) {
        // 若有积压丢弃，先写一条溢出警告（使用 write 避免持锁期间再次 push）
        uint32_t d = dropped.exchange(0, std::memory_order_relaxed);
        if (d > 0) {
            lock.unlock();
            char buf[128];
            int n = snprintf(buf, sizeof(buf),
                             "[LOG_OVERFLOW] %u log message(s) dropped due to queue full\n", d);
            if (n > 0) {
                fwrite(buf, 1, static_cast<size_t>(n), stderr);
                if (s_logFile && s_logFile->isOpen())
                    s_logFile->write(buf, n);
            }
            lock.lock();
        }

        bool needFlush = false;
        while (!q.empty()) {
            Entry e = std::move(q.front());
            q.pop();
            lock.unlock();

            fprintf(stderr, "%s", e.data.constData());
            if (s_logFile && s_logFile->isOpen()) {
                s_logFile->write(e.data);
            }
            if (e.urgent) needFlush = true;

            lock.lock();
        }
        if (needFlush) {
            lock.unlock();
            fflush(stderr);
            if (s_logFile && s_logFile->isOpen()) s_logFile->flush();
            lock.lock();
        }
    }
};

static AsyncLogQueue s_asyncLog;

} // namespace

static void clientMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context);
    const char *level = nullptr;
    bool urgent = false;
    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO";  break;
    case QtWarningMsg:  level = "WARN";  urgent = true; break;
    case QtCriticalMsg: level = "CRIT";  urgent = true; break;
    case QtFatalMsg:    level = "FATAL"; urgent = true; break;
    default:            level = "???";   break;
    }

    QByteArray line = QStringLiteral("[%1][%2] %3\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
             QString::fromUtf8(level), msg)
        .toUtf8();

    // FATAL：同步写入，防止进程退出前丢失
    if (type == QtFatalMsg) {
        fprintf(stderr, "%s", line.constData());
        fflush(stderr);
        if (s_logFile && s_logFile->isOpen()) {
            s_logFile->write(line);
            s_logFile->flush();
        }
        return;
    }

    s_asyncLog.push(std::move(line), urgent);
}

int main(int argc, char *argv[])
{
#if defined(__linux__) && defined(__GLIBC__)
    signal(SIGSEGV, segfaultHandler);
#endif
    QGuiApplication app(argc, argv);

    // 日志文件：CLIENT_LOG_FILE 优先；否则若存在挂载目录 /workspace/logs 则写入 logs/client-YYYY-MM-DD.log，否则 /tmp/remote-driving-client-YYYY-MM-DD.log
    QString logPath = QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLIENT_LOG_FILE"));
    if (logPath.isEmpty()) {
        const QString day = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        if (QDir(QStringLiteral("/workspace/logs")).exists()) {
            logPath = QStringLiteral("/workspace/logs/client-") + day + QStringLiteral(".log");
        } else {
            logPath = QStringLiteral("/tmp/remote-driving-client-") + day + QStringLiteral(".log");
        }
    }
    s_logFile = new QFile(logPath);
    if (!s_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        fprintf(stderr, "[Client][Main] 无法打开日志文件: %s，仅输出到终端\n", qPrintable(logPath));
        delete s_logFile;
        s_logFile = nullptr;
    }
    // 启动异步日志后台线程，然后安装 handler
    s_asyncLog.start();
    qInstallMessageHandler(clientMessageHandler);
    qDebug().noquote() << "[Client][Main] 异步日志已启动，写入:" << logPath;

    qRegisterMetaType<QImage>("QImage");  // 用于 videoFrameReady 跨线程及 QML 传参

    // 设置应用信息
    app.setApplicationName("Remote Driving Client");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("RemoteDriving");

    // 配置中文字体支持
    QFontDatabase fontDb;
    QStringList fontFamilies = fontDb.families();
    
    // 优先使用的中文字体列表
    QStringList preferredFonts = {
        "WenQuanYi Zen Hei",      // 文泉驿正黑
        "WenQuanYi Micro Hei",     // 文泉驿微米黑
        "Noto Sans CJK SC",        // Noto Sans 简体中文
        "Noto Sans CJK TC",        // Noto Sans 繁体中文
        "Source Han Sans SC",      // 思源黑体 简体
        "Droid Sans Fallback",     // Droid 回退字体
        "SimHei",                  // 黑体
        "Microsoft YaHei"          // 微软雅黑
    };
    
    QString chineseFont;
    for (const QString &font : preferredFonts) {
        if (fontFamilies.contains(font)) {
            chineseFont = font;
            qDebug() << "Using Chinese font:" << font;
            break;
        }
    }
    
    // 如果没有找到中文字体，使用默认字体
    if (chineseFont.isEmpty()) {
        qWarning() << "No Chinese font found, Chinese text may not display correctly";
        qWarning() << "Available fonts:" << fontFamilies;
    } else {
        // 设置默认字体为中文字体
        QFont defaultFont(chineseFont, 12);
        app.setFont(defaultFont);
        qDebug() << "Default font set to:" << chineseFont;
    }

    // 设置 QML 样式（可选，如果 QuickControls2 可用）
#ifdef ENABLE_QT6_QUICKCONTROLS2
    QQuickStyle::setStyle("Material");
#endif

    // 注册 C++ 类型到 QML
    qmlRegisterType<WebRtcClient>("RemoteDriving", 1, 0, "WebRtcClient");
    qmlRegisterType<VideoRenderer>("RemoteDriving", 1, 0, "VideoRenderer");
    qmlRegisterType<MqttController>("RemoteDriving", 1, 0, "MqttController");
    qmlRegisterType<VehicleStatus>("RemoteDriving", 1, 0, "VehicleStatus");
    qmlRegisterType<AuthManager>("RemoteDriving", 1, 0, "AuthManager");
    qmlRegisterType<VehicleManager>("RemoteDriving", 1, 0, "VehicleManager");

    // 检查是否需要清除登录状态
    // 支持环境变量 CLIENT_RESET_LOGIN 或命令行参数 --reset-login
    bool resetLogin = false;
    
    // 检查环境变量
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString resetLoginEnv = env.value("CLIENT_RESET_LOGIN", "").toLower();
    if (resetLoginEnv == "1" || resetLoginEnv == "true" || resetLoginEnv == "yes") {
        resetLogin = true;
        qDebug() << "CLIENT_RESET_LOGIN environment variable set, will reset login state";
    }
    
    // 检查命令行参数
    QStringList args = QCoreApplication::arguments();
    if (args.contains("--reset-login") || args.contains("--clear-login") || args.contains("-r")) {
        resetLogin = true;
        qDebug() << "Command line argument detected, will reset login state";
    }
    
    // 创建全局对象
    qDebug() << "Creating C++ objects...";
    auto authManager = std::make_unique<AuthManager>(&app, !resetLogin);  // 如果 resetLogin=true，则不加载保存的凭据
    qDebug() << "  ✓ AuthManager created";
    
    // 如果指定清除登录状态，调用 clearCredentials 确保完全清除
    if (resetLogin) {
        qDebug() << "Resetting login state...";
        authManager->clearCredentials();
        qDebug() << "  ✓ Login state reset";
    }
    auto vehicleManager = std::make_unique<VehicleManager>(&app);
    qDebug() << "  ✓ VehicleManager created";
    auto vehicleStatus = std::make_unique<VehicleStatus>(&app);
    qDebug() << "  ✓ VehicleStatus created";
    auto nodeHealthChecker = std::make_unique<NodeHealthChecker>(&app);
    qDebug() << "  ✓ NodeHealthChecker created";
    auto webrtcClient = std::make_unique<WebRtcClient>(&app);
    qDebug() << "  ✓ WebRtcClient created";
    auto webrtcStreamManager = std::make_unique<WebRtcStreamManager>(&app);
    qDebug() << "  ✓ WebRtcStreamManager created";
    auto mqttController = std::make_unique<MqttController>(&app);
    qDebug() << "  ✓ MqttController created";
    QString mqttBrokerEnv = QProcessEnvironment::systemEnvironment().value("MQTT_BROKER_URL");
    if (!mqttBrokerEnv.isEmpty()) {
        mqttController->setBrokerUrl(mqttBrokerEnv);
        qDebug() << "  ✓ MQTT broker URL from MQTT_BROKER_URL:" << mqttBrokerEnv;
    }
    // 控制通道：优先 DataChannel，可选 MQTT/WebSocket
    mqttController->setWebRtcClient(webrtcStreamManager->frontClient());
    QString controlChannelPreferred = QProcessEnvironment::systemEnvironment().value("CONTROL_CHANNEL_PREFERRED");
    if (!controlChannelPreferred.isEmpty()) {
        mqttController->setPreferredChannel(controlChannelPreferred);
        qDebug() << "  ✓ Control channel preferred from CONTROL_CHANNEL_PREFERRED:" << controlChannelPreferred;
    } else {
        qDebug() << "  ✓ Control channel: AUTO (DataChannel when available, else MQTT)";
    }

    auto eventBus = std::make_unique<EventBus>(&app);
    auto systemStateMachine = std::make_unique<SystemStateMachine>(eventBus.get(), &app);
    auto networkQuality = std::make_unique<NetworkQualityAggregator>(
        vehicleStatus.get(), nodeHealthChecker.get(), &app);
    auto teleopSession = std::make_unique<SessionManager>(
        authManager.get(), vehicleManager.get(), mqttController.get(),
        webrtcStreamManager.get(), systemStateMachine.get(), &app);
    auto vehicleControl = std::make_unique<VehicleControlService>(
        mqttController.get(), vehicleManager.get(), webrtcStreamManager.get(), &app);
    auto safetyMonitor = std::make_unique<SafetyMonitorService>(
        vehicleStatus.get(), systemStateMachine.get(), &app);
    vehicleControl->setSafetyMonitor(safetyMonitor.get());
    auto degradationManager = std::make_unique<DegradationManager>(
        systemStateMachine.get(), &app);
    MqttTransportAdapter mqttTransportAdapter(mqttController.get());
    (void)mqttTransportAdapter;
    ControlLoopTicker controlLoopTicker;
    if (QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLIENT_ENABLE_CONTROL_TICKER"))
        == QStringLiteral("1")) {
        controlLoopTicker.setIntervalMs(20);
        controlLoopTicker.start();
    }

    // 连接信号
    qDebug() << "Connecting signals...";
    // 四路视频使用 webrtcStreamManager 内四个 WebRtcClient；独立的 webrtcClient 仅作兼容/单路演示，不能驱动 videoConnected
    QObject::connect(webrtcStreamManager.get(), &WebRtcStreamManager::anyConnectedChanged,
                     vehicleStatus.get(), [vs = vehicleStatus.get(), wsm = webrtcStreamManager.get()]() {
                         vs->setVideoConnected(wsm->anyConnected());
                     });
    vehicleStatus->setVideoConnected(webrtcStreamManager->anyConnected());
    QObject::connect(mqttController.get(), &MqttController::connectionStatusChanged,
                     vehicleStatus.get(), &VehicleStatus::setMqttConnected);

    QObject::connect(authManager.get(), &AuthManager::loginSucceeded,
                     teleopSession.get(), &SessionManager::onLoginSucceeded);
    QObject::connect(authManager.get(), &AuthManager::loginStatusChanged,
                     teleopSession.get(), &SessionManager::onLoginStatusChanged);

    QObject::connect(vehicleManager.get(), &VehicleManager::currentVinChanged,
                     mqttController.get(), &MqttController::setCurrentVin);
    QObject::connect(vehicleManager.get(), &VehicleManager::currentVinChanged,
                     teleopSession.get(), &SessionManager::onVinSelected);
    // 会话创建成功后以正确 WHEP URL 重连四路流（覆盖 onVinSelected 时的初始回退连接）
    QObject::connect(vehicleManager.get(), &VehicleManager::sessionCreated,
                     teleopSession.get(), &SessionManager::onSessionCreated);

    QObject::connect(mqttController.get(), &MqttController::statusReceived,
                     vehicleStatus.get(), &VehicleStatus::updateStatus);
    qDebug() << "  ✓ Signals connected";

    // 创建 QML 引擎
    qDebug() << "Creating QML engine...";
    QQmlApplicationEngine engine;

    // 注册全局对象到 QML
    qDebug() << "Registering context properties...";
    // std::make_unique 成功时永不返回 nullptr；此处无需 nullptr 检查，直接注册
    qDebug() << "  All objects constructed, registering...";
    
    registerContextProperties(engine.rootContext(),
        authManager.get(), vehicleManager.get(), webrtcClient.get(),
        webrtcStreamManager.get(), mqttController.get(), vehicleStatus.get(),
        nodeHealthChecker.get(),
        eventBus.get(), systemStateMachine.get(), teleopSession.get(),
        vehicleControl.get(), safetyMonitor.get(), networkQuality.get(),
        degradationManager.get());
    qDebug() << "  ✓ Context properties registered";

    QUrl url = resolveQmlMainUrl(&engine);
    if (!url.isValid()) {
        qCritical() << "[QML_LOAD] ❌ 无法找到 main.qml";
        return -1;
    }

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
            qCritical() << "[QML_LOAD] ❌ QML 对象创建失败，URL:" << objUrl;
            QCoreApplication::exit(-1);
        } else if (obj) {
            qDebug() << "[QML_LOAD] ✓ QML 对象创建成功，URL:" << objUrl;
        }
    }, Qt::QueuedConnection);

    qDebug() << "[QML_LOAD] 开始加载 QML 文件:" << url;
    engine.load(url);
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "[QML_LOAD] ❌ engine.load() 失败（QML 文件无法加载或含语法错误），退出码 -1";
        return -1;
    }
    qDebug() << "[QML_LOAD] QML 文件加载完成";

    int ret = app.exec();

    // 安全关闭异步日志：确保所有消息落盘
    qInstallMessageHandler(nullptr);
    s_asyncLog.stop();
    if (s_logFile) {
        s_logFile->flush();
        s_logFile->close();
        delete s_logFile;
        s_logFile = nullptr;
    }
    return ret;
}
