#include "antireplayguard.h"

#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>

AntiReplayGuard::AntiReplayGuard()
    : m_mutex(),
      m_highestSeq(0),
      m_initialized(false),
      m_window(),
      m_overflowCount(0),
      m_lastSeqBeforeOverflow(0),
      m_lastResetReason(QStringLiteral("Initial construction")),
      m_lastOverflowTimeMs(0) {}

bool AntiReplayGuard::checkAndRecord(uint32_t seq, int64_t timestampMs, int64_t localTimeMs,
                                     QString* reason) {
  // 时间戳漂移检查（不加锁，先快速失败）
  if (!checkTimestampDrift(timestampMs, localTimeMs, reason)) {
    qWarning() << "[Client][AntiReplay] timestamp rejected seq=" << seq
               << "drift=" << (timestampMs - localTimeMs) << "ms";
    return false;
  }

  return checkAndRecordInternal(seq, timestampMs, localTimeMs, reason);
}

bool AntiReplayGuard::checkAndRecordInternal(uint32_t seq, int64_t timestampMs, int64_t localTimeMs,
                                             QString* reason) {
  Q_UNUSED(timestampMs);
  Q_UNUSED(localTimeMs);
  QMutexLocker lock(&m_mutex);

  if (!m_initialized) {
    // 第一条消息，直接接受并初始化窗口基准
    m_highestSeq = seq;
    m_initialized = true;
    m_window.reset();
    m_window.set(seq % WINDOW_SIZE);
    qInfo() << "[Client][AntiReplay] Initialized with seq=" << seq;
    return true;
  }

  // 计算相对于最高 seq 的差异（处理环绕）
  int32_t diff = static_cast<int32_t>(seq) - static_cast<int32_t>(m_highestSeq);

  // 检查溢出风险（仅对正差值有意义）
  // 溢出发生在 uint32_t 环绕时：
  // 当 diff > 0 且 diff 接近 2^31 时，说明 seq 发生了环绕
  // 注意：diff < 0 时绝对不是溢出，而是正常的乱序包或重复包
  if (diff > 0) {
    // 新 seq 比当前最高值大
    uint32_t uDiff = static_cast<uint32_t>(diff);
    // 只有当无符号差值超过溢出阈值时才认为是溢出环绕
    if (uDiff > OVERFLOW_THRESHOLD) {
      // 检测到溢出环绕：uint32_t 计数器回绕后重新开始
      handleOverflow(seq);
      return true;
    }
  }
  // diff <= 0 的情况（相同 seq 或更小的 seq）不需要检查溢出

  // 正常序列号处理
  if (diff > 0) {
    // 新 seq，比当前最高序号更大 —— 正常前进
    if (static_cast<uint32_t>(diff) >= static_cast<uint32_t>(WINDOW_SIZE)) {
      // 跨度超窗口，重置整个 bitset（防止大跳跃时误判）
      m_window.reset();
    } else {
      // 清除 [highestSeq+1 .. seq] 对应 slot（标记为"未见"）
      for (int32_t i = 1; i <= diff; ++i) {
        m_window.reset((m_highestSeq + static_cast<uint32_t>(i)) % WINDOW_SIZE);
      }
    }
    m_highestSeq = seq;
    m_window.set(seq % WINDOW_SIZE);
    return true;
  }

  if (diff == 0) {
    // 完全相同的 seq：重复包
    if (reason)
      *reason = QString("duplicate seq=%1").arg(seq);
    qWarning() << "[Client][AntiReplay] DUPLICATE seq=" << seq;
    return false;
  }

  // diff < 0：seq 比已知最高值更小（旧包或重放）
  const uint32_t behind = static_cast<uint32_t>(-diff);
  if (behind >= static_cast<uint32_t>(WINDOW_SIZE)) {
    // 超出窗口，无法判断 —— 保守拒绝
    if (reason)
      *reason = QString("seq=%1 too old (behind=%2)").arg(seq).arg(behind);
    qWarning() << "[Client][AntiReplay] TOO OLD seq=" << seq << "behind=" << behind;
    return false;
  }

  if (m_window.test(seq % WINDOW_SIZE)) {
    // 窗口内已见过 —— 重放
    if (reason)
      *reason = QString("replay seq=%1").arg(seq);
    qWarning() << "[Client][AntiReplay] REPLAY seq=" << seq;
    return false;
  }

  // 窗口内但未见过（乱序到达的旧包），接受
  m_window.set(seq % WINDOW_SIZE);
  return true;
}

void AntiReplayGuard::handleOverflow(uint32_t seq) {
  m_lastSeqBeforeOverflow = m_highestSeq;
  m_overflowCount.fetch_add(1, std::memory_order_relaxed);
  m_lastOverflowTimeMs = QDateTime::currentMSecsSinceEpoch();

  qWarning() << "[Client][AntiReplay] OVERFLOW detected! counter="
             << m_overflowCount.load(std::memory_order_relaxed) << "lastSeq=" << m_highestSeq
             << "newSeq=" << seq
             << "diff=" << static_cast<int32_t>(seq) - static_cast<int32_t>(m_highestSeq);

  internalReset(QString("overflow: seq jumped from %1 to %2, diff=%3")
                    .arg(m_highestSeq)
                    .arg(seq)
                    .arg(static_cast<int32_t>(seq) - static_cast<int32_t>(m_highestSeq)),
                ResetReason::Overflow);

  // 处理新序列号
  m_highestSeq = seq;
  m_initialized = true;
  m_window.reset();
  m_window.set(seq % WINDOW_SIZE);
}

void AntiReplayGuard::internalReset(const QString& reason, ResetReason reasonType) {
  m_highestSeq = 0;
  m_initialized = false;
  m_window.reset();
  m_lastResetReason = reason;

  switch (reasonType) {
    case ResetReason::Overflow:
      qWarning() << "[Client][AntiReplay] Reset due to overflow:" << reason;
      break;
    case ResetReason::Manual:
      qInfo() << "[Client][AntiReplay] Manual reset:" << reason;
      break;
    case ResetReason::Init:
      qDebug() << "[Client][AntiReplay] Reset due to init:" << reason;
      break;
    case ResetReason::Timeout:
      qWarning() << "[Client][AntiReplay] Reset due to timeout:" << reason;
      break;
  }
}

void AntiReplayGuard::reset() {
  QMutexLocker lock(&m_mutex);
  internalReset("manual reset", ResetReason::Manual);
}

bool AntiReplayGuard::checkTimestampDrift(int64_t msgTimestampMs, int64_t localTimeMs,
                                          QString* reason) {
  const int64_t drift = msgTimestampMs - localTimeMs;
  if (drift > MAX_TIMESTAMP_DRIFT_MS) {
    if (reason)
      *reason = QString("timestamp too far in future drift=%1ms").arg(drift);
    return false;
  }
  if (drift < MIN_TIMESTAMP_DRIFT_MS) {
    if (reason)
      *reason = QString("timestamp too old drift=%1ms").arg(drift);
    return false;
  }
  return true;
}

bool AntiReplayGuard::isOverflowing() const {
  QMutexLocker lock(&m_mutex);

  if (!m_initialized) {
    return false;
  }

  // 检查当前差异是否接近溢出边界
  uint32_t diff = 0;
  if (m_highestSeq != 0) {
    // 计算到溢出边界的距离
    // 如果差异接近 OVERFLOW_THRESHOLD，返回 true
    // 这里简化处理：检查差异是否大于 OVERFLOW_THRESHOLD 的 75%
    diff = m_highestSeq;  // 简化的估算
  }

  return diff > (OVERFLOW_THRESHOLD * 3 / 4);
}

uint32_t AntiReplayGuard::currentDiff() const {
  // 这是简化实现，实际应用中可能需要更复杂的逻辑
  // 返回当前序列号的估算差异
  return m_highestSeq;
}

QString AntiReplayGuard::lastResetReason() const { return m_lastResetReason; }