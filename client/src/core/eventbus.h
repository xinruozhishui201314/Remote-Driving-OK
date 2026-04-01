#pragma once
#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QVariantMap>
#include <functional>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <memory>

// 编译期 FNV-1a 32bit 字符串哈希，用于生成 TYPE_ID constexpr 常量
constexpr uint32_t client_fnv1a_32(const char* s, uint32_t h = 2166136261u) {
    return (*s == '\0') ? h
                        : client_fnv1a_32(s + 1,
                              (h ^ static_cast<uint32_t>(static_cast<unsigned char>(*s)))
                              * 16777619u);
}

/**
 * 高性能类型安全事件总线（《客户端架构设计》§3.2.1）。
 *
 * 使用方式：
 *   // 定义事件（优先级宏：CRITICAL/HIGH/NORMAL/LOW）
 *   DEFINE_CLIENT_EVENT(EmergencyStopEvent, CRITICAL,
 *       std::string reason;
 *   )
 *
 *   // 订阅
 *   auto handle = EventBus::instance().subscribe<EmergencyStopEvent>(
 *       [](const EmergencyStopEvent& e) { ... });
 *
 *   // 发布
 *   EmergencyStopEvent evt;
 *   evt.reason = "deadman";
 *   EventBus::instance().publish(evt);
 *
 * CRITICAL 事件同步直接调用；其余事件进入优先级队列，
 * 由主线程（或 processPending）按序分发。
 */

using SubscriptionHandle = int;

// ─── 事件基类 ─────────────────────────────────────────────────────────────────
class ClientEvent {
public:
    enum class Priority : uint8_t {
        CRITICAL = 0,
        HIGH     = 1,
        NORMAL   = 2,
        LOW      = 3,
    };

    virtual ~ClientEvent() = default;
    virtual uint32_t typeId() const = 0;
    virtual Priority priority() const { return Priority::NORMAL; }

    int64_t timestamp = 0;
    uint32_t sequenceNumber = 0;
};

// ─── 类型安全事件定义宏 ────────────────────────────────────────────────────────
#define DEFINE_CLIENT_EVENT(EventName, PriorityLevel, ...)             \
    class EventName : public ClientEvent {                              \
    public:                                                             \
        static constexpr uint32_t TYPE_ID = client_fnv1a_32(#EventName); \
        static_assert(TYPE_ID != 0, "TYPE_ID must be non-zero");       \
        uint32_t typeId() const override { return TYPE_ID; }           \
        Priority priority() const override {                           \
            return Priority::PriorityLevel;                            \
        }                                                               \
        __VA_ARGS__                                                     \
    };

// ─── 预定义核心事件 ───────────────────────────────────────────────────────────

DEFINE_CLIENT_EVENT(EmergencyStopEvent, CRITICAL,
    QString reason;
    enum class Source { DEADMAN, SAFETY_MONITOR, SAFETY_CHECKER, ERROR_RECOVERY, USER } source{Source::USER};
)

DEFINE_CLIENT_EVENT(VehicleControlEvent, HIGH,
    double steeringAngle = 0.0;
    double throttle = 0.0;
    double brake = 0.0;
    int gear = 0;
)

DEFINE_CLIENT_EVENT(TelemetryUpdateEvent, NORMAL,
    double speed = 0.0;
    double throttle = 0.0;
    double brake = 0.0;
    double steering = 0.0;
    int gear = 0;
    double battery = 0.0;
)

DEFINE_CLIENT_EVENT(NetworkQualityEvent, NORMAL,
    double score = 1.0;
    double rttMs = 0.0;
    double packetLossRate = 0.0;
    double bandwidthKbps = 0.0;
    bool degraded = false;
)

DEFINE_CLIENT_EVENT(SessionStateEvent, NORMAL,
    QString state;
    QString vin;
)

DEFINE_CLIENT_EVENT(VideoFrameDropEvent, NORMAL,
    uint32_t cameraId = 0;
    uint32_t count = 0;
)

// 旧兼容 ID（供尚未迁移的模块使用）
namespace ClientEventType {
constexpr int EmergencyStop  = 1;
constexpr int NetworkQuality = 2;
constexpr int SessionState   = 3;
constexpr int ControlCommand = 4;
} // namespace ClientEventType

// ─── EventBus 主类 ────────────────────────────────────────────────────────────
class EventBus : public QObject {
    Q_OBJECT

public:
    static EventBus& instance() {
        static EventBus bus;
        return bus;
    }

    explicit EventBus(QObject* parent = nullptr);
    ~EventBus() override;

    // ── 类型安全 API（推荐）──────────────────────────────────────────────────

    template <typename EventType>
    SubscriptionHandle subscribe(std::function<void(const EventType&)> handler) {
        const uint32_t tid = EventType::TYPE_ID;
        auto wrapper = [handler](const ClientEvent& e) {
            handler(static_cast<const EventType&>(e));
        };
        return subscribeRaw(tid, std::move(wrapper));
    }

    template <typename EventType>
    void publish(const EventType& event) {
        if (event.priority() == ClientEvent::Priority::CRITICAL) {
            dispatchImmediate(event.typeId(), event);
        } else {
            enqueue(event.priority(), event.typeId(),
                    std::make_shared<EventType>(event));
        }
    }

    void unsubscribe(SubscriptionHandle handle);

    // ── 旧 QVariantMap API（向后兼容，将逐步废弃）───────────────────────────

    struct LegacyEvent {
        int typeId = 0;
        int priority = 2; // Normal
        QVariantMap payload;
    };

    int subscribe(int typeId, std::function<void(const LegacyEvent&)> handler);
    void publish(const LegacyEvent& event);

public slots:
    void processPending();

signals:
    void eventDispatched(int typeId, QVariantMap payload);

private:
    SubscriptionHandle subscribeRaw(uint32_t typeId,
                                    std::function<void(const ClientEvent&)> handler);
    void dispatchImmediate(uint32_t typeId, const ClientEvent& event);
    void enqueue(ClientEvent::Priority prio, uint32_t typeId,
                 std::shared_ptr<ClientEvent> event);
    void scheduleFlush();

    struct Subscriber {
        SubscriptionHandle handle;
        std::function<void(const ClientEvent&)> handler;
    };

    struct QueuedEvent {
        ClientEvent::Priority priority;
        int seq;
        std::shared_ptr<ClientEvent> event;

        bool operator>(const QueuedEvent& o) const {
            if (priority != o.priority)
                return static_cast<uint8_t>(priority) > static_cast<uint8_t>(o.priority);
            return seq > o.seq;
        }
    };

    QMutex m_subMutex;
    std::unordered_map<uint32_t, std::vector<Subscriber>> m_subscribers;
    std::atomic<SubscriptionHandle> m_nextHandle{1};

    QMutex m_queueMutex;
    // 使用 vector + push_heap/pop_heap 实现优先级队列
    std::vector<QueuedEvent> m_queue;
    std::atomic<int> m_seqCounter{0};
    std::atomic<bool> m_flushScheduled{false};
};
