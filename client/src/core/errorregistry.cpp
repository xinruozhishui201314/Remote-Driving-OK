#include "errorregistry.h"
#include "../../backend/src/protocol/fault_code.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

ErrorRegistry::ErrorRegistry(QObject* parent) : QObject(parent) {
  qInfo() << "[Client][ErrorRegistry] initialized";
}

ErrorRegistry::Error ErrorRegistry::makeError(Category category, const QString& message,
                                              Level level, const QString& component) {
  Error error;
  error.category = category;
  error.message = message;
  error.level = level;
  error.component = component;
  error.timestampMs = QDateTime::currentMSecsSinceEpoch();
  error.occurrenceCount = 1;
  return error;
}

int ErrorRegistry::reportFault(const QString& code, const QString& component) {
  using namespace teleop::protocol;
  std::string stdCode = code.toStdString();
  if (!FaultCodeManager::exists(stdCode)) {
    return report(Category::System, QStringLiteral("Unknown fault code: %1").arg(code), Level::Warn, component);
  }

  const FaultCode& fc = FaultCodeManager::get(stdCode);
  Category cat = Category::Unknown;
  switch (fc.domain) {
    case FaultDomain::TELEOP: cat = Category::Session; break;
    case FaultDomain::NETWORK: cat = Category::Network; break;
    case FaultDomain::VEHICLE_CTRL: cat = Category::Control; break;
    case FaultDomain::CAMERA: cat = Category::Video; break;
    case FaultDomain::POWER: cat = Category::System; break;
    case FaultDomain::SWEEPER: cat = Category::System; break;
    case FaultDomain::SECURITY: cat = Category::Auth; break;
  }

  Level lvl = Level::Info;
  switch (fc.severity) {
    case FaultSeverity::INFO: lvl = Level::Info; break;
    case FaultSeverity::WARN: lvl = Level::Warn; break;
    case FaultSeverity::ERROR: lvl = Level::Error; break;
    case FaultSeverity::CRITICAL: lvl = Level::Fatal; break;
  }

  QString msg = QString("[%1] %2").arg(code).arg(QString::fromStdString(fc.name));
  return report(cat, msg, lvl, component);
}

int ErrorRegistry::report(Category category, const QString& message, Level level,
                          const QString& component) {
  QMutexLocker lock(&m_mutex);

  const int errorId = m_nextErrorId++;

  // 查找是否有相同类别+相同消息+相同组件的错误（去重聚合）
  auto it = std::find_if(
      m_errors.begin(), m_errors.end(), [category, &message, &component](const Error& e) {
        return e.category == category && e.message == message && e.component == component;
      });

  if (it != m_errors.end()) {
    // 聚合：增加出现次数，更新最新时间戳
    it->occurrenceCount++;
    it->timestampMs = QDateTime::currentMSecsSinceEpoch();
    m_totalErrorCount.fetch_add(1);

    // 如果新的级别更高，更新级别
    if (level > it->level) {
      it->level = level;
    }

    qDebug().noquote() << QString(
                              "[Client][ErrorRegistry] Aggregated error #%1: "
                              "[%2][%3] %4 (occurrences: %5)")
                              .arg(errorId)
                              .arg(categoryToString(it->category))
                              .arg(levelToString(it->level))
                              .arg(it->message)
                              .arg(it->occurrenceCount);

    // 创建临时 Error 对象用于信号
    Error updatedError = *it;
    lock.unlock();
    emit errorReported(updatedError, errorId);
    return errorId;
  }

  // 新错误：添加到列表
  Error error = makeError(category, message, level, component);
  m_errors.append(error);

  // 有界存储：超过上限时移除最老的非 Fatal 错误
  if (m_errors.size() > MAX_TOTAL_ERRORS) {
    pruneOldErrors();
  }

  // 更新统计
  updateStats();

  // 发射信号
  qWarning().noquote() << QString(
                              "[Client][ErrorRegistry] Error #%1: "
                              "[%2][%3][%4] %5")
                              .arg(errorId)
                              .arg(categoryToString(category))
                              .arg(levelToString(level))
                              .arg(component)
                              .arg(message);

  lock.unlock();
  emit errorReported(error, errorId);
  emit errorSummaryChanged(getErrorSummary());

  return errorId;
}

QVector<ErrorRegistry::Error> ErrorRegistry::getErrors(Category category, int maxCount) const {
  QMutexLocker lock(&m_mutex);

  QVector<Error> result;

  if (category == Category::Unknown) {
    result = m_errors;
  } else {
    for (const auto& error : m_errors) {
      if (error.category == category) {
        result.append(error);
      }
    }
  }

  // 按时间戳倒序排序
  std::sort(result.begin(), result.end(),
            [](const Error& a, const Error& b) { return a.timestampMs > b.timestampMs; });

  // 限制返回数量
  if (maxCount > 0 && result.size() > maxCount) {
    result.resize(maxCount);
  }

  return result;
}

void ErrorRegistry::clearErrors(Category category) {
  {
    QMutexLocker lock(&m_mutex);

    if (category == Category::Unknown) {
      const int count = m_errors.size();
      m_errors.clear();
      m_fatalErrors.store(0);
      m_errorErrors.store(0);
      m_totalErrorCount.store(0);
      qInfo() << "[Client][ErrorRegistry] Cleared all errors:" << count << "entries removed";
    } else {
      const QString catStr = categoryToString(category);
      int removed = 0;
      int fatalRemoved = 0;
      int errorRemoved = 0;

      m_errors.erase(
          std::remove_if(m_errors.begin(), m_errors.end(),
                         [category, &removed, &fatalRemoved, &errorRemoved](const Error& e) {
                           if (e.category == category) {
                             if (e.level == Level::Fatal)
                               fatalRemoved++;
                             if (e.level == Level::Error)
                               errorRemoved++;
                             removed++;
                             return true;
                           }
                           return false;
                         }),
          m_errors.end());

      updateStats();
      qInfo() << "[Client][ErrorRegistry] Cleared errors for category" << catStr << ":" << removed
              << "entries removed";
    }
  }
  // getErrorSummary() 也会加锁；必须在释放 m_mutex 后再调用，否则同线程重入死锁
  emit errorSummaryChanged(getErrorSummary());
}

QString ErrorRegistry::getErrorSummary() const {
  QMutexLocker lock(&m_mutex);

  QJsonObject summary;
  summary["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  summary["totalErrors"] = m_errors.size();
  summary["totalErrorCount"] = m_totalErrorCount.load();
  summary["fatalErrors"] = m_fatalErrors.load();
  summary["errorErrors"] = m_errorErrors.load();

  // 按类别统计
  QJsonObject byCategory;
  for (int i = 0; i <= static_cast<int>(Category::System); ++i) {
    const Category cat = static_cast<Category>(i);
    const QString catStr = categoryToString(cat);
    int count = 0;
    int fatalCount = 0;
    int errorCount = 0;
    int warnCount = 0;

    for (const auto& e : m_errors) {
      if (e.category == cat) {
        count++;
        switch (e.level) {
          case Level::Fatal:
            fatalCount++;
            break;
          case Level::Error:
            errorCount++;
            break;
          case Level::Warn:
            warnCount++;
            break;
          case Level::Info:
            break;
        }
      }
    }

    if (count > 0) {
      QJsonObject catObj;
      catObj["count"] = count;
      catObj["fatal"] = fatalCount;
      catObj["error"] = errorCount;
      catObj["warn"] = warnCount;
      byCategory[catStr] = catObj;
    }
  }
  summary["byCategory"] = byCategory;

  // 最近 10 条错误
  QJsonArray recentErrors;
  const int recentCount = qMin(10, m_errors.size());
  for (int i = 0; i < recentCount; ++i) {
    const auto& e = m_errors[i];
    QJsonObject errObj;
    errObj["category"] = categoryToString(e.category);
    errObj["level"] = levelToString(e.level);
    errObj["message"] = e.message;
    errObj["component"] = e.component;
    errObj["timestamp"] = QDateTime::fromMSecsSinceEpoch(e.timestampMs).toString(Qt::ISODate);
    errObj["occurrences"] = e.occurrenceCount;
    recentErrors.append(errObj);
  }
  summary["recentErrors"] = recentErrors;

  return QJsonDocument(summary).toJson(QJsonDocument::Compact);
}

void ErrorRegistry::updateStats() {
  int fatal = 0;
  int error = 0;
  for (const auto& e : m_errors) {
    switch (e.level) {
      case Level::Fatal:
        fatal++;
        break;
      case Level::Error:
        error++;
        break;
      default:
        break;
    }
  }

  const int prevFatal = m_fatalErrors.load();
  const int prevError = m_errorErrors.load();

  m_fatalErrors.store(fatal);
  m_errorErrors.store(error);

  if (fatal != prevFatal) {
    emit fatalErrorsChanged(fatal);
  }
}

void ErrorRegistry::pruneOldErrors() {
  // 移除最老的非 Fatal 错误
  int removed = 0;
  m_errors.erase(std::remove_if(m_errors.begin(), m_errors.end(),
                                [&removed](const Error& e) {
                                  if (removed == 0 && e.level != Level::Fatal) {
                                    removed++;
                                    return true;
                                  }
                                  return false;
                                }),
                 m_errors.end());

  // 如果全是 Fatal，则移除最老的
  if (removed == 0 && !m_errors.isEmpty()) {
    m_errors.removeFirst();
  }

  qDebug() << "[Client][ErrorRegistry] Pruned oldest non-fatal error";

  // 每类错误最多保留MAX_ERRORS_PER_CATEGORY条
  QMap<Category, QVector<Error>> byCategory;
  for (const auto& e : m_errors) {
    byCategory[e.category].append(e);
  }
  for (auto& pair : byCategory) {
    while (pair.size() > MAX_ERRORS_PER_CATEGORY) {
      // 移除最老的错误（按时间戳排序，最早的在前面）
      pair.erase(pair.begin());
    }
  }
  // 重新组装m_errors
  m_errors.clear();
  for (auto it = byCategory.begin(); it != byCategory.end(); ++it) {
    m_errors.append(it.value());
  }
}

void ErrorRegistry::checkLevelDowngrade() {
  QMutexLocker lock(&m_mutex);
  const int64_t now = QDateTime::currentMSecsSinceEpoch();
  const int64_t downgradeWindowMs = 10 * 60 * 1000;  // 10分钟

  for (auto& error : m_errors) {
    if (now - error.timestampMs > downgradeWindowMs) {
      // 超过10分钟未重复，降低级别
      if (error.level == ErrorRegistry::Level::Error) {
        error.level = ErrorRegistry::Level::Warn;
      } else if (error.level == ErrorRegistry::Level::Warn) {
        error.level = ErrorRegistry::Level::Info;
      }
    }
  }
}

QString ErrorRegistry::categoryToString(Category category) {
  switch (category) {
    case Category::Network:
      return QStringLiteral("Network");
    case Category::Video:
      return QStringLiteral("Video");
    case Category::Control:
      return QStringLiteral("Control");
    case Category::Auth:
      return QStringLiteral("Auth");
    case Category::Session:
      return QStringLiteral("Session");
    case Category::Safety:
      return QStringLiteral("Safety");
    case Category::System:
      return QStringLiteral("System");
    default:
      return QStringLiteral("Unknown");
  }
}

QString ErrorRegistry::levelToString(Level level) {
  switch (level) {
    case Level::Info:
      return QStringLiteral("Info");
    case Level::Warn:
      return QStringLiteral("Warn");
    case Level::Error:
      return QStringLiteral("Error");
    case Level::Fatal:
      return QStringLiteral("Fatal");
    default:
      return QStringLiteral("Unknown");
  }
}

ErrorRegistry::Category ErrorRegistry::stringToCategory(const QString& str) {
  if (str == QStringLiteral("Network"))
    return Category::Network;
  if (str == QStringLiteral("Video"))
    return Category::Video;
  if (str == QStringLiteral("Control"))
    return Category::Control;
  if (str == QStringLiteral("Auth"))
    return Category::Auth;
  if (str == QStringLiteral("Session"))
    return Category::Session;
  if (str == QStringLiteral("Safety"))
    return Category::Safety;
  if (str == QStringLiteral("System"))
    return Category::System;
  return Category::Unknown;
}

ErrorRegistry::Level ErrorRegistry::stringToLevel(const QString& str) {
  if (str == QStringLiteral("Info"))
    return Level::Info;
  if (str == QStringLiteral("Warn"))
    return Level::Warn;
  if (str == QStringLiteral("Error"))
    return Level::Error;
  if (str == QStringLiteral("Fatal"))
    return Level::Fatal;
  return Level::Info;
}
