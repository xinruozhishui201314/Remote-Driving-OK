#include "client_logging_setup.h"
#include "client_crash_diagnostics.h"

#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QtGlobal>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <queue>
#include <thread>

static QFile *s_logFile = nullptr;

namespace {

struct AsyncLogQueue {
    static constexpr size_t kCapacity = 4096;
    static constexpr size_t kRotationMaxSizeMb = 100;
    static constexpr int kRotationMaxFiles = 5;

    struct Entry {
        QByteArray data;
        bool urgent;
    };

    std::mutex mtx;
    std::condition_variable cv;
    std::queue<Entry> q;
    std::atomic<bool> running{false};
    std::thread worker;
    std::atomic<uint32_t> dropped{0};

    QString baseLogPath;
    qint64 currentFileSize = 0;
    bool rotationEnabled = false;
    int maxFiles = kRotationMaxFiles;

    void setRotationConfig(const QString &path, bool enabled, int maxKeepFiles)
    {
        std::lock_guard<std::mutex> lock(mtx);
        baseLogPath = path;
        rotationEnabled = enabled;
        maxFiles = maxKeepFiles;
        currentFileSize = 0;
        if (s_logFile && s_logFile->isOpen()) {
            currentFileSize = s_logFile->size();
        }
    }

    void start()
    {
        running.store(true);
        worker = std::thread([this]() {
            while (running.load()) {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait_for(lock, std::chrono::milliseconds(50));
                drainLocked(lock);
            }
            std::unique_lock<std::mutex> lock(mtx);
            drainLocked(lock);
        });
    }

    void stop()
    {
        running.store(false);
        cv.notify_all();
        if (worker.joinable())
            worker.join();
    }

    void push(QByteArray data, bool urgent)
    {
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
    void checkAndRotate(std::unique_lock<std::mutex> &lock)
    {
        if (!rotationEnabled || baseLogPath.isEmpty()) {
            return;
        }
        const qint64 maxSizeBytes = static_cast<qint64>(kRotationMaxSizeMb) * 1024 * 1024;
        if (currentFileSize >= maxSizeBytes) {
            performRotation(lock);
        }
    }

    void performRotation(std::unique_lock<std::mutex> &lock)
    {
        lock.unlock();
        if (s_logFile && s_logFile->isOpen()) {
            s_logFile->flush();
            const QString oldPath = s_logFile->fileName();
            s_logFile->close();

            const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
            QFileInfo fileInfo(oldPath);
            const QString dir = fileInfo.absolutePath();
            const QString baseName = fileInfo.baseName();
            const QString newPath = QString("%1/%2.%3.log").arg(dir, baseName, timestamp);

            if (QFile::exists(oldPath) && QFile::rename(oldPath, newPath)) {
                fprintf(stderr, "[LOG_ROTATION] Log rotated: %s -> %s\n",
                        oldPath.toUtf8().constData(), newPath.toUtf8().constData());
                QStringList args;
                args << "-9" << newPath;
                QProcess::startDetached("gzip", args);

                QDir logDir(dir);
                const QString pattern = baseName + ".20*.log*";
                QStringList oldLogs = logDir.entryList({pattern}, QDir::Files, QDir::Name);
                while (oldLogs.size() > maxFiles) {
                    const QString toRemove = oldLogs.takeFirst();
                    const QString fullPath = dir + "/" + toRemove;
                    QFile::remove(fullPath);
                }
            }

            s_logFile->setFileName(oldPath);
            if (!s_logFile->open(QIODevice::Append | QIODevice::Text)) {
                fprintf(stderr, "[LOG_ROTATION] ERROR: Cannot reopen log file after rotation\n");
            }
        }
        lock.lock();
        currentFileSize = 0;
    }

    void drainLocked(std::unique_lock<std::mutex> &lock)
    {
        checkAndRotate(lock);

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
                currentFileSize += e.data.size();
            }
            if (e.urgent)
                needFlush = true;

            lock.lock();
        }
        if (needFlush) {
            lock.unlock();
            fflush(stderr);
            if (s_logFile && s_logFile->isOpen())
                s_logFile->flush();
            lock.lock();
        }
        checkAndRotate(lock);
    }
};

static AsyncLogQueue s_asyncLog;

static void clientMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context);
    const char *level = nullptr;
    bool urgent = false;
    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        break;
    case QtInfoMsg:
        level = "INFO";
        break;
    case QtWarningMsg:
        level = "WARN";
        urgent = true;
        break;
    case QtCriticalMsg:
        level = "CRIT";
        urgent = true;
        break;
    case QtFatalMsg:
        level = "FATAL";
        urgent = true;
        break;
    default:
        level = "???";
        break;
    }

    QString displayMsg = msg;
    if (type == QtWarningMsg) {
        const QString extra = ClientCrashDiagnostics::annotateIfX11Broke(msg);
        if (!extra.isEmpty()) {
            displayMsg += extra;
        }
    }

    QByteArray line = QStringLiteral("[%1][%2] %3\n")
                          .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                               QString::fromUtf8(level), displayMsg)
                          .toUtf8();

    static bool s_asyncLogDeprecationWarned = false;
    if (!s_asyncLogDeprecationWarned) {
        s_asyncLogDeprecationWarned = true;
        fprintf(stderr, "[DEPRECATION_WARNING] AsyncLogQueue is deprecated. "
                        "Use Logger::instance() for structured logging instead.\n");
    }

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

} // namespace

namespace ClientLogging {

bool init()
{
    QString logPath =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLIENT_LOG_FILE"));
    if (logPath.isEmpty()) {
        const QString day = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        if (QDir(QStringLiteral("/workspace/logs")).exists()) {
            logPath = QStringLiteral("/workspace/logs/client-") + day + QStringLiteral(".log");
        } else {
            logPath =
                QStringLiteral("/tmp/remote-driving-client-") + day + QStringLiteral(".log");
        }
    }
    s_logFile = new QFile(logPath);
    if (!s_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        fprintf(stderr, "[Client][Main] 无法打开日志文件: %s，仅输出到终端\n", qPrintable(logPath));
        delete s_logFile;
        s_logFile = nullptr;
    }

    s_asyncLog.start();

    const bool rotationEnabled =
        QProcessEnvironment::systemEnvironment()
            .value(QStringLiteral("CLIENT_LOG_ROTATION_ENABLED"), QStringLiteral("1"))
            .toInt()
        == 1;
    const int maxKeepFiles =
        QProcessEnvironment::systemEnvironment()
            .value(QStringLiteral("CLIENT_LOG_ROTATION_MAX_FILES"), QStringLiteral("5"))
            .toInt();
    s_asyncLog.setRotationConfig(logPath, rotationEnabled, maxKeepFiles);
    qInstallMessageHandler(clientMessageHandler);
    qDebug().noquote() << "[Client][Main] 异步日志已启动，写入:" << logPath
                       << " 轮转:" << (rotationEnabled ? "启用" : "禁用")
                       << " 保留文件数:" << maxKeepFiles;
    return true;
}

void shutdown()
{
    qInstallMessageHandler(nullptr);
    s_asyncLog.stop();
    if (s_logFile) {
        s_logFile->flush();
        s_logFile->close();
        delete s_logFile;
        s_logFile = nullptr;
    }
}

} // namespace ClientLogging
