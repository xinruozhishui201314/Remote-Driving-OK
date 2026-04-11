#include "ratelimiter.h"

#include "core/metricscollector.h"
#include "core/tracing.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

RateLimiter::RateLimiter(double rate, int burst, QObject* parent)
    : QObject(parent),
      m_rate(rate),
      m_burst(burst),
      m_tokens(static_cast<double>(burst)),
      m_lastRefill(std::chrono::steady_clock::now()),
      m_refillTimer(new QTimer(this)) {
  // 初始化时令牌数为桶容量
  m_tokens = static_cast<double>(burst);

  // 设置定时器，每 100ms 补充一次令牌
  // 使用 Qt::PreciseTimer 确保精确计时
  m_refillTimer->setTimerType(Qt::PreciseTimer);
  m_refillTimer->setInterval(100);  // 100ms 补充一次
  connect(m_refillTimer, &QTimer::timeout, this, &RateLimiter::refillTokens);

  // 启动定时器
  m_refillTimer->start();

  qDebug().noquote() << "[RateLimiter] 初始化: rate=" << rate << " tokens/s, burst=" << burst;
}

RateLimiter::~RateLimiter() {
  m_refillTimer->stop();
  qDebug().noquote() << "[RateLimiter] 销毁: rejectedCount=" << m_rejectedCount.load();
}

void RateLimiter::refillTokens() {
  // 补充令牌：按时间流逝的比例补充
  auto now = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed = now - m_lastRefill;
  double seconds = elapsed.count();

  if (seconds > 0 && m_rate > 0) {
    double newTokens = seconds * m_rate;
    double oldTokens = m_tokens;

    {
      QMutexLocker locker(&m_mutex);
      m_tokens = qMin(m_tokens + newTokens, static_cast<double>(m_burst));
    }

    m_lastRefill = now;

    // 记录补充的令牌数（用于监控）
    METRICS_SET("rate_limiter_available_tokens", m_tokens);
  }
}

double RateLimiter::consumeTokens(double count) {
  QMutexLocker locker(&m_mutex);

  if (m_tokens >= count) {
    m_tokens -= count;
    return 0.0;  // 返回剩余不足的令牌数（0 表示成功）
  } else {
    double deficit = count - m_tokens;
    m_tokens = 0.0;
    return deficit;  // 返回不足的令牌数
  }
}

bool RateLimiter::tryAcquire() {
  // 尝试获取令牌（非阻塞）
  double deficit = consumeTokens(1.0);

  if (deficit > 0.0) {
    // 令牌不足，记录限流事件
    m_rejectedCount.fetch_add(1);
    METRICS_INC("rate_limiter_rejected_total");

    // 检测状态变化
    bool wasLimited = m_limited.load();
    m_limited.store(true);

    if (!wasLimited) {
      qWarning().noquote() << "[RateLimiter] 触发限流: availableTokens=" << availableTokens();
      emit rateLimitExceeded();
    }

    return false;
  }

  // 成功获取令牌
  METRICS_INC("rate_limiter_allowed_total");

  // 检测恢复：曾处于限流状态且本次成功拿到令牌即视为恢复（burst=1 时取走后桶空，不能再要求
  // availableTokens>0）
  bool wasLimited = m_limited.load();
  if (wasLimited) {
    m_limited.store(false);
    qDebug().noquote() << "[RateLimiter] 解除限流: availableTokens=" << availableTokens();
    emit rateLimitReleased();
  }

  return true;
}

bool RateLimiter::acquire(int timeoutMs) {
  if (timeoutMs == 0) {
    // 零超时，等同于 tryAcquire
    return tryAcquire();
  }

  QElapsedTimer timer;
  timer.start();

  // 首先尝试一次非阻塞获取
  if (tryAcquire()) {
    return true;
  }

  // 循环等待直到成功或超时
  const int checkIntervalMs = 10;  // 每 10ms 检查一次
  int waitedMs = 0;

  while (timeoutMs < 0 || waitedMs < timeoutMs) {
    // 等待一段时间
    int remaining = (timeoutMs < 0) ? checkIntervalMs : qMin(checkIntervalMs, timeoutMs - waitedMs);
    QThread::msleep(remaining);
    if (QCoreApplication::instance()) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }

    // 再次尝试获取
    if (tryAcquire()) {
      return true;
    }

    waitedMs = timer.elapsed();

    // 检查是否超时
    if (timeoutMs >= 0 && waitedMs >= timeoutMs) {
      qWarning().noquote() << "[RateLimiter] 获取令牌超时: timeoutMs=" << timeoutMs;
      m_rejectedCount.fetch_add(1);
      return false;
    }
  }

  return false;
}

double RateLimiter::availableTokens() const {
  QMutexLocker locker(&m_mutex);
  return m_tokens;
}

qint64 RateLimiter::rejectedCount() const { return m_rejectedCount.load(); }

void RateLimiter::resetStats() {
  m_rejectedCount.store(0);
  METRICS_SET("rate_limiter_rejected_total", 0.0);
  METRICS_SET("rate_limiter_allowed_total", 0.0);
  qDebug() << "[RateLimiter] 统计已重置";
}

void RateLimiter::setRate(double rate) {
  if (rate <= 0) {
    qWarning() << "[RateLimiter] 无效的速率:" << rate << "（必须 > 0）";
    return;
  }

  {
    QMutexLocker locker(&m_mutex);
    m_rate = rate;
  }

  qDebug().noquote() << "[RateLimiter] 速率调整为:" << rate << " tokens/s";
  METRICS_SET("rate_limiter_rate", rate);
}

void RateLimiter::setBurst(int burst) {
  if (burst <= 0) {
    qWarning() << "[RateLimiter] 无效的突发容量:" << burst << "（必须 > 0）";
    return;
  }

  {
    QMutexLocker locker(&m_mutex);
    m_burst = burst;
    // 当前令牌数不能超过新容量
    if (m_tokens > burst) {
      m_tokens = static_cast<double>(burst);
    }
  }

  qDebug().noquote() << "[RateLimiter] 突发容量调整为:" << burst;
  METRICS_SET("rate_limiter_burst", static_cast<double>(burst));
}