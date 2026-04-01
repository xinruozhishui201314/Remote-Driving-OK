#include "errorrecoverymanager.h"
#include "../utils/TimeUtils.h"
#include <QDebug>
#include <algorithm>

ErrorRecoveryManager::ErrorRecoveryManager(QObject* parent)
    : QObject(parent)
{
    connect(&m_retryTimer, &QTimer::timeout, this, &ErrorRecoveryManager::attemptRecovery);
    m_retryTimer.setSingleShot(true);
}

void ErrorRecoveryManager::setConfig(const RecoveryConfig& cfg)
{
    m_config = cfg;
}

void ErrorRecoveryManager::registerRecoveryAction(RecoveryLevel level,
                                                    std::function<bool()> action)
{
    m_actions[level] = std::move(action);
}

void ErrorRecoveryManager::reportError(ErrorCategory category, const QString& description)
{
    const int64_t now = TimeUtils::nowMs();
    auto& record = m_errors[category];

    if (record.occurrenceCount == 0) {
        record.category = category;
        record.description = description;
        record.firstOccurrenceMs = now;
        record.currentLevel = RecoveryLevel::AUTO_RETRY;
        record.retryCount = 0;
    }

    record.lastOccurrenceMs = now;
    record.occurrenceCount++;

    qWarning() << "[Client][ErrorRecovery] error reported"
               << "category=" << static_cast<int>(category)
               << "desc=" << description
               << "count=" << record.occurrenceCount
               << "level=" << static_cast<int>(record.currentLevel);

    // Escalate if too many retries at current level
    if (record.retryCount >= m_config.maxRetries) {
        escalate(record);
    }

    if (record.currentLevel == RecoveryLevel::SAFE_STOP) {
        qCritical() << "[Client][ErrorRecovery] escalated to SAFE_STOP";
        emit safeStopRequired(description);
        return;
    }

    m_pendingCategory = category;
    const int delay = calculateRetryDelayMs(record);
    m_retryTimer.start(delay);
}

void ErrorRecoveryManager::clearError(ErrorCategory category)
{
    m_errors.erase(category);
    qInfo() << "[Client][ErrorRecovery] cleared error category="
            << static_cast<int>(category);
}

void ErrorRecoveryManager::attemptRecovery()
{
    auto it = m_errors.find(m_pendingCategory);
    if (it == m_errors.end()) return;

    auto& record = it->second;
    const RecoveryLevel level = record.currentLevel;

    auto actionIt = m_actions.find(level);
    if (actionIt == m_actions.end() || !actionIt->second) {
        qWarning() << "[Client][ErrorRecovery] no action registered for level"
                   << static_cast<int>(level);
        record.retryCount++;
        emit recoveryAttempted(static_cast<int>(level), false);
        return;
    }

    qInfo() << "[Client][ErrorRecovery] attempting recovery level="
            << static_cast<int>(level)
            << "retry=" << record.retryCount;

    const bool success = actionIt->second();
    record.retryCount++;
    emit recoveryAttempted(static_cast<int>(level), success);

    if (success) {
        qInfo() << "[Client][ErrorRecovery] recovery succeeded level="
                << static_cast<int>(level);
        clearError(m_pendingCategory);
    } else {
        qWarning() << "[Client][ErrorRecovery] recovery failed, retry=" << record.retryCount;
        if (record.retryCount >= m_config.maxRetries) {
            escalate(record);
        }
        if (record.currentLevel < RecoveryLevel::SAFE_STOP) {
            m_retryTimer.start(calculateRetryDelayMs(record));
        } else {
            emit safeStopRequired(record.description);
        }
    }
}

void ErrorRecoveryManager::escalate(ErrorRecord& record)
{
    const auto old = record.currentLevel;
    switch (record.currentLevel) {
    case RecoveryLevel::AUTO_RETRY:      record.currentLevel = RecoveryLevel::SERVICE_RESTART; break;
    case RecoveryLevel::SERVICE_RESTART: record.currentLevel = RecoveryLevel::SESSION_REBUILD;  break;
    case RecoveryLevel::SESSION_REBUILD: record.currentLevel = RecoveryLevel::SAFE_STOP;        break;
    case RecoveryLevel::SAFE_STOP:       break;
    }
    record.retryCount = 0;
    qWarning() << "[Client][ErrorRecovery] escalated from level"
               << static_cast<int>(old) << "to" << static_cast<int>(record.currentLevel);
}

int ErrorRecoveryManager::calculateRetryDelayMs(const ErrorRecord& record) const
{
    const double delay = m_config.baseRetryMs
        * std::pow(m_config.backoffFactor, record.retryCount);
    return static_cast<int>(std::min(delay, static_cast<double>(m_config.maxRetryMs)));
}
