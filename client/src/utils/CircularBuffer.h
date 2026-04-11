#pragma once
#include <array>
#include <cstddef>
#include <stdexcept>

/**
 * 固定容量环形缓冲区（非线程安全，单线程历史记录使用）。
 * 用于延迟历史、性能指标历史等不需要跨线程的场景。
 */
template <typename T, std::size_t Capacity>
class CircularBuffer {
 public:
  void push_back(const T& item) {
    m_buffer[m_tail] = item;
    m_tail = (m_tail + 1) % Capacity;
    if (m_size < Capacity) {
      ++m_size;
    } else {
      m_head = (m_head + 1) % Capacity;  // 覆盖最老数据
    }
  }

  void push_back(T&& item) {
    m_buffer[m_tail] = std::move(item);
    m_tail = (m_tail + 1) % Capacity;
    if (m_size < Capacity) {
      ++m_size;
    } else {
      m_head = (m_head + 1) % Capacity;
    }
  }

  const T& operator[](std::size_t idx) const { return m_buffer[(m_head + idx) % Capacity]; }

  T& operator[](std::size_t idx) { return m_buffer[(m_head + idx) % Capacity]; }

  const T& back() const { return m_buffer[(m_tail + Capacity - 1) % Capacity]; }
  const T& front() const { return m_buffer[m_head]; }

  std::size_t size() const { return m_size; }
  bool empty() const { return m_size == 0; }
  bool full() const { return m_size == Capacity; }
  static constexpr std::size_t capacity() { return Capacity; }

  void clear() {
    m_head = 0;
    m_tail = 0;
    m_size = 0;
  }

 private:
  std::array<T, Capacity> m_buffer{};
  std::size_t m_head = 0;
  std::size_t m_tail = 0;
  std::size_t m_size = 0;
};
