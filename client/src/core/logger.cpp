#include "logger.h"

#include "../utils/TimeUtils.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>

static const char* levelStr(Logger::Level level) {
  switch (level) {
    case Logger::Level::Debug:
      return "DBG";
    case Logger::Level::Info:
      return "INF";
    case Logger::Level::Warning:
      return "WRN";
    case Logger::Level::Critical:
      return "CRT";
  }
  return "???";
}

Logger::Logger(QObject* parent)
    : QObject(parent),
      m_file(),
      m_mutex(),
      m_condition(),
      m_queue(),
      m_running(false),
      m_workerThread(nullptr),
      m_messageInterceptor(),
      m_maxQueueSize(DEFAULT_MAX_QUEUE_SIZE),
      m_droppedCount(0),
      m_rotationConfig(),
      m_baseLogPath(),
      m_currentFileSize(0) {}

Logger::~Logger() {
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

void Logger::initialize(const QString& logFilePath) {
  m_baseLogPath = logFilePath;
  if (!logFilePath.isEmpty()) {
    m_file.setFileName(logFilePath);
    if (!m_file.open(QIODevice::Append | QIODevice::Text)) {
      qWarning() << "[Client][Logger] cannot open log file:" << logFilePath;
    } else {
      m_currentFileSize = m_file.size();
    }
  }

  m_running = true;
  m_workerThread = QThread::create([this]() { workerLoop(); });
  m_workerThread->setObjectName("LoggerWorker");
  m_workerThread->start(QThread::LowPriority);

  qInfo() << "[Client][Logger] initialized"
          << (logFilePath.isEmpty() ? "(stderr only)" : logFilePath);
}

void Logger::setRotationConfig(const RotationConfig& config) {
  QMutexLocker lock(&m_mutex);
  m_rotationConfig = config;
  qInfo() << "[Client][Logger] Rotation config updated: enabled=" << config.enabled
          << " maxSizeMb=" << config.maxSizeMb << " maxFiles=" << config.maxFiles;
}

void Logger::checkAndRotate() {
  if (!m_rotationConfig.enabled || m_baseLogPath.isEmpty()) {
    return;
  }

  const qint64 maxSizeBytes = static_cast<qint64>(m_rotationConfig.maxSizeMb) * 1024 * 1024;
  if (m_currentFileSize >= maxSizeBytes) {
    performRotation();
  }
}

void Logger::performRotation() {
  if (m_baseLogPath.isEmpty()) {
    return;
  }

  // 关闭当前文件
  const QString oldPath = m_file.fileName();
  m_file.close();

  // 生成带时间戳的轮转文件名
  const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
  QFileInfo fileInfo(m_baseLogPath);
  const QString dir = fileInfo.absolutePath();
  const QString baseName = fileInfo.baseName();
  const QString newPath = QString("%1/%2.%3.log").arg(dir, baseName, timestamp);

  // 重命名当前日志文件
  if (QFile::exists(m_baseLogPath)) {
    if (QFile::rename(m_baseLogPath, newPath)) {
      qInfo() << "[Client][Logger] Log rotated:" << m_baseLogPath << "->" << newPath;
      emit rotationTriggered(m_baseLogPath, newPath);
    } else {
      qWarning() << "[Client][Logger] Failed to rotate log:" << m_baseLogPath << "->" << newPath;
    }
  }

  // 压缩旧日志（异步执行，不阻塞）
  if (m_rotationConfig.maxFiles > 0) {
    QStringList args;
    args << "-9" << newPath;
    QProcess::startDetached("gzip", args);
    qDebug() << "[Client][Logger] Compressing rotated log:" << newPath << ".gz";
  }

  // 清理旧日志文件
  if (m_rotationConfig.maxFiles > 0) {
    QDir logDir(dir);
    const QString pattern = baseName + ".20*.log*";
    QStringList oldLogs = logDir.entryList({pattern}, QDir::Files, QDir::Name);
    // 保留最新的 maxFiles 个
    while (oldLogs.size() > m_rotationConfig.maxFiles) {
      const QString toRemove = oldLogs.takeFirst();
      const QString fullPath = dir + "/" + toRemove;
      if (QFile::remove(fullPath)) {
        qDebug() << "[Client][Logger] Removed old log:" << fullPath;
      }
    }
  }

  // 重新打开日志文件
  m_file.setFileName(m_baseLogPath);
  if (!m_file.open(QIODevice::Append | QIODevice::Text)) {
    qWarning() << "[Client][Logger] Cannot reopen log file after rotation:" << m_baseLogPath;
  }
  m_currentFileSize = 0;
}

void Logger::installMessageHandler() { qInstallMessageHandler(Logger::qtMessageHandler); }

void Logger::log(Level level, const QString& module, const QString& component,
                 const QString& message) {
  LogEntry entry;
  entry.level = level;
  entry.formatted = formatEntry(level, module, component, message);
  enqueueWithBound(std::move(entry));
}

void Logger::enqueueWithBound(LogEntry&& entry) {
  QMutexLocker lock(&m_mutex);

  // ── 有界队列核心逻辑（防止内存膨胀）──────────────────────────────
  // 当队列超过上限时，丢弃最老的条目并记录统计
  if (m_queue.size() >= m_maxQueueSize.load()) {
    m_queue.dequeue();
    const int dropped = m_droppedCount.fetch_add(1) + 1;
    // 每丢弃 100 条打印一次警告，避免日志刷屏
    if (dropped % 100 == 0 || dropped <= 3) {
      fprintf(stderr,
              "[Logger] WARNING: Queue overflow, dropped oldest entry. "
              "total_dropped=%d max_size=%d queue_size=%lld\n",
              dropped, m_maxQueueSize.load(), static_cast<long long>(m_queue.size()));
    }
    // 发射溢出信号（可用于监控告警）
    emit queueOverflow(dropped, m_queue.size());
  }

  m_queue.enqueue(std::move(entry));
  m_condition.wakeOne();
}

void Logger::logInfo(const QString& module, const QString& component, const QString& msg) {
  instance().log(Level::Info, module, component, msg);
}

void Logger::logWarn(const QString& module, const QString& component, const QString& msg) {
  instance().log(Level::Warning, module, component, msg);
}

void Logger::logError(const QString& module, const QString& component, const QString& msg) {
  instance().log(Level::Critical, module, component, msg);
}

void Logger::qtMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
  Level level = Level::Info;
  switch (type) {
    case QtDebugMsg:
      level = Level::Debug;
      break;
    case QtInfoMsg:
      level = Level::Info;
      break;
    case QtWarningMsg:
      level = Level::Warning;
      break;
    case QtCriticalMsg:
      level = Level::Critical;
      break;
    case QtFatalMsg:
      level = Level::Critical;
      break;
  }

  QString displayMsg = msg;
  if (instance().m_messageInterceptor) {
    const QString extra = instance().m_messageInterceptor(type, msg);
    if (!extra.isEmpty()) {
      displayMsg += extra;
    }
  }

  const QString module = ctx.category ? QString::fromLatin1(ctx.category) : "Qt";
  LogEntry entry;
  entry.level = level;
  entry.formatted = instance().formatEntry(level, module, QString{}, displayMsg);

  // Fatal: write synchronously and flush
  if (type == QtFatalMsg) {
    fprintf(stderr, "%s\n", entry.formatted.toLocal8Bit().constData());
    fflush(stderr);
    abort();
  }

  // 使用有界队列（防止内存膨胀）
  instance().enqueueWithBound(std::move(entry));
}

void Logger::workerLoop() {
  int waitMs = 100;  // 初始等待时间

  while (true) {
    QQueue<LogEntry> local;
    {
      QMutexLocker lock(&m_mutex);
      if (m_queue.isEmpty() && m_running) {
        m_condition.wait(&m_mutex, waitMs);
        // 指数退避：下次等待更长时间
        if (m_queue.isEmpty()) {
          waitMs = qMin(waitMs * 2, 1000);  // 最大1秒
        } else {
          waitMs = 100;  // 有数据立即恢复快速轮询
        }
      }
      local = std::move(m_queue);
      if (!m_running && local.isEmpty())
        break;
    }
    while (!local.isEmpty()) {
      writeEntry(local.dequeue());
    }
  }
}

void Logger::writeEntry(const LogEntry& entry) {
  // Always write to stderr (无锁，stderr 是线程安全的)
  fprintf(stderr, "%s\n", entry.formatted.toLocal8Bit().constData());

  // 文件写入：先在锁内准备好数据，然后释放锁再写入
  // 这样可以避免文件 I/O 阻塞其他线程的 enqueue 操作
  if (m_file.isOpen()) {
    // 准备要写入的完整行
    const QString line = entry.formatted + u'\n';
    const QByteArray lineUtf8 = line.toUtf8();
    const qint64 lineSize = lineUtf8.size();

    // 仅在锁内更新状态变量（m_currentFileSize）
    // 文件写入在锁外执行，避免阻塞
    {
      QMutexLocker lock(&m_mutex);
      // 检查文件是否仍然打开（可能在锁外被关闭）
      if (!m_file.isOpen()) {
        return;
      }
      // 写入文件（这里仍然需要锁来保护 QFile 的内部状态）
      QTextStream stream(&m_file);
      stream << lineUtf8;
      stream.flush();
      m_currentFileSize += lineSize;
    }
    // 锁已释放，文件写入完成

    // 检查是否需要轮转（在锁外调用，避免死锁）
    checkAndRotate();
  }
}

QString Logger::formatEntry(Level level, const QString& module, const QString& component,
                            const QString& message) const {
  const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
  if (component.isEmpty()) {
    return QString("[%1][%2][%3] %4").arg(ts, levelStr(level), module, message);
  }
  return QString("[%1][%2][%3][%4] %5").arg(ts, levelStr(level), module, component, message);
}
