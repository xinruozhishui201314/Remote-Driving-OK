#pragma once
#include <QMutex>
#include <QString>

#include <atomic>
#include <bitset>
#include <cstdint>

/**
 * 滑动窗口反重放防护（《客户端架构设计》§3 安全要求）。
 *
 * 双重保护：
 *   1) 序列号滑动窗口（1024 slot bitset），O(1) 查找，拒绝回放旧包和重复包
 *   2) 时间戳漂移检查，拒绝超出接受窗口的过期/超前消息
 *   3) 溢出保护：检测序列号环绕并自动恢复
 *
 * 线程安全：内部互斥锁，可跨线程调用。
 */
class AntiReplayGuard {
 public:
  static constexpr int WINDOW_SIZE = 1024;                  // 滑动窗口槽数（2的幂）
  static constexpr int64_t MAX_TIMESTAMP_DRIFT_MS = 10000;  // 最大时间戳漂移 ±10s
  static constexpr int64_t MIN_TIMESTAMP_DRIFT_MS = -5000;  // 允许稍微超前（时钟误差）

  // 溢出阈值：序列号差异超过此值触发溢出重置
  static constexpr uint32_t OVERFLOW_THRESHOLD = 0x80000000;  // 2^31 = 0x80000000

  AntiReplayGuard();
  ~AntiReplayGuard() = default;

  /**
   * 检查并记录收到的消息。
   *
   * @param seq        消息序列号
   * @param timestampMs 消息时间戳（wall-clock ms since epoch，来自消息本身）
   * @param localTimeMs 本地当前时间（wall-clock ms，用于漂移检查）
   * @param reason     失败原因（out）
   * @return true = 合法（首次见到的包，时间戳在窗口内）
   *         false = 拒绝（重放、过期或超前）
   */
  bool checkAndRecord(uint32_t seq, int64_t timestampMs, int64_t localTimeMs,
                      QString* reason = nullptr);

  /**
   * 重置状态（切换会话时调用）。
   */
  void reset();

  /**
   * 仅做时间戳漂移检查，不更新状态（用于出站消息的自检）。
   */
  static bool checkTimestampDrift(int64_t msgTimestampMs, int64_t localTimeMs,
                                  QString* reason = nullptr);

  // ═══════════════════════════════════════════════════════════════════════════════
  // 溢出保护
  // ═══════════════════════════════════════════════════════════════════════════��═══

  // 获取溢出计数器
  Q_INVOKABLE uint32_t overflow() const { return m_overflowCount.load(std::memory_order_relaxed); }

  // 是否处于溢出风险状态（当前 diff 接近溢出边界）
  Q_INVOKABLE bool isOverflowing() const;

  // 获取当前序列号差异
  Q_INVOKABLE uint32_t currentDiff() const;

  // 获取重置原因
  Q_INVOKABLE QString lastResetReason() const;

 private:
  enum class ResetReason {
    Manual,
    Overflow,
    Init,
    Timeout,
  };

  bool checkAndRecordInternal(uint32_t seq, int64_t timestampMs, int64_t localTimeMs,
                              QString* reason);
  void handleOverflow(uint32_t seq);
  void internalReset(const QString& reason, ResetReason reasonType);

  mutable QMutex m_mutex;
  uint32_t m_highestSeq = 0;
  bool m_initialized = false;
  std::bitset<WINDOW_SIZE> m_window;

  // 溢出保护
  std::atomic<int> m_overflowCount{0};
  uint32_t m_lastSeqBeforeOverflow = 0;
  QString m_lastResetReason;
  int64_t m_lastOverflowTimeMs = 0;
};