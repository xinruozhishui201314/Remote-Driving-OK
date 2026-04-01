#include "eventbus.h"
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <algorithm>

EventBus::EventBus(QObject* parent)
    : QObject(parent)
{}

EventBus::~EventBus() = default;

// ─── 类型安全 API 实现 ────────────────────────────────────────────────────────

SubscriptionHandle EventBus::subscribeRaw(uint32_t typeId,
                                           std::function<void(const ClientEvent&)> handler)
{
    QMutexLocker lock(&m_subMutex);
    const SubscriptionHandle h = m_nextHandle.fetch_add(1, std::memory_order_relaxed);
    m_subscribers[typeId].push_back(Subscriber{h, std::move(handler)});
    return h;
}

void EventBus::unsubscribe(SubscriptionHandle handle)
{
    QMutexLocker lock(&m_subMutex);
    for (auto& [tid, subs] : m_subscribers) {
        subs.erase(std::remove_if(subs.begin(), subs.end(),
                                   [handle](const Subscriber& s) {
                                       return s.handle == handle;
                                   }),
                   subs.end());
    }
}

void EventBus::dispatchImmediate(uint32_t typeId, const ClientEvent& event)
{
    std::vector<Subscriber> copy;
    {
        QMutexLocker lock(&m_subMutex);
        auto it = m_subscribers.find(typeId);
        if (it != m_subscribers.end()) {
            copy = it->second;
        }
    }
    for (const auto& sub : copy) {
        if (sub.handler) sub.handler(event);
    }
    qCritical().noquote() << "[Client][EventBus] CRITICAL dispatch typeId=" << typeId;
}

void EventBus::enqueue(ClientEvent::Priority prio, uint32_t typeId,
                        std::shared_ptr<ClientEvent> event)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, prio, typeId, ev = std::move(event)]() mutable {
            enqueue(prio, typeId, std::move(ev));
        }, Qt::QueuedConnection);
        return;
    }

    {
        QMutexLocker lock(&m_queueMutex);
        m_queue.push_back(QueuedEvent{prio, m_seqCounter.fetch_add(1), std::move(event)});
        std::push_heap(m_queue.begin(), m_queue.end(),
                       [](const QueuedEvent& a, const QueuedEvent& b) {
                           return a > b; // min-heap by priority
                       });
    }
    scheduleFlush();
}

void EventBus::scheduleFlush()
{
    bool expected = false;
    if (m_flushScheduled.compare_exchange_strong(expected, true)) {
        QMetaObject::invokeMethod(this, &EventBus::processPending, Qt::QueuedConnection);
    }
}

void EventBus::processPending()
{
    std::vector<QueuedEvent> local;
    {
        QMutexLocker lock(&m_queueMutex);
        m_flushScheduled.store(false);
        local.swap(m_queue);
    }

    // local 是一个堆，依次弹出
    auto cmp = [](const QueuedEvent& a, const QueuedEvent& b) { return a > b; };
    while (!local.empty()) {
        std::pop_heap(local.begin(), local.end(), cmp);
        QueuedEvent qe = std::move(local.back());
        local.pop_back();

        std::vector<Subscriber> copy;
        {
            QMutexLocker lock(&m_subMutex);
            auto it = m_subscribers.find(qe.event->typeId());
            if (it != m_subscribers.end()) {
                copy = it->second;
            }
        }
        for (const auto& sub : copy) {
            if (sub.handler) sub.handler(*qe.event);
        }
        emit eventDispatched(static_cast<int>(qe.event->typeId()), QVariantMap{});
    }
}

// ─── 旧兼容 API 实现 ──────────────────────────────────────────────────────────

int EventBus::subscribe(int typeId, std::function<void(const LegacyEvent&)> handler)
{
    const SubscriptionHandle h = m_nextHandle.fetch_add(1, std::memory_order_relaxed);
    // 用 typeId 的哈希包装为 raw subscriber
    auto wrapper = [handler, typeId](const ClientEvent&) {
        // Legacy subscribers receive dispatched signal via eventDispatched
        Q_UNUSED(handler)
        Q_UNUSED(typeId)
    };
    {
        QMutexLocker lock(&m_subMutex);
        // Store under a legacy namespace typeId (shifted to avoid collision)
        uint32_t legacyKey = static_cast<uint32_t>(typeId) | 0x80000000u;
        m_subscribers[legacyKey].push_back(Subscriber{h, std::move(wrapper)});
    }
    // Also connect to eventDispatched signal via lambda stored separately
    connect(this, &EventBus::eventDispatched, this,
            [handler, typeId, h](int tid, QVariantMap payload) {
                if (tid == typeId) {
                    LegacyEvent e;
                    e.typeId = tid;
                    e.payload = payload;
                    handler(e);
                }
            },
            Qt::DirectConnection);
    return h;
}

void EventBus::publish(const LegacyEvent& event)
{
    // Emit directly for legacy subscribers
    QMetaObject::invokeMethod(this, [this, event]() {
        emit eventDispatched(event.typeId, event.payload);
    }, thread() == QThread::currentThread() ? Qt::DirectConnection : Qt::QueuedConnection);
}
