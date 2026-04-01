#pragma once
#include <QObject>
#include <QTimer>
#include <functional>
#include <map>

/**
 * 错误恢复管理器（《客户端架构设计》§5.2）。
 *
 * 4级恢复策略（自动升级）：
 *   Level 1 - AUTO_RETRY:       自动重试（指数退避，最多 3 次）
 *   Level 2 - SERVICE_RESTART:  重启受影响的服务模块
 *   Level 3 - SESSION_REBUILD:  断开并重建整个会话
 *   Level 4 - SAFE_STOP:        安全停车，需人工干预
 */
class ErrorRecoveryManager : public QObject {
    Q_OBJECT

public:
    enum class RecoveryLevel : uint8_t {
        AUTO_RETRY       = 1,
        SERVICE_RESTART  = 2,
        SESSION_REBUILD  = 3,
        SAFE_STOP        = 4,
    };

    enum class ErrorCategory {
        NETWORK_ERROR,
        MEDIA_ERROR,
        CONTROL_ERROR,
        AUTH_ERROR,
        HARDWARE_ERROR,
        UNKNOWN,
    };

    struct ErrorRecord {
        ErrorCategory category;
        QString description;
        int64_t firstOccurrenceMs = 0;
        int64_t lastOccurrenceMs  = 0;
        int     occurrenceCount   = 0;
        RecoveryLevel currentLevel = RecoveryLevel::AUTO_RETRY;
        int     retryCount = 0;
    };

    struct RecoveryConfig {
        int maxRetries      = 3;
        int baseRetryMs     = 500;
        int maxRetryMs      = 30000;
        double backoffFactor = 2.0;
    };

    explicit ErrorRecoveryManager(QObject* parent = nullptr);

    void setConfig(const RecoveryConfig& cfg);

    // 注册恢复动作
    void registerRecoveryAction(RecoveryLevel level, std::function<bool()> action);

    // 报告错误（触发恢复流程）
    void reportError(ErrorCategory category, const QString& description);

    // 手动重置错误状态
    void clearError(ErrorCategory category);

signals:
    void recoveryAttempted(int level, bool success);
    void recoveryFailed(const QString& reason);
    void safeStopRequired(const QString& reason);

private slots:
    void attemptRecovery();

private:
    void escalate(ErrorRecord& record);
    int calculateRetryDelayMs(const ErrorRecord& record) const;

    RecoveryConfig m_config;
    std::map<ErrorCategory, ErrorRecord> m_errors;
    std::map<RecoveryLevel, std::function<bool()>> m_actions;
    QTimer m_retryTimer;
    ErrorCategory m_pendingCategory = ErrorCategory::UNKNOWN;
};
