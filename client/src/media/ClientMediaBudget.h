#pragma once

#include <array>
#include <atomic>
#include <cstdint>

/**
 * 四路 WebRTC 入站 RTP 的全局 + 分槽字节预算（跨路公平，避免单路撑爆进程内存）。
 * tryAcquire 在入队前调用；release 在成功出队后调用。与 RtpPacketSpscQueue 配对使用。
 */
class ClientMediaBudget {
 public:
  static constexpr int kMaxSlots = 4;

  static ClientMediaBudget &instance();

  /** 无效槽位：不记账（单测或未注册） */
  void setSlotEnabled(int slot, bool enabled);
  bool slotEnabled(int slot) const;

  /**
   * 尝试为即将入队的 RTP 包占用字节预算。
   * @return false 时调用方应丢弃该包（并可触发 PLI）。
   */
  bool tryAcquire(int slot, std::int64_t byteLen);

  /** 包已从 SPSC 队列取出并交给解码器后调用 */
  void release(int slot, std::int64_t byteLen);

  std::int64_t totalBytes() const { return m_totalBytes.load(std::memory_order_acquire); }
  std::int64_t slotBytes(int slot) const;

  std::int64_t maxGlobalBytes() const { return m_maxGlobalBytes; }
  std::int64_t maxPerSlotBytes() const { return m_maxPerSlotBytes; }

 private:
  ClientMediaBudget();

  std::array<std::atomic<std::int64_t>, kMaxSlots> m_slotBytes{};
  std::array<std::atomic<bool>, kMaxSlots> m_slotEnabled{};
  std::atomic<std::int64_t> m_totalBytes{0};

  std::int64_t m_maxGlobalBytes = 12LL * 1024 * 1024;
  std::int64_t m_maxPerSlotBytes = 4LL * 1024 * 1024;
};
