#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <atomic>
#include <cstdint>

class HealthChecker : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(HealthChecker)

 public:
  static HealthChecker& instance() {
    static HealthChecker checker;
    return checker;
  }

  explicit HealthChecker(QObject* parent = nullptr);
  ~HealthChecker() override = default;

  // ═══════════════════════════════════════════════════════════════════════════════
  // 健康状态定义
  // ═══════════════════════════════════════════════════════════════════════════════

  enum class Status : uint8_t {
    Unknown = 0,
    Healthy = 1,
    Degraded = 2,
    Unhealthy = 3,
  };
  Q_ENUM(Status)

  struct ComponentHealth {
    Status status = Status::Unknown;
    QString message;
    int64_t lastCheckMs = 0;
    int64_t lastUpdateMs = 0;  // 组件最后更新时间
    int errorCount = 0;
    int timeoutMs = 30000;  // 默认超时 30s
    bool timeoutEnabled = true;
  };

  struct SystemHealth {
    Status overallStatus = Status::Unknown;
    bool videoDecoderHealthy = false;
    bool mqttConnected = false;
    bool controlLoopActive = false;
    bool gpuAccelerated = false;
    bool networkQualityOk = false;
    QString currentState;
    int64_t uptimeMs = 0;

    // 内存和资源监控
    int64_t memoryUsedBytes = 0;
    int64_t memoryTotalBytes = 0;
    double memoryUsagePercent = 0.0;
    int threadCount = 0;
    int cpuPercent = 0;

    QVariantMap componentStatus;
  };

  struct SystemReadiness {
    bool ready = false;
    bool livenessOk = false;
    QStringList unreadyReasons;
    QVariantMap details;
  };

  // ═══════════════════════════════════════════════════════════════════════════════
  // 核心检查方法
  // ═══════════════════════════════════════════════════════════════════════════════

  // 获取完整健康状态（用于 /health 端点）
  Q_INVOKABLE SystemHealth checkHealth() const;

  // 获取就绪状态（用于 /ready 端点，K8s 探针）
  Q_INVOKABLE SystemReadiness checkReadiness() const;

  // ═══════════════════════════════════════════════════════════════════════════════
  // 子系统状态更新（由各子系统调用）
  // ═══════════════════════════════════════════════════════════════════════════════

  void updateVideoDecoderStatus(bool healthy, const QString& message = QString());
  void updateMqttStatus(bool connected, const QString& message = QString());
  void updateControlLoopStatus(bool active, const QString& message = QString());
  void updateGpuStatus(bool accelerated, const QString& message = QString());
  void updateNetworkQuality(bool ok, const QString& message = QString());
  void updateOverallState(const QString& state);

  // ═══════════════════════════════════════════════════════════════════════════════
  // 超时检测与降级
  // ═══════════════════════════════════════════════════════════════════════════════

  // 更新组件超时阈值
  Q_INVOKABLE void updateComponentTimeout(const QString& component, int timeoutMs);

  // 获取组件超时阈值
  Q_INVOKABLE int getComponentTimeout(const QString& component) const;

  // 启用/禁用组件超时检测
  Q_INVOKABLE void setComponentTimeoutEnabled(const QString& component, bool enabled);

  // 手动检查超时并降级（通常由定时器调用）
  void checkTimeouts();

  // ═══════════════════════════════════════════════════════════════════════════════
  // 内存监控
  // ═══════════════════════════════════════════════════════════════════════════════

  // 更新内存信息
  Q_INVOKABLE void updateMemoryInfo(int64_t usedBytes, int64_t totalBytes);

  // 获取当前内存使用信息
  Q_INVOKABLE QVariantMap getMemoryInfo() const;

  // 获取系统内存使用（自动检测）
  static QVariantMap getSystemMemoryUsage();

  // ═══════════════════════════════════════════════════════════════════════════════
  // 线程池监控（预留接口）
  // ═══════════════════════════════════════════════════════════════════════════════

  // 更新线程池状态
  Q_INVOKABLE void updateThreadPoolStatus(int activeThreads, int totalThreads, int queueSize);

  // 获取线程池信息
  Q_INVOKABLE QVariantMap getThreadPoolStatus() const;

  // ═══════════════════════════════════════════════════════════════════════════════
  // 插件监控（预留接口）
  // ═══════════════════════════════════════════════════════════════════════════════

  // 更新插件状态
  Q_INVOKABLE void updatePluginStatus(const QString& pluginName, bool loaded,
                                      const QString& version = QString());

  // 获取所有插件状态
  Q_INVOKABLE QVariantMap getAllPluginStatus() const;

  // ═══════════════════════════════════════════════════════════════════════════════
  // 错误计数（用于错误率计算）
  // ═══════════════════════════════════════════════════════════════════════════════

  void incrementErrorCount(const QString& component);
  void resetErrorCount(const QString& component);
  int getErrorCount(const QString& component) const;

  // ═══════════════════════════════════════════════════════════════════════════════
  // 启动时间
  // ═══════════════════════════════════════════════════════════════════════════════

  void recordStartTime();
  int64_t getUptimeMs() const;

 signals:
  // 健康状态变化信号
  void healthStatusChanged(Status status);
  void readinessChanged(bool ready);

  // 组件状态变化信号
  void componentStatusChanged(const QString& component, Status status);

  // 超时降级信号
  void componentTimeout(const QString& component, int64_t lastUpdateMs);

  // 内存告警信号
  void memoryWarning(double usagePercent);
  void memoryCritical(double usagePercent);

 private:
  void updateComponentHealth(const QString& component, Status status, const QString& message);
  Status computeOverallStatus() const;
  bool computeReadiness() const;
  void checkComponentTimeout(const QString& component);

  QVariantMap m_components;
  QString m_currentState;
  int64_t m_startTimeMs = 0;
  std::atomic<int> m_videoDecoderErrors{0};
  std::atomic<int> m_mqttErrors{0};
  std::atomic<int> m_controlErrors{0};
  std::atomic<int> m_networkErrors{0};

  // 超时检测定时器
  QTimer m_timeoutCheckTimer;

  // 内存监控
  int64_t m_memoryUsedBytes = 0;
  int64_t m_memoryTotalBytes = 0;

  // 线程池监控
  int m_threadPoolActiveThreads = 0;
  int m_threadPoolTotalThreads = 0;
  int m_threadPoolQueueSize = 0;
  bool m_threadPoolEnabled = false;

  // 插件状态
  QVariantMap m_plugins;
  bool m_pluginsEnabled = false;
};
