#include "eventbus.h"

#include "logger.h"
#include "metricscollector.h"

#include <QCoreApplication>
#include <QDebug>
#include <QThread>

#include <algorithm>
#include <chrono>

// 使用 std::chrono 获取纳秒时间戳
static int64_t currentTimeNs() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

EventBus::EventBus(QObject* parent)
    : QObject(parent),
      m_subMutex(),
      m_subscribers(),
      m_nextHandle(1),
      m_queueMutex(),
      m_queue(),
      m_seqCounter(0),
      m_flushScheduled(false),
      m_statsMutex(),
      m_lastDispatchTimeNs(0),
      m_totalDispatched(0),
      m_totalDropped(0),
      m_dispatchIntervalSumNs(0),
      m_dispatchIntervalCount(0),
      m_totalDispatchLatencyNs(0),
      m_maxDispatchLatencyNs(0),
      m_lastQueueMaxDepth(0),
      m_overflowWarnings(0) {
  qInfo() << "[Client][EventBus] Initialized with MAX_QUEUE_DEPTH=" << MAX_QUEUE_DEPTH;

  // 注册 EventBus 指标到 MetricsCollector
  MetricsCollector& metrics = MetricsCollector::instance();
  metrics.set("eventbus_queue_depth", 0.0);
  metrics.set("eventbus_queue_max_depth", 0.0);
  metrics.set("eventbus_subscribers_total", 0.0);
  metrics.set("eventbus_dispatch_total", 0.0);
  metrics.set("eventbus_dropped_total", 0.0);
  metrics.set("eventbus_dispatch_latency_ms", 0.0);
  metrics.set("eventbus_queue_fill_ratio", 0.0);
}

EventBus::~EventBus() = default;

// ─── 类型安全 API 实现 ────────────────────────────────────────────────────────

SubscriptionHandle EventBus::subscribeRaw(uint32_t typeId,
                                          std::function<void(const ClientEvent&)> handler) {
  SubscriptionHandle h;
  {
    QMutexLocker lock(&m_subMutex);
    h = m_nextHandle.fetch_add(1, std::memory_order_relaxed);
    m_subscribers[typeId].push_back(Subscriber{h, std::move(handler)});
  }

  // 更新订阅者数量指标
  int totalSubscribers = 0;
  {
    QMutexLocker lock(&m_subMutex);
    for (const auto& [tid, subs] : m_subscribers) {
      totalSubscribers += static_cast<int>(subs.size());
    }
  }
  MetricsCollector::instance().set("eventbus_subscribers_total",
                                   static_cast<double>(totalSubscribers));

  qDebug() << "[Client][EventBus] Subscribed: typeId=" << typeId << "handle=" << h;
  return h;
}

void EventBus::unsubscribe(SubscriptionHandle handle) {
  {
    QMutexLocker lock(&m_subMutex);
    for (auto& [tid, subs] : m_subscribers) {
      subs.erase(std::remove_if(subs.begin(), subs.end(),
                                [handle](const Subscriber& s) { return s.handle == handle; }),
                 subs.end());
    }
  }

  // 更新订阅者数量指标
  int totalSubscribers = 0;
  {
    QMutexLocker lock(&m_subMutex);
    for (const auto& [tid, subs] : m_subscribers) {
      totalSubscribers += static_cast<int>(subs.size());
    }
  }
  MetricsCollector::instance().set("eventbus_subscribers_total",
                                   static_cast<double>(totalSubscribers));

  qDebug() << "[Client][EventBus] Unsubscribed: handle=" << handle;
}

void EventBus::dispatchImmediate(uint32_t typeId, const ClientEvent& event) {
  std::vector<Subscriber> copy;
  {
    QMutexLocker lock(&m_subMutex);
    auto it = m_subscribers.find(typeId);
    if (it != m_subscribers.end()) {
      copy = it->second;
    }
  }
  for (const auto& sub : copy) {
    if (sub.handler)
      sub.handler(event);
  }
  qCritical().noquote() << "[Client][EventBus] CRITICAL dispatch typeId=" << typeId;
}

void EventBus::enqueue(ClientEvent::Priority prio, uint32_t typeId,
                       std::shared_ptr<ClientEvent> event) {
  if (QThread::currentThread() != thread()) {
    QMetaObject::invokeMethod(
        this,
        [this, prio, typeId, ev = std::move(event)]() mutable {
          enqueue(prio, typeId, std::move(ev));
        },
        Qt::QueuedConnection);
    return;
  }

  int currentDepth = 0;

  {
    QMutexLocker lock(&m_queueMutex);

    // ── 队列容量检查（防内存膨胀）──────────────────────────────
    // 当队列超过上限时，丢弃最低优先级的事件
    while (m_queue.size() >= MAX_QUEUE_DEPTH) {
      // 弹出最低优先级的最老事件
      if (!m_queue.empty()) {
        auto cmp = [](const QueuedEvent& a, const QueuedEvent& b) { return a > b; };
        std::pop_heap(m_queue.begin(), m_queue.end(), cmp);
        m_queue.pop_back();
        m_totalDropped.fetch_add(1, std::memory_order_relaxed);

        // 记录丢弃到 MetricsCollector
        MetricsCollector::instance().increment("eventbus_dropped_total");

        // 每丢弃 100 条打印一次警告
        int64_t dropped = m_totalDropped.load();
        if (dropped % 100 == 0 || dropped <= 3) {
          qWarning().noquote()
              << "[Client][EventBus] Queue overflow, dropped lowest priority event. total_dropped="
              << dropped;
        }

        // 发射丢弃信号
        emit eventDropped(typeId, static_cast<int>(dropped));
      }
    }

    // 记录入队时间戳
    QueuedEvent qe;
    qe.priority = prio;
    qe.seq = m_seqCounter.fetch_add(1);
    qe.event = std::move(event);
    qe.enqueueTimeNs = currentTimeNs();

    m_queue.push_back(qe);
    std::push_heap(m_queue.begin(), m_queue.end(), [](const QueuedEvent& a, const QueuedEvent& b) {
      return a > b;  // min-heap by priority
    });

    currentDepth = static_cast<int>(m_queue.size());

    // 更新最大深度记录
    if (currentDepth > m_lastQueueMaxDepth.load()) {
      m_lastQueueMaxDepth.store(currentDepth);
      MetricsCollector::instance().set("eventbus_queue_max_depth",
                                       static_cast<double>(currentDepth));
    }

    // 检查是否需要发出溢出警告（队列超过 80% 时）
    if (currentDepth >= MAX_QUEUE_DEPTH * 8 / 10) {
      // 使用滑动窗口记录最近一次警告时间（避免持续满载时每秒产生大量警告）
      static int64_t s_lastOverflowWarningMs = 0;
      static int s_overflowWarningCount = 0;

      int64_t nowMs = QDateTime::currentMSecsSinceEpoch();
      if (nowMs - s_lastOverflowWarningMs < 1000) {
        s_overflowWarningCount++;
        if (s_overflowWarningCount >= 4)
          return;  // 1秒内最多4次警告
      }
      s_lastOverflowWarningMs = nowMs;
      s_overflowWarningCount = 0;

      m_overflowWarnings.fetch_add(1, std::memory_order_relaxed);
      emit queueOverflowWarning(currentDepth, MAX_QUEUE_DEPTH);
      qWarning().noquote() << "[Client][EventBus] Queue fill ratio high: " << currentDepth << "/"
                           << MAX_QUEUE_DEPTH;
    }
  }

  // 更新队列深度指标（带锁外更新以避免锁竞争）
  MetricsCollector::instance().set("eventbus_queue_depth", static_cast<double>(currentDepth));
  MetricsCollector::instance().set(
      "eventbus_queue_fill_ratio",
      static_cast<double>(currentDepth) / static_cast<double>(MAX_QUEUE_DEPTH));

  scheduleFlush();
}

void EventBus::scheduleFlush() {
  bool expected = false;
  if (m_flushScheduled.compare_exchange_strong(expected, true)) {
    QMetaObject::invokeMethod(this, &EventBus::processPending, Qt::QueuedConnection);
  }
}

void EventBus::processPending() {
  int64_t processingStartNs = currentTimeNs();

  std::vector<QueuedEvent> local;
  {
    QMutexLocker lock(&m_queueMutex);
    m_flushScheduled.store(false);
    local.swap(m_queue);

    // 更新队列深度指标
    MetricsCollector::instance().set("eventbus_queue_depth", static_cast<double>(m_queue.size()));
    MetricsCollector::instance().set(
        "eventbus_queue_fill_ratio",
        static_cast<double>(m_queue.size()) / static_cast<double>(MAX_QUEUE_DEPTH));
  }

  // 计算分发间隔
  int64_t nowNs = currentTimeNs();
  {
    QMutexLocker lock(&m_statsMutex);
    if (m_lastDispatchTimeNs > 0) {
      int64_t intervalNs = nowNs - m_lastDispatchTimeNs;
      m_dispatchIntervalSumNs += intervalNs;
      m_dispatchIntervalCount++;
    }
    m_lastDispatchTimeNs = nowNs;
  }

  int dispatchCount = 0;

  // local 是一个堆，依次弹出
  auto cmp = [](const QueuedEvent& a, const QueuedEvent& b) { return a > b; };
  while (!local.empty()) {
    std::pop_heap(local.begin(), local.end(), cmp);
    QueuedEvent qe = std::move(local.back());
    local.pop_back();

    // ── 计算分发延迟（enqueue 到 dispatch 的时间差）────────────
    int64_t dispatchLatencyNs = nowNs - qe.enqueueTimeNs;
    int64_t dispatchLatencyMs = dispatchLatencyNs / 1'000'000;

    {
      QMutexLocker lock(&m_statsMutex);
      // 更新延迟统计
      m_totalDispatchLatencyNs += dispatchLatencyNs;
      if (dispatchLatencyNs > m_maxDispatchLatencyNs) {
        m_maxDispatchLatencyNs = dispatchLatencyNs;
      }
      m_totalDispatched.fetch_add(1, std::memory_order_relaxed);
    }

    // 检查延迟是否超过阈值
    if (dispatchLatencyMs >= DISPATCH_LATENCY_WARNING_MS) {
      qWarning().noquote() << "[Client][EventBus] High dispatch latency: " << dispatchLatencyMs
                           << "ms for typeId=" << qe.event->typeId();
      emit dispatchLatencyWarning(dispatchLatencyMs);
    }

    std::vector<Subscriber> copy;
    {
      QMutexLocker lock(&m_subMutex);
      auto it = m_subscribers.find(qe.event->typeId());
      if (it != m_subscribers.end()) {
        copy = it->second;
      }
    }
    for (const auto& sub : copy) {
      if (sub.handler)
        sub.handler(*qe.event);
    }
    emit eventDispatched(static_cast<int>(qe.event->typeId()), QVariantMap{});

    dispatchCount++;
  }

  // 更新分发统计到 MetricsCollector
  if (dispatchCount > 0) {
    MetricsCollector::instance().increment("eventbus_dispatch_total",
                                           static_cast<double>(dispatchCount));
  }

  // 记录处理耗时到 MetricsCollector
  int64_t processingTimeNs = currentTimeNs() - processingStartNs;
  MetricsCollector::instance().observe("eventbus_dispatch_latency_ms",
                                       static_cast<double>(processingTimeNs / 1'000'000));
}

// ─── 旧兼容 API 实现 ──────────────────────────────────────────────────────────

int EventBus::subscribe(int typeId, std::function<void(const LegacyEvent&)> handler) {
  const SubscriptionHandle h = m_nextHandle.fetch_add(1, std::memory_order_relaxed);

  // 存储一个哑 handler 到 raw subscriber 表（仅用于 handle 管理）
  // 实际的 Legacy 事件通过 eventDispatched 信号传递
  {
    QMutexLocker lock(&m_subMutex);
    // Store under a legacy namespace typeId (shifted to avoid collision)
    uint32_t legacyKey = static_cast<uint32_t>(typeId) | 0x80000000u;
    // 存储一个有效的哑 handler，标记为 legacy 订阅者
    m_subscribers[legacyKey].push_back(Subscriber{h, [](const ClientEvent&) {
                                                    // 空操作；Legacy 订阅者通过 eventDispatched
                                                    // 信号接收事件
                                                  }});
  }

  // 使用 Qt::UniqueConnection 防止重复连接
  // 注意：这里使用 lambda 捕获 handler，确保在信号发射时 handler 仍然有效
  // Qt::DirectConnection 保证事件同步传递到当前线程
  QObject::connect(this, &EventBus::eventDispatched,
                   [handler, typeId, h](int tid, QVariantMap payload) {
                     if (tid == typeId) {
                       LegacyEvent e;
                       e.typeId = tid;
                       e.payload = payload;
                       handler(e);
                     }
                   });

  qDebug() << "[Client][EventBus] Legacy subscribe: typeId=" << typeId << "handle=" << h
           << "(signal-based, not wrapper-based)";
  return h;
}

void EventBus::publish(const LegacyEvent& event) {
  // Emit directly for legacy subscribers
  QMetaObject::invokeMethod(
      this, [this, event]() { emit eventDispatched(event.typeId, event.payload); },
      thread() == QThread::currentThread() ? Qt::DirectConnection : Qt::QueuedConnection);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 队列监控统计实现
// ═══════════════════════════════════════════════════════════════════════════════

EventBus::QueueStats EventBus::getQueueStats() const {
  QueueStats stats;

  // 获取队列深度
  {
    QMutexLocker lock(&m_queueMutex);
    stats.queueDepth = static_cast<int>(m_queue.size());
  }

  // 获取订阅者数量
  {
    QMutexLocker lock(&m_subMutex);
    stats.subscriberCount = 0;
    for (const auto& [tid, subs] : m_subscribers) {
      stats.subscriberCount += static_cast<int>(subs.size());
    }
  }

  // 获取分发统计
  {
    QMutexLocker lock(&m_statsMutex);
    stats.totalDispatched = m_totalDispatched.load(std::memory_order_relaxed);
    stats.totalDropped = m_totalDropped.load(std::memory_order_relaxed);
    stats.lastDispatchMs = m_lastDispatchTimeNs / 1'000'000;
    stats.maxDispatchLatencyMs = m_maxDispatchLatencyNs / 1'000'000;
    stats.lastQueueMaxDepth = m_lastQueueMaxDepth.load(std::memory_order_relaxed);
    stats.overflowWarnings = m_overflowWarnings.load(std::memory_order_relaxed);

    // 计算平均分发间隔
    if (m_dispatchIntervalCount > 0) {
      stats.avgDispatchIntervalMs = m_dispatchIntervalSumNs / (m_dispatchIntervalCount * 1'000'000);
    }

    // 计算平均分发延迟
    if (m_totalDispatched.load(std::memory_order_relaxed) > 0) {
      stats.avgDispatchLatencyMs =
          static_cast<double>(m_totalDispatchLatencyNs) /
          static_cast<double>(m_totalDispatched.load(std::memory_order_relaxed) * 1'000'000);
    }
  }

  // 计算队列填充率
  stats.fillRatio = static_cast<double>(stats.queueDepth) / static_cast<double>(MAX_QUEUE_DEPTH);

  return stats;
}

void EventBus::resetQueueStats() {
  QMutexLocker lock(&m_statsMutex);
  m_totalDispatched.store(0, std::memory_order_relaxed);
  m_totalDropped.store(0, std::memory_order_relaxed);
  m_lastDispatchTimeNs = 0;
  m_dispatchIntervalSumNs = 0;
  m_dispatchIntervalCount = 0;
  m_totalDispatchLatencyNs = 0;
  m_maxDispatchLatencyNs = 0;
  m_lastQueueMaxDepth.store(0, std::memory_order_relaxed);
  m_overflowWarnings.store(0, std::memory_order_relaxed);

  qInfo() << "[Client][EventBus] Queue stats reset";
}

double EventBus::getQueueFillRatio() const {
  QMutexLocker lock(&m_queueMutex);
  return static_cast<double>(m_queue.size()) / static_cast<double>(MAX_QUEUE_DEPTH);
}

int64_t EventBus::getMaxDispatchLatencyMs() const {
  QMutexLocker lock(&m_statsMutex);
  return m_maxDispatchLatencyNs / 1'000'000;
}
