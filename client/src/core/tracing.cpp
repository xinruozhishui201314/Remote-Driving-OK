#include "tracing.h"

#include "logger.h"
#include "metricscollector.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QUuid>

// OpenTelemetry 追踪上下文常量
static const char* TRACE_ID_KEY = "trace_id";
static const char* SPAN_ID_KEY = "span_id";
static const char* PARENT_SPAN_ID_KEY = "parent_span_id";

// ═══════════════════════════════════════════════════════════════════════════════
// 静态方法实现
// ═══════════════════════════════════════════════════════════════════════════════

QString Tracing::generateTraceId() {
  // 生成 UUID v4 格式的 TraceID
  // 移除连字符并转换为大写十六进制（32 字符）
  const QUuid uuid = QUuid::createUuid();
  QString traceId = uuid.toString(QUuid::WithoutBraces).toUpper();
  // 移除花括号（如果存在）
  traceId.remove('{').remove('}');
  return traceId;
}

QString Tracing::generateSpanId() {
  // 生成 8 字节随机 SpanID（16 个十六进制字符）
  // 使用 QRandomGenerator 以确保高质量随机数
  quint64 rand1 = QRandomGenerator::global()->generate64();
  quint64 rand2 = QRandomGenerator::global()->generate64();

  // 组合为 16 字符十六进制字符串
  QString spanId;
  spanId.reserve(16);
  spanId = QString("%1%2").arg(rand1, 8, 16, QChar('0')).arg(rand2, 8, 16, QChar('0'));
  return spanId.toUpper();
}

int64_t Tracing::currentTimeNs() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 构造函数与析构函数
// ═══════════════════════════════════════════════════════════════════════════════

Tracing::Tracing(QObject* parent)
    : QObject(parent),
      m_currentTraceId(),
      m_currentSpanId(),
      m_spanCounter(0),
      m_samplingRate(1.0),
      m_totalSpansStarted(0),
      m_totalSpansEnded(0),
      m_totalErrors(0),
      m_activeSpans(),
      m_spansMutex() {
  qInfo() << "[Tracing] Initialized";
}

Tracing::~Tracing() = default;

// ═══════════════════════════════════════════════════════════════════════════════
// TraceID 管理
// ═══════════════════════════════════════════════════════════════════════════════

QString Tracing::currentTraceId() const {
  QMutexLocker locker(&m_spansMutex);
  return m_currentTraceId;
}

void Tracing::setCurrentTraceId(const QString& traceId) {
  QMutexLocker locker(&m_spansMutex);
  m_currentTraceId = traceId;
  qDebug() << "[Tracing] Current trace ID set:" << traceId;
}

void Tracing::clearCurrentTraceId() {
  QMutexLocker locker(&m_spansMutex);
  m_currentTraceId.clear();
  m_currentSpanId.clear();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Span 管理
// ═══════════════════════════════════════════════════════════════════════════════

Tracing::Span Tracing::beginSpan(const QString& name, const QString& operation) {
  // 检查采样率
  if (!shouldSample()) {
    // 返回空 Span，表示不追踪
    return Span{};
  }

  QMutexLocker locker(&m_spansMutex);

  // 获取或生成 TraceID
  QString traceId = m_currentTraceId;
  if (traceId.isEmpty()) {
    traceId = generateTraceId();
    m_currentTraceId = traceId;
  }

  // 保存父 SpanID（如果存在）
  QString parentSpanId = m_currentSpanId;

  // 生成新的 SpanID
  QString spanId = generateSpanId();
  m_currentSpanId = spanId;

  // 构建 Span
  Span span;
  span.traceId = traceId;
  span.spanId = spanId;
  span.parentSpanId = parentSpanId;
  span.name = name;
  span.operation = operation;
  span.startTimeNs = currentTimeNs();

  // 更新统计
  m_totalSpansStarted.fetch_add(1, std::memory_order_relaxed);

  locker.unlock();

  // 发射信号
  emit spanStarted(traceId, spanId, name);

  // 记录活跃追踪数
  METRICS_OBSERVE("tracing_spans_active",
                  static_cast<double>(m_totalSpansStarted.load() - m_totalSpansEnded.load()));

  qDebug().noquote() << "[Tracing] Span started: traceId=" << traceId << "spanId=" << spanId
                     << "name=" << name << "operation=" << operation;

  return span;
}

void Tracing::endSpan(Span& span) {
  if (span.traceId.isEmpty() || span.ended) {
    // 空 Span 或已结束，无需处理
    return;
  }

  // 记录结束时间
  span.endTimeNs = currentTimeNs();
  span.ended = true;

  // 更新统计
  m_totalSpansEnded.fetch_add(1, std::memory_order_relaxed);
  if (span.hasError()) {
    m_totalErrors.fetch_add(1, std::memory_order_relaxed);
  }

  // 计算持续时间
  int64_t durationMs = span.durationMs();

  // 发射信号
  emit spanEnded(span.traceId, span.spanId, durationMs);
  if (span.hasError()) {
    emit spanError(span.traceId, span.spanId, span.error);
  }

  // 记录到 MetricsCollector
  METRICS_OBSERVE("tracing_span_duration_ms", static_cast<double>(durationMs));
  if (span.hasError()) {
    METRICS_INC("tracing_errors_total");
  }

  // 记录到日志
  if (span.hasError()) {
    qWarning().noquote() << "[Tracing] Span ended with error: traceId=" << span.traceId
                         << "spanId=" << span.spanId << "name=" << span.name
                         << "error=" << span.error << "durationMs=" << durationMs;
  } else {
    qDebug().noquote() << "[Tracing] Span ended: traceId=" << span.traceId
                       << "spanId=" << span.spanId << "name=" << span.name
                       << "durationMs=" << durationMs;
  }
}

void Tracing::endSpanByName(const QString& name) {
  // 对于按名称结束的简单实现，这里不维护活跃 Span 栈
  // 在复杂场景下可以使用 std::vector 维护多个同名 Span
  Q_UNUSED(name)
  // 简化实现：记录日志
  qDebug() << "[Tracing] endSpanByName called (simplified implementation):" << name;
}

void Tracing::addSpanTag(Span& span, const QString& key, const QVariant& value) {
  if (span.ended) {
    qWarning() << "[Tracing] Cannot add tag to ended span:" << span.spanId;
    return;
  }
  span.tags[key] = value;
}

void Tracing::markSpanError(Span& span, const QString& error) {
  if (span.ended) {
    qWarning() << "[Tracing] Cannot mark error on ended span:" << span.spanId;
    return;
  }
  span.error = error;
  METRICS_INC("tracing_errors_total");
}

// ═══════════════════════════════════════════════════════════════════════════════
// 上下文传播
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap Tracing::addTraceToContext(const QVariantMap& original) const {
  QMutexLocker locker(&m_spansMutex);

  QVariantMap context = original;

  // 添加 TraceID（如果存在）
  if (!m_currentTraceId.isEmpty()) {
    context[TRACE_ID_KEY] = m_currentTraceId;
  }

  // 添加当前 SpanID（可选）
  if (!m_currentSpanId.isEmpty()) {
    context[SPAN_ID_KEY] = m_currentSpanId;
  }

  return context;
}

QString Tracing::extractTraceId(const QVariantMap& context) const {
  if (context.contains(TRACE_ID_KEY)) {
    return context[TRACE_ID_KEY].toString();
  }
  return QString{};
}

QString Tracing::extractSpanId(const QVariantMap& context) const {
  if (context.contains(SPAN_ID_KEY)) {
    return context[SPAN_ID_KEY].toString();
  }
  return QString{};
}

QString Tracing::extractParentSpanId(const QVariantMap& context) const {
  if (context.contains(PARENT_SPAN_ID_KEY)) {
    return context[PARENT_SPAN_ID_KEY].toString();
  }
  return QString{};
}

// ═══════════════════════════════════════════════════════════════════════════════
// 采样控制
// ═══════════════════════════════════════════════════════════════════════════════

void Tracing::setSamplingRate(double rate) {
  // 限制在 [0.0, 1.0] 范围内
  rate = qBound(0.0, rate, 1.0);
  m_samplingRate.store(rate, std::memory_order_relaxed);
  qInfo() << "[Tracing] Sampling rate set to:" << rate;
  emit samplingRateChanged(rate);

  // 记录采样率到指标
  METRICS_OBSERVE("tracing_sampling_rate", rate);
}

double Tracing::samplingRate() const { return m_samplingRate.load(std::memory_order_relaxed); }

bool Tracing::shouldSample() const {
  double rate = m_samplingRate.load(std::memory_order_relaxed);
  if (rate >= 1.0) {
    return true;
  }
  if (rate <= 0.0) {
    return false;
  }

  // 基于当前纳秒时间进行确定性采样
  int64_t timeNs = currentTimeNs();
  double normalized = (timeNs % 10000) / 10000.0;
  return normalized < rate;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 统计信息
// ═══════════════════════════════════════════════════════════════════════════════

Tracing::TracingStats Tracing::getStats() const {
  TracingStats stats;
  stats.totalSpansStarted = m_totalSpansStarted.load(std::memory_order_relaxed);
  stats.totalSpansEnded = m_totalSpansEnded.load(std::memory_order_relaxed);
  stats.totalErrors = m_totalErrors.load(std::memory_order_relaxed);
  stats.activeSpans = static_cast<int>(stats.totalSpansStarted - stats.totalSpansEnded);
  stats.currentSamplingRate = m_samplingRate.load(std::memory_order_relaxed);
  return stats;
}
