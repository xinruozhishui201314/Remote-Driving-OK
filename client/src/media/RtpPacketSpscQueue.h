#pragma once

#include "ClientMediaBudget.h"
#include "../utils/LockFreeQueue.h"
#include "RtpIngressTypes.h"

#include <QString>

#include <cstddef>

/**
 * 媒体线程（libdatachannel onMessage）→ 解码线程 的 SPSC 包队列。
 * 容量为包个数上限；另受 ClientMediaBudget 字节上限约束（跨路公平）。
 */
class RtpPacketSpscQueue {
 public:
  static constexpr std::size_t kCapacityPackets = 2048;

  explicit RtpPacketSpscQueue(QString streamTag, int budgetSlot);
  ~RtpPacketSpscQueue();

  QString streamTag() const { return m_streamTag; }
  int budgetSlot() const { return m_budgetSlot; }

  /**
   * 生产者（单线程）：尝试入队。失败时预算不泄漏。
   */
  bool tryPush(RtpIngressPacket &&pkt);

  /** 消费者（单线程）：出队；成功时释放预算字节 */
  bool pop(RtpIngressPacket &out);

  bool empty() const { return m_queue.empty(); }
  std::size_t packetCount() const { return m_queue.size(); }

  /** 消费者：丢弃全部已入队包（teardown） */
  void discardAll();

 private:
  QString m_streamTag;
  int m_budgetSlot = -1;
  SPSCQueue<RtpIngressPacket, kCapacityPackets> m_queue;
};
