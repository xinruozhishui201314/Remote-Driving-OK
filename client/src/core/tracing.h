#pragma once
#include <QMutex>
#include <QObject>
#include <QString>
#include <QUuid>
#include <QVariant>
#include <QVariantMap>

#include <atomic>
#include <chrono>
#include <functional>

/**
 * 分布式追踪系统
 *
 * 实现 TraceID 跨模块传播，支持日志和消息中的追踪
 * 符合 OpenTelemetry 规范（简化版）
 *
 * 使用方式：
 *   // 生成新的 TraceID
 *   QString traceId = Tracing::generateTraceId();
 *
 *   // 获取/设置当前 TraceID
 *   Tracing::instance().setCurrentTraceId(traceId);
 *   QString currentTrace = Tracing::instance().currentTraceId();
 *
 *   // 创建 Span 追踪操作
 *   auto span = Tracing::instance().beginSpan("MQTT", "publish");
 *   // ... 执行操作 ...
 *   Tracing::instance().endSpan(span);
 *
 *   // 使用 RAII 风格的 trace 包装
 *   Tracing::instance().trace("MQTT", []() {
 *       // 操作
 *   });
 *
 *   // 在消息/日志中添加 TraceID
 *   QVariantMap ctx = Tracing::instance().addTraceToContext(originalMap);
 */
class Tracing : public QObject {
  Q_OBJECT

 public:
  static Tracing& instance() {
    static Tracing tracer;
    return tracer;
  }

  // ═══════════════════════════════════════════════════════════════
  // ID 生成
  // ═══════════════════════════════════════════════════════════════

  /**
   * 生成新的 TraceID（UUID v4 格式）
   * 32 个十六进制字符，符合 W3C Trace Context 规范
   */
  static QString generateTraceId();

  /**
   * 生成 SpanID（8 字节十六进制）
   * 16 个十六进制字符
   */
  static QString generateSpanId();

  // ═══════════════════════════════════════════════════════════════
  // 当前 TraceID 管理
  // ═══════════════════════════════════════════════════════════════

  /**
   * 获取当前 TraceID
   */
  QString currentTraceId() const;

  /**
   * 设置当前 TraceID（跨模块传播）
   */
  void setCurrentTraceId(const QString& traceId);

  /**
   * 清除当前 TraceID
   */
  void clearCurrentTraceId();

  // ═══════════════════════════════════════════════════════════════
  // Span 管理
  // ═══════════════════════════════════════════════════════════════

  /**
   * Span 结构体，记录追踪跨度信息
   */
  struct Span {
    QString traceId;
    QString spanId;
    QString parentSpanId;
    QString name;
    QString operation;
    int64_t startTimeNs = 0;
    int64_t endTimeNs = 0;
    QVariantMap tags;
    QString error;
    bool ended = false;

    /**
     * 计算跨度持续时间（毫秒）
     */
    int64_t durationMs() const {
      if (endTimeNs == 0 || startTimeNs == 0)
        return 0;
      return (endTimeNs - startTimeNs) / 1'000'000;
    }

    /**
     * 检查是否有错误
     */
    bool hasError() const { return !error.isEmpty(); }
  };

  /**
   * 开始一个新的 Span
   * 自动继承当前 TraceID，并生成新的 SpanID
   */
  Q_INVOKABLE Span beginSpan(const QString& name, const QString& operation = QString{});

  /**
   * 结束一个 Span
   * 记录结束时间并发射信号
   */
  Q_INVOKABLE void endSpan(Span& span);

  /**
   * 结束一个 Span（通过 name 查找最后一个活跃的）
   */
  Q_INVOKABLE void endSpanByName(const QString& name);

  /**
   * 给 Span 添加标签
   */
  Q_INVOKABLE void addSpanTag(Span& span, const QString& key, const QVariant& value);

  /**
   * 给 Span 标记错误
   */
  Q_INVOKABLE void markSpanError(Span& span, const QString& error);

  // ═══════════════════════════════════════════════════════════════
  // 上下文传播
  // ═══════════════════════════════════════════════════════════════

  /**
   * 将 TraceID 添加到上下文（Map）
   * 用于日志消息或 MQTT 消息头
   */
  Q_INVOKABLE QVariantMap addTraceToContext(const QVariantMap& original) const;

  /**
   * 从上下文中提取 TraceID
   */
  Q_INVOKABLE QString extractTraceId(const QVariantMap& context) const;

  /**
   * 从上下文中提取 SpanID
   */
  Q_INVOKABLE QString extractSpanId(const QVariantMap& context) const;

  /**
   * 从上下文中提取父 SpanID
   */
  Q_INVOKABLE QString extractParentSpanId(const QVariantMap& context) const;

  // ═══════════════════════════════════════════════════════════════
  // RAII 风格追踪包装
  // ═══════════════════════════════════════════════════════════════

  /**
   * 追踪函数执行
   * 自动管理 Span 的开始和结束
   */
  template <typename Func>
  auto trace(const QString& name, Func&& func) -> decltype(func()) {
    Span span = beginSpan(name);
    try {
      auto result = func();
      endSpan(span);
      return result;
    } catch (...) {
      markSpanError(span, "exception");
      endSpan(span);
      throw;
    }
  }

  /**
   * 追踪函数执行（带 operation 参数）
   */
  template <typename Func>
  auto trace(const QString& name, const QString& operation, Func&& func) -> decltype(func()) {
    Span span = beginSpan(name, operation);
    try {
      auto result = func();
      endSpan(span);
      return result;
    } catch (...) {
      markSpanError(span, "exception");
      endSpan(span);
      throw;
    }
  }

  // ═══════════════════════════════════════════════════════════════
  // 追踪采样
  // ═══════════════════════════════════════════════════════════════

  /**
   * 设置采样率 (0.0 - 1.0)
   * 仅追踪采样率比例的事件
   */
  Q_INVOKABLE void setSamplingRate(double rate);

  /**
   * 获取当前采样率
   */
  Q_INVOKABLE double samplingRate() const;

  /**
   * 检查是否应该采样
   */
  bool shouldSample() const;

  /**
   * 获取追踪统计
   */
  struct TracingStats {
    int64_t totalSpansStarted = 0;
    int64_t totalSpansEnded = 0;
    int64_t totalErrors = 0;
    int activeSpans = 0;
    double currentSamplingRate = 1.0;
  };
  Q_INVOKABLE TracingStats getStats() const;

 signals:
  /**
   * Span 开始信号
   */
  void spanStarted(const QString& traceId, const QString& spanId, const QString& name);

  /**
   * Span 结束信号
   */
  void spanEnded(const QString& traceId, const QString& spanId, int64_t durationMs);

  /**
   * Span 错误信号
   */
  void spanError(const QString& traceId, const QString& spanId, const QString& error);

  /**
   * 追踪采样率变更
   */
  void samplingRateChanged(double rate);

 private:
  explicit Tracing(QObject* parent = nullptr);
  ~Tracing() override;

  // 禁用拷贝
  Tracing(const Tracing&) = delete;
  Tracing& operator=(const Tracing&) = delete;

  /**
   * 获取当前时间（纳秒）
   */
  static int64_t currentTimeNs();

  /**
   * 创建子 Span（设置父 SpanID）
   */
  Span createChildSpan(const QString& name, const QString& operation);

  mutable QString m_currentTraceId;
  mutable QString m_currentSpanId;
  std::atomic<int> m_spanCounter{0};
  std::atomic<double> m_samplingRate{1.0};

  // 追踪统计
  std::atomic<int64_t> m_totalSpansStarted{0};
  std::atomic<int64_t> m_totalSpansEnded{0};
  std::atomic<int64_t> m_totalErrors{0};

  // 活跃 Span 栈（用于嵌套追踪）
  QVariantList m_activeSpans;
  mutable QMutex m_spansMutex;
};

// Metrics convenience macros (forward to MetricsCollector::instance())
// METRICS_INC 支持 1 参数（默认 +1）或 2 参数（指定增量）
#define METRICS_SET(name, value) \
  MetricsCollector::instance().set(QStringLiteral(name), static_cast<double>(value))
#define METRICS_INC(...) METRICS_INC_IMPL(__VA_ARGS__, 1.0)
#define METRICS_INC_IMPL(name, _d, ...) \
  MetricsCollector::instance().increment(QStringLiteral(name), static_cast<double>(_d))
#define METRICS_OBSERVE(name, value) \
  MetricsCollector::instance().observe(QStringLiteral(name), static_cast<double>(value))

// 便利宏
#define TRACE_SCOPE(name) auto __tracing_span = Tracing::instance().beginSpan(name)

#define TRACE_OPERATION(name, op) auto __tracing_span = Tracing::instance().beginSpan(name, op)

#define TRACE_END() Tracing::instance().endSpan(__tracing_span)

// 日志宏（自动添加 traceId）
#define TRACE_LOG_CONTEXT() Tracing::instance().addTraceToContext(QVariantMap())
