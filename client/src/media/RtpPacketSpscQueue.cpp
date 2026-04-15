#include "RtpPacketSpscQueue.h"

#include <QDebug>

RtpPacketSpscQueue::RtpPacketSpscQueue(QString streamTag, int budgetSlot)
    : m_streamTag(std::move(streamTag)), m_budgetSlot(budgetSlot), m_queue() {}

RtpPacketSpscQueue::~RtpPacketSpscQueue() { discardAll(); }

bool RtpPacketSpscQueue::tryPush(RtpIngressPacket &&pkt) {
  const std::int64_t n = static_cast<std::int64_t>(pkt.bytes.size());
  ClientMediaBudget &bud = ClientMediaBudget::instance();
  if (m_budgetSlot >= 0 && m_budgetSlot < ClientMediaBudget::kMaxSlots) {
    if (!bud.tryAcquire(m_budgetSlot, n)) {
      static std::atomic<int> s_log{0};
      const int c = ++s_log;
      if (c <= 12 || (c % 200) == 0) {
        qWarning() << "[Client][Media][Budget] tryAcquire 拒绝 stream=" << m_streamTag
                   << " slot=" << m_budgetSlot << " bytes=" << n << " totalB=" << bud.totalBytes()
                   << " slotB=" << bud.slotBytes(m_budgetSlot)
                   << " ★ 跨路字节预算满，丢弃 RTP（可增大 CLIENT_MEDIA_RTP_RING_* 或降码率）";
      }
      return false;
    }
  }
  if (!m_queue.push(std::move(pkt))) {
    if (m_budgetSlot >= 0 && m_budgetSlot < ClientMediaBudget::kMaxSlots)
      bud.release(m_budgetSlot, n);
    static std::atomic<int> s_full{0};
    const int c = ++s_full;
    if (c <= 12 || (c % 200) == 0) {
      qWarning() << "[Client][Media][Ring] SPSC 满 stream=" << m_streamTag
                 << " capPackets=" << kCapacityPackets << " n=" << c;
    }
    return false;
  }
  return true;
}

bool RtpPacketSpscQueue::pop(RtpIngressPacket &out) {
  if (!m_queue.pop(out))
    return false;
  if (m_budgetSlot >= 0 && m_budgetSlot < ClientMediaBudget::kMaxSlots) {
    const std::int64_t n = static_cast<std::int64_t>(out.bytes.size());
    ClientMediaBudget::instance().release(m_budgetSlot, n);
  }
  return true;
}

void RtpPacketSpscQueue::discardAll() {
  RtpIngressPacket p;
  while (pop(p)) {
  }
}
