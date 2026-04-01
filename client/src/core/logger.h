#pragma once
#include <QObject>
#include <QString>
#include <QFile>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>
#include <functional>

/**
 * 结构化异步日志（《客户端架构设计》§3.1.3 & §7.3）。
 *
 * 特性：
 * - 异步写入，不阻塞调用线程
 * - 前缀 [Module][Component] 统一格式
 * - 安装 Qt 消息处理器，所有 qDebug/qInfo/qWarning/qCritical 都通过此路由
 * - 支持文件输出（循环写入）
 *
 * 初始化：
 *   Logger::instance().initialize("/var/log/remote-driving-client.log");
 */
class Logger : public QObject {
    Q_OBJECT

public:
    enum class Level { Debug, Info, Warning, Critical };

    static Logger& instance() {
        static Logger log;
        return log;
    }

    explicit Logger(QObject* parent = nullptr);
    ~Logger() override;

    void initialize(const QString& logFilePath = QString{});

    // 安装 Qt 消息处理器（替换默认 stderr 输出）
    void installMessageHandler();

    // 直接写入（给 C++ 代码调用）
    void log(Level level, const QString& module, const QString& component,
             const QString& message);

    // 静态便利方法
    static void logInfo(const QString& module, const QString& component, const QString& msg);
    static void logWarn(const QString& module, const QString& component, const QString& msg);
    static void logError(const QString& module, const QString& component, const QString& msg);

    // Qt 消息处理器回调
    static void qtMessageHandler(QtMsgType type, const QMessageLogContext& ctx,
                                  const QString& msg);

private:
    struct LogEntry {
        Level level;
        QString formatted;
    };

    void workerLoop();
    void writeEntry(const LogEntry& entry);
    QString formatEntry(Level level, const QString& module, const QString& component,
                        const QString& message) const;

    QFile m_file;
    QMutex m_mutex;
    QWaitCondition m_condition;
    QQueue<LogEntry> m_queue;
    bool m_running = false;
    QThread* m_workerThread = nullptr;
};
