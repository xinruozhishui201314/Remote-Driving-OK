#include "ClientMediaBudget.h"

#include <QtGlobal>

ClientMediaBudget &ClientMediaBudget::instance() {
  static ClientMediaBudget s;
  return s;
}

ClientMediaBudget::ClientMediaBudget() {
  const int envG = qEnvironmentVariableIntValue("CLIENT_MEDIA_RTP_RING_GLOBAL_BYTES");
  const int envS = qEnvironmentVariableIntValue("CLIENT_MEDIA_RTP_RING_PER_SLOT_BYTES");
  if (envG > 65536)
    m_maxGlobalBytes = envG;
  if (envS > 32768)
    m_maxPerSlotBytes = envS;
  for (int i = 0; i < kMaxSlots; ++i) {
    m_slotEnabled[i].store(false, std::memory_order_relaxed);
    m_slotBytes[i].store(0, std::memory_order_relaxed);
  }
}

void ClientMediaBudget::setSlotEnabled(int slot, bool enabled) {
  if (slot < 0 || slot >= kMaxSlots)
    return;
  m_slotEnabled[slot].store(enabled, std::memory_order_release);
}

bool ClientMediaBudget::slotEnabled(int slot) const {
  if (slot < 0 || slot >= kMaxSlots)
    return false;
  return m_slotEnabled[slot].load(std::memory_order_acquire);
}

bool ClientMediaBudget::tryAcquire(int slot, std::int64_t byteLen) {
  if (byteLen <= 0)
    return true;
  if (slot < 0 || slot >= kMaxSlots)
    return true;
  if (!m_slotEnabled[slot].load(std::memory_order_acquire))
    return true;

  for (;;) {
    std::int64_t total = m_totalBytes.load(std::memory_order_acquire);
    if (total + byteLen > m_maxGlobalBytes)
      return false;
    const std::int64_t local = m_slotBytes[slot].load(std::memory_order_acquire);
    if (local + byteLen > m_maxPerSlotBytes)
      return false;
    if (m_totalBytes.compare_exchange_weak(total, total + byteLen, std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
      m_slotBytes[slot].fetch_add(byteLen, std::memory_order_relaxed);
      return true;
    }
  }
}

void ClientMediaBudget::release(int slot, std::int64_t byteLen) {
  if (byteLen <= 0)
    return;
  if (slot < 0 || slot >= kMaxSlots)
    return;
  if (!m_slotEnabled[slot].load(std::memory_order_acquire))
    return;
  m_slotBytes[slot].fetch_sub(byteLen, std::memory_order_relaxed);
  m_totalBytes.fetch_sub(byteLen, std::memory_order_relaxed);
}

std::int64_t ClientMediaBudget::slotBytes(int slot) const {
  if (slot < 0 || slot >= kMaxSlots)
    return 0;
  return m_slotBytes[slot].load(std::memory_order_acquire);
}
