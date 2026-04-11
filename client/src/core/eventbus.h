#pragma once
#include <QMutex>
#include <QObject>
#include <QVariantMap>
#include <QWaitCondition>

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

// 编译期 FNV-1a 32bit 字符串哈希，用于生成 TYPE_ID constexpr 常量
constexpr uint32_t client_fnv1a_32(const char* s, uint32_t h = 2166136261u) {
  return (*s == '\0')
             ? h
             : client_fnv1a_32(
                   s + 1, (h ^ static_cast<uint32_t>(static_cast<unsigned char>(*s))) * 16777619u);
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
    HIGH = 1,
    NORMAL = 2,
    LOW = 3,
  };

  virtual ~ClientEvent() = default;
  virtual uint32_t typeId() const = 0;
  virtual Priority priority() const { return Priority::NORMAL; }

  int64_t timestamp = 0;
  uint32_t sequenceNumber = 0;
};

// ─── 类型安全事件定义宏 ────────────────────────────────────────────────────────
#define DEFINE_CLIENT_EVENT(EventName, PriorityLevel, ...)                 \
  class EventName : public ClientEvent {                                   \
   public:                                                                 \
    static constexpr uint32_t TYPE_ID = client_fnv1a_32(#EventName);       \
    static_assert(TYPE_ID != 0, "TYPE_ID must be non-zero");               \
    uint32_t typeId() const override { return TYPE_ID; }                   \
    Priority priority() const override { return Priority::PriorityLevel; } \
    __VA_ARGS__                                                            \
  };

// ─── 预定义核心事件 ───────────────────────────────────────────────────────────

DEFINE_CLIENT_EVENT(EmergencyStopEvent, CRITICAL, QString reason; enum class Source{
    DEADMAN, SAFETY_MONITOR, SAFETY_CHECKER, ERROR_RECOVERY, USER} source{Source::USER};)

DEFINE_CLIENT_EVENT(VehicleControlEvent, HIGH, double steeringAngle = 0.0; double throttle = 0.0;
                    double brake = 0.0; int gear = 0;)

DEFINE_CLIENT_EVENT(TelemetryUpdateEvent, NORMAL, double speed = 0.0; double throttle = 0.0;
                    double brake = 0.0; double steering = 0.0; int gear = 0; double battery = 0.0;)

DEFINE_CLIENT_EVENT(NetworkQualityEvent, NORMAL, double score = 1.0; double rttMs = 0.0;
                    double packetLossRate = 0.0; double bandwidthKbps = 0.0; bool degraded = false;)

DEFINE_CLIENT_EVENT(SessionStateEvent, NORMAL, QString state; QString vin;)

DEFINE_CLIENT_EVENT(VideoFrameDropEvent, NORMAL, uint32_t cameraId = 0; uint32_t count = 0;)

/** 解码器/码流自检：多 slice + FFmpeg 多线程等条状风险；mitigationApplied=已强制单线程并等 IDR */
DEFINE_CLIENT_EVENT(VideoDecodeIntegrityEvent, HIGH, QString stream;
                    QString code; QString detail; bool mitigationApplied = false;
                    QString healthContractLine;)

/**
 * Scene Graph / 纹理路径自检：QImage 与解码指纹不一致、或与 QSGTexture::textureSize 不一致。
 * suspectGpuCompositor=true 时优先查驱动/RHI/合成器（解码 CPU 缓冲可能仍一致）。
 */
DEFINE_CLIENT_EVENT(VideoPresentIntegrityEvent, HIGH, QString stream; QString surfaceId; QString code;
                    QString detail; bool suspectGpuCompositor = false;)

// 旧兼容 ID（供尚未迁移的模块使用）
namespace ClientEventType {
constexpr int EmergencyStop = 1;
constexpr int NetworkQuality = 2;
constexpr int SessionState = 3;
constexpr int ControlCommand = 4;
}  // namespace ClientEventType

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
    auto wrapper = [handler](const ClientEvent& e) { handler(static_cast<const EventType&>(e)); };
    return subscribeRaw(tid, std::move(wrapper));
  }

  template <typename EventType>
  void publish(const EventType& event) {
    if (event.priority() == ClientEvent::Priority::CRITICAL) {
      dispatchImmediate(event.typeId(), event);
    } else {
      enqueue(event.priority(), event.typeId(), std::make_shared<EventType>(event));
    }
  }

  void unsubscribe(SubscriptionHandle handle);

  // ── 旧 QVariantMap API（向后兼容，将逐步废弃）───────────────────────────

  struct LegacyEvent {
    int typeId = 0;
    int priority = 2;  // Normal
    QVariantMap payload;
  };

  int subscribe(int typeId, std::function<void(const LegacyEvent&)> handler);
  void publish(const LegacyEvent& event);

 public slots:
  void processPending();

 signals:
  void eventDispatched(int typeId, QVariantMap payload);

 private:
  SubscriptionHandle subscribeRaw(uint32_t typeId, std::function<void(const ClientEvent&)> handler);
  void dispatchImmediate(uint32_t typeId, const ClientEvent& event);
  void enqueue(ClientEvent::Priority prio, uint32_t typeId, std::shared_ptr<ClientEvent> event);
  void scheduleFlush();

  struct Subscriber {
    SubscriptionHandle handle;
    std::function<void(const ClientEvent&)> handler;
  };

  struct QueuedEvent {
    ClientEvent::Priority priority;
    int seq;
    std::shared_ptr<ClientEvent> event;
    int64_t enqueueTimeNs = 0;  // 入队时间戳，用于计算分发延迟

    bool operator>(const QueuedEvent& o) const {
      if (priority != o.priority)
        return static_cast<uint8_t>(priority) > static_cast<uint8_t>(o.priority);
      return seq > o.seq;
    }
  };

  // ═══════════════════════════════════════════════════════════════
  // 队列监控统计
  // ═══════════════════════════════════════════════════════════════

  /**
   * 队列监控统计结构
   */
  struct QueueStats {
    int queueDepth = 0;                 // 当前队列深度
    int subscriberCount = 0;            // 总订阅者数
    int64_t lastDispatchMs = 0;         // 上次分发时间
    int64_t avgDispatchIntervalMs = 0;  // 平均分发间隔
    int64_t totalDispatched = 0;        // 总分发数
    int64_t totalDropped = 0;           // 总丢弃数
    double fillRatio = 0.0;             // 队列填充率（深度/容量）
    double avgDispatchLatencyMs = 0.0;  // 平均分发延迟
    int64_t maxDispatchLatencyMs = 0;   // 最大分发延迟
    int64_t lastQueueMaxDepth = 0;      // 历史最大深度
    int64_t overflowWarnings = 0;       // 溢出警告次数
  };

  // 队列容量限制（防内存膨胀）
  static constexpr int MAX_QUEUE_DEPTH = 10000;

  // 分发延迟警告阈值（毫秒）
  static constexpr int64_t DISPATCH_LATENCY_WARNING_MS = 100;

  /**
   * 获取队列监控统计
   */
  Q_INVOKABLE QueueStats getQueueStats() const;

  /**
   * 重置队列统计
   */
  Q_INVOKABLE void resetQueueStats();

  /**
   * 获取队列填充率
   */
  Q_INVOKABLE double getQueueFillRatio() const;

  /**
   * 获取最大分发延迟（毫秒）
   */
  Q_INVOKABLE int64_t getMaxDispatchLatencyMs() const;

 signals:
  /**
   * 队列溢出警告信号（队列深度接近上限时触发）
   */
  void queueOverflowWarning(int currentDepth, int maxDepth);

  /**
   * 分发延迟警告信号（延迟超过阈值时��发）
   */
  void dispatchLatencyWarning(int64_t latencyMs);

  /**
   * 事件丢弃信号
   */
  void eventDropped(uint32_t typeId, int totalDropped);

 private:
  mutable QMutex m_subMutex;
  std::unordered_map<uint32_t, std::vector<Subscriber>> m_subscribers;
  std::atomic<SubscriptionHandle> m_nextHandle{1};

  mutable QMutex m_queueMutex;
  // 使用 vector + push_heap/pop_heap 实现优先级队列
  std::vector<QueuedEvent> m_queue;
  std::atomic<int> m_seqCounter{0};
  std::atomic<bool> m_flushScheduled{false};

  // ── 队列监控统计 ──────────────────────────────────────────────
  mutable QMutex m_statsMutex;
  int64_t m_lastDispatchTimeNs = 0;
  std::atomic<int64_t> m_totalDispatched{0};
  std::atomic<int64_t> m_totalDropped{0};
  int64_t m_dispatchIntervalSumNs = 0;
  int64_t m_dispatchIntervalCount = 0;
  int64_t m_totalDispatchLatencyNs = 0;
  int64_t m_maxDispatchLatencyNs = 0;
  std::atomic<int64_t> m_lastQueueMaxDepth{0};
  std::atomic<int64_t> m_overflowWarnings{0};
};
