#include "logger.h"
#include "../utils/TimeUtils.h"
#include <QDateTime>
#include <QDebug>
#include <QTextStream>

static const char* levelStr(Logger::Level level) {
    switch (level) {
    case Logger::Level::Debug:    return "DBG";
    case Logger::Level::Info:     return "INF";
    case Logger::Level::Warning:  return "WRN";
    case Logger::Level::Critical: return "CRT";
    }
    return "???";
}

Logger::Logger(QObject* parent)
    : QObject(parent)
{}

Logger::~Logger()
{
    if (m_workerThread) {
        {
            QMutexLocker lock(&m_mutex);
            m_running = false;
            m_condition.wakeOne();
        }
        m_workerThread->wait(3000);
        delete m_workerThread;
    }
}

void Logger::initialize(const QString& logFilePath)
{
    if (!logFilePath.isEmpty()) {
        m_file.setFileName(logFilePath);
        if (!m_file.open(QIODevice::Append | QIODevice::Text)) {
            qWarning() << "[Client][Logger] cannot open log file:" << logFilePath;
        }
    }

    m_running = true;
    m_workerThread = QThread::create([this]() { workerLoop(); });
    m_workerThread->setObjectName("LoggerWorker");
    m_workerThread->start(QThread::LowPriority);

    qInfo() << "[Client][Logger] initialized"
            << (logFilePath.isEmpty() ? "(stderr only)" : logFilePath);
}

void Logger::installMessageHandler()
{
    qInstallMessageHandler(Logger::qtMessageHandler);
}

void Logger::log(Level level, const QString& module, const QString& component,
                  const QString& message)
{
    LogEntry entry;
    entry.level = level;
    entry.formatted = formatEntry(level, module, component, message);
    {
        QMutexLocker lock(&m_mutex);
        m_queue.enqueue(std::move(entry));
        m_condition.wakeOne();
    }
}

void Logger::logInfo(const QString& module, const QString& component, const QString& msg)
{
    instance().log(Level::Info, module, component, msg);
}

void Logger::logWarn(const QString& module, const QString& component, const QString& msg)
{
    instance().log(Level::Warning, module, component, msg);
}

void Logger::logError(const QString& module, const QString& component, const QString& msg)
{
    instance().log(Level::Critical, module, component, msg);
}

void Logger::qtMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    Level level = Level::Info;
    switch (type) {
    case QtDebugMsg:    level = Level::Debug; break;
    case QtInfoMsg:     level = Level::Info; break;
    case QtWarningMsg:  level = Level::Warning; break;
    case QtCriticalMsg: level = Level::Critical; break;
    case QtFatalMsg:    level = Level::Critical; break;
    }

    const QString module = ctx.category ? QString::fromLatin1(ctx.category) : "Qt";
    LogEntry entry;
    entry.level = level;
    entry.formatted = instance().formatEntry(level, module, QString{}, msg);

    // Fatal: write synchronously and flush
    if (type == QtFatalMsg) {
        fprintf(stderr, "%s\n", entry.formatted.toLocal8Bit().constData());
        fflush(stderr);
        abort();
    }

    QMutexLocker lock(&instance().m_mutex);
    instance().m_queue.enqueue(std::move(entry));
    instance().m_condition.wakeOne();
}

void Logger::workerLoop()
{
    while (true) {
        QQueue<LogEntry> local;
        {
            QMutexLocker lock(&m_mutex);
            if (m_queue.isEmpty() && m_running) {
                m_condition.wait(&m_mutex, 100);
            }
            local = std::move(m_queue);
            if (!m_running && local.isEmpty()) break;
        }
        while (!local.isEmpty()) {
            writeEntry(local.dequeue());
        }
    }
}

void Logger::writeEntry(const LogEntry& entry)
{
    // Always write to stderr
    fprintf(stderr, "%s\n", entry.formatted.toLocal8Bit().constData());

    // Write to file if open
    if (m_file.isOpen()) {
        QTextStream stream(&m_file);
        stream << entry.formatted << "\n";
        stream.flush();
    }
}

QString Logger::formatEntry(Level level, const QString& module, const QString& component,
                              const QString& message) const
{
    const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    if (component.isEmpty()) {
        return QString("[%1][%2][%3] %4").arg(ts, levelStr(level), module, message);
    }
    return QString("[%1][%2][%3][%4] %5")
        .arg(ts, levelStr(level), module, component, message);
}
