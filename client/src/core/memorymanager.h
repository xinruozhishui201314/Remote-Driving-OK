#pragma once
#include <QObject>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

/**
 * 内存管理器（《客户端架构设计》§4.3）。
 */

/**
 * 类型安全对象池（预分配，O(1) acquire/release）。
 * 2025/2026 规范要求：Lock-free 设计，消除控制路径上的 std::mutex。
 */
template <typename T, std::size_t PoolSize = 64>
class ObjectPool {
 public:
  ObjectPool() : m_storage(), m_top(PoolSize) {
    for (std::size_t i = 0; i < PoolSize; ++i) {
      m_freeNodes[i].store(nullptr);
    }
    m_storage.resize(PoolSize);
    for (std::size_t i = 0; i < PoolSize; ++i) {
      m_freeNodes[i].store(&m_storage[i]);
    }
  }

  // 获取对象（O(1)，无锁原子操作）
  T* acquire() {
    while (true) {
      std::size_t currentTop = m_top.load(std::memory_order_acquire);
      if (currentTop == 0) {
        return nullptr; // 池已空
      }
      std::size_t nextTop = currentTop - 1;
      if (m_top.compare_exchange_weak(currentTop, nextTop, std::memory_order_release)) {
        return m_freeNodes[nextTop].load(std::memory_order_acquire);
      }
    }
  }

  // 归还对象（O(1)，无锁原子操作）
  void release(T* obj) {
    if (!obj) return;
    while (true) {
      std::size_t currentTop = m_top.load(std::memory_order_acquire);
      if (currentTop >= PoolSize) {
        return;
      }
      std::size_t nextTop = currentTop + 1;
      m_freeNodes[currentTop].store(obj, std::memory_order_release);
      if (m_top.compare_exchange_weak(currentTop, nextTop, std::memory_order_release)) {
        return;
      }
    }
  }

  // RAII 句柄
  struct Handle {
    T* ptr = nullptr;
    ObjectPool* pool = nullptr;

    Handle() = default;
    Handle(T* p, ObjectPool* pl) : ptr(p), pool(pl) {}
    ~Handle() {
      if (pool && ptr)
        pool->release(ptr);
    }
    
    Handle(Handle&& other) noexcept : ptr(other.ptr), pool(other.pool) {
      other.ptr = nullptr;
      other.pool = nullptr;
    }
    Handle& operator=(Handle&& other) noexcept {
      if (this != &other) {
        if (pool && ptr) pool->release(ptr);
        ptr = other.ptr;
        pool = other.pool;
        other.ptr = nullptr;
        other.pool = nullptr;
      }
      return *this;
    }
    
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    T* operator->() { return ptr; }
    T& operator*() { return *ptr; }
    explicit operator bool() const { return ptr != nullptr; }
    T* get() const { return ptr; }
  };

  Handle acquireHandle() { return Handle(acquire(), this); }

  std::size_t available() const {
    return m_top.load(std::memory_order_relaxed);
  }

  static constexpr std::size_t capacity() { return PoolSize; }

 private:
  std::vector<T> m_storage;
  std::atomic<T*> m_freeNodes[PoolSize];
  std::atomic<std::size_t> m_top;
};

/**
 * 全局内存管理器（单例）。
 */
class MemoryManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(MemoryManager)

 public:
  static MemoryManager& instance() {
    static MemoryManager mgr;
    return mgr;
  }

  explicit MemoryManager(QObject* parent = nullptr);

  // 锁定所有内存页（需要 CAP_IPC_LOCK 或 ulimit -l unlimited）
  bool lockMemory();

  // 预分配内存以减少页错误（提交虚拟页到物理内存）
  void prefaultStack(std::size_t bytes = 8 * 1024 * 1024);

  // 内存使用统计（读取 /proc/self/status）
  struct MemoryStats {
    size_t virtualKb = 0;
    size_t residentKb = 0;
    size_t sharedKb = 0;
    bool memLocked = false;
  };
  MemoryStats queryStats() const;

  bool isMemoryLocked() const { return m_locked; }

 private:
  bool m_locked = false;
};
