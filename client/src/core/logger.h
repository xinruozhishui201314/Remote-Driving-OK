#pragma once
#include <QObject>
#include <QString>
#include <QFile>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>
#include <QAtomicInt>
#include <QFileInfo>
#include <functional>
#include <atomic>

/**
 * 结构化异步日志（《客户端架构设计》§3.1.3 & §7.3）。
 *
 * 特性：
 * - 异步写入，不阻塞调用线程
 * - 前缀 [Module][Component] 统一格式
 * - 安装 Qt 消息处理器，所有 qDebug/qInfo/qWarning/qCritical 都通过此路由
 * - 支持文件输出（循环写入）
 * - 有界队列防止内存膨胀（产品化关键修复）
 * - 日志轮转支持（自动压缩和清理旧日志）
 *
 * 产品化增强：
 * - 有界队列：MAX_QUEUE_SIZE 上限防止内存膨胀
 * - 溢出统计：m_droppedCount 记录丢弃的日志数
 * - 配置化：setMaxQueueSize() 支持运行时调整
 * - 日志轮转：支持按大小轮转、自动压缩、保留数量控制
 *
 * 初始化：
 *   Logger::instance().initialize("/var/log/remote-driving-client.log");
 */
class Logger : public QObject {
    Q_OBJECT

public:
    enum class Level { Debug, Info, Warning, Critical };

    // 有界队列容量（默认 10000 条，超过则丢弃最老条目）
    static constexpr int DEFAULT_MAX_QUEUE_SIZE = 10000;

    // 日志轮转默认配置
    static constexpr int DEFAULT_ROTATION_MAX_SIZE_MB = 100;
    static constexpr int DEFAULT_ROTATION_MAX_FILES = 5;

    static Logger& instance() {
        static Logger log;
        return log;
    }

    explicit Logger(QObject* parent = nullptr);
    ~Logger() override;

    void initialize(const QString& logFilePath = QString{});

    // 有界队列大小设置（运行时可调整）
    void setMaxQueueSize(int maxSize) { m_maxQueueSize.store(maxSize); }
    int maxQueueSize() const { return m_maxQueueSize.load(); }

    // 溢出统计（用于监控告警）
    int droppedCount() const { return m_droppedCount.load(); }
    void resetDroppedCount() { m_droppedCount.store(0); }

    // 日志轮转配置
    struct RotationConfig {
        bool enabled = false;
        int maxSizeMb = DEFAULT_ROTATION_MAX_SIZE_MB;
        int maxFiles = DEFAULT_ROTATION_MAX_FILES;
        QString filePrefix = "remote-driving-client";
    };
    void setRotationConfig(const RotationConfig& config);
    RotationConfig rotationConfig() const { return m_rotationConfig; }

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

signals:
    // 队列溢出信号（可用于触发告警）
    void queueOverflow(int droppedCount, int queueSize);
    // 日志轮转信号
    void rotationTriggered(const QString& oldFile, const QString& newFile);

private:
    struct LogEntry {
        Level level;
        QString formatted;
    };

    void workerLoop();
    void writeEntry(const LogEntry& entry);
    QString formatEntry(Level level, const QString& module, const QString& component,
                       const QString& message) const;

    // 有界队列核心方法
    void enqueueWithBound(LogEntry&& entry);

    // 日志轮转
    void checkAndRotate();
    void performRotation();

    QFile m_file;
    QMutex m_mutex;
    QWaitCondition m_condition;
    QQueue<LogEntry> m_queue;
    bool m_running = false;
    QThread* m_workerThread = nullptr;

    // 有界队列配置
    std::atomic<int> m_maxQueueSize{DEFAULT_MAX_QUEUE_SIZE};
    std::atomic<int> m_droppedCount{0};

    // 日志轮转配置
    RotationConfig m_rotationConfig;
    QString m_baseLogPath;
    qint64 m_currentFileSize = 0;
};
