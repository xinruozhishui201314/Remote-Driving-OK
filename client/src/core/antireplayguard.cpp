#include "antireplayguard.h"
#include <QMutexLocker>
#include <QDebug>

// ─── checkAndRecord ───────────────────────────────────────────────────────────

bool AntiReplayGuard::checkAndRecord(uint32_t seq, int64_t timestampMs,
                                      int64_t localTimeMs, QString* reason)
{
    // 时间戳漂移检查（不加锁，先快速失败）
    if (!checkTimestampDrift(timestampMs, localTimeMs, reason)) {
        qWarning() << "[Client][AntiReplay] timestamp rejected seq=" << seq
                   << "drift=" << (timestampMs - localTimeMs) << "ms";
        return false;
    }

    QMutexLocker lock(&m_mutex);

    if (!m_initialized) {
        // 第一条消息，直接接受并初始化窗口基准
        m_highestSeq  = seq;
        m_initialized = true;
        m_window.reset();
        m_window.set(seq % WINDOW_SIZE);
        return true;
    }

    // 检查相对于最高 seq 的位置
    const int32_t diff = static_cast<int32_t>(seq) - static_cast<int32_t>(m_highestSeq);

    if (diff > 0) {
        // 新 seq，比当前最高序号更大 —— 正常前进
        if (diff >= WINDOW_SIZE) {
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
        if (reason) *reason = QString("duplicate seq=%1").arg(seq);
        qWarning() << "[Client][AntiReplay] DUPLICATE seq=" << seq;
        return false;
    }

    // diff < 0：seq 比已知最高值更小（旧包或重放）
    const uint32_t behind = static_cast<uint32_t>(-diff);
    if (behind >= static_cast<uint32_t>(WINDOW_SIZE)) {
        // 超出窗口，无法判断 —— 保守拒绝
        if (reason) *reason = QString("seq=%1 too old (behind=%2)").arg(seq).arg(behind);
        qWarning() << "[Client][AntiReplay] TOO OLD seq=" << seq << "behind=" << behind;
        return false;
    }

    if (m_window.test(seq % WINDOW_SIZE)) {
        // 窗口内已见过 —— 重放
        if (reason) *reason = QString("replay seq=%1").arg(seq);
        qWarning() << "[Client][AntiReplay] REPLAY seq=" << seq;
        return false;
    }

    // 窗口内但未见过（乱序到达的旧包），接受
    m_window.set(seq % WINDOW_SIZE);
    return true;
}

// ─── reset ────────────────────────────────────────────────────────────────────

void AntiReplayGuard::reset()
{
    QMutexLocker lock(&m_mutex);
    m_highestSeq  = 0;
    m_initialized = false;
    m_window.reset();
    qInfo() << "[Client][AntiReplay] window reset";
}

// ─── checkTimestampDrift ──────────────────────────────────────────────────────

bool AntiReplayGuard::checkTimestampDrift(int64_t msgTimestampMs,
                                           int64_t localTimeMs, QString* reason)
{
    const int64_t drift = msgTimestampMs - localTimeMs;
    if (drift > MAX_TIMESTAMP_DRIFT_MS) {
        if (reason) *reason = QString("timestamp too far in future drift=%1ms").arg(drift);
        return false;
    }
    if (drift < MIN_TIMESTAMP_DRIFT_MS) {
        if (reason) *reason = QString("timestamp too old drift=%1ms").arg(drift);
        return false;
    }
    return true;
}
