#pragma once
#include <array>
#include <atomic>
#include <cstddef>

/**
 * SPSC 单生产者单消费者无锁环形队列。
 * 控制通道延迟 < 500ns，避免 mutex 在关键路径。
 * Capacity 必须是 2 的幂（运行时断言），以便用位掩码代替取模。
 */
template <typename T, std::size_t Capacity = 1024>
class SPSCQueue {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

 public:
  SPSCQueue() = default;

  // 生产者调用（单线程）
  bool push(const T& item) {
    const auto tail = m_tail.load(std::memory_order_relaxed);
    const auto next = (tail + 1) & kMask;
    if (next == m_head.load(std::memory_order_acquire)) {
      return false;  // 队列满
    }
    m_buffer[tail] = item;
    m_tail.store(next, std::memory_order_release);
    return true;
  }

  bool push(T&& item) {
    const auto tail = m_tail.load(std::memory_order_relaxed);
    const auto next = (tail + 1) & kMask;
    if (next == m_head.load(std::memory_order_acquire)) {
      return false;
    }
    m_buffer[tail] = std::move(item);
    m_tail.store(next, std::memory_order_release);
    return true;
  }

  // 消费者调用（单线程）
  bool pop(T& item) {
    const auto head = m_head.load(std::memory_order_relaxed);
    if (head == m_tail.load(std::memory_order_acquire)) {
      return false;  // 队列空
    }
    item = std::move(m_buffer[head]);
    m_head.store((head + 1) & kMask, std::memory_order_release);
    return true;
  }

  bool empty() const {
    return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
  }

  std::size_t size() const {
    const auto h = m_head.load(std::memory_order_relaxed);
    const auto t = m_tail.load(std::memory_order_relaxed);
    return (t - h) & kMask;
  }

 private:
  static constexpr std::size_t kMask = Capacity - 1;

  alignas(64) std::array<T, Capacity> m_buffer{};
  alignas(64) std::atomic<std::size_t> m_head{0};
  alignas(64) std::atomic<std::size_t> m_tail{0};
};

/**
 * MPSC 多生产者单消费者无锁栈（用于帧池归还）。
 */
template <typename T>
class LockFreeStack {
 public:
  void push(T* item) {
    Node* node = new Node{item, m_top.load(std::memory_order_relaxed)};
    while (!m_top.compare_exchange_weak(node->next, node, std::memory_order_release,
                                        std::memory_order_relaxed)) {
    }
  }

  bool pop(T*& item) {
    Node* node = m_top.load(std::memory_order_acquire);
    while (node) {
      if (m_top.compare_exchange_weak(node, node->next, std::memory_order_release,
                                      std::memory_order_acquire)) {
        item = node->data;
        delete node;
        return true;
      }
    }
    return false;
  }

 private:
  struct Node {
    T* data;
    Node* next;
  };
  std::atomic<Node*> m_top{nullptr};
};
