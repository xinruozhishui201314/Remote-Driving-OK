#pragma once
#include <QObject>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

/**
 * 内存管理器（《客户端架构设计》§4.3）。
 *
 * 功能：
 * 1. ObjectPool<T>：预分配对象池，消除关键路径动态分配
 * 2. mlockall()：锁定所有内存页到物理内存（防止 page fault 抖动）
 * 3. 内存使用统计
 */

/**
 * 类型安全对象池（预分配，O(1) acquire/release）。
 */
template <typename T, std::size_t PoolSize = 64>
class ObjectPool {
 public:
  ObjectPool() {
    m_storage.resize(PoolSize);
    m_freeList.reserve(PoolSize);
    for (std::size_t i = 0; i < PoolSize; ++i) {
      m_freeList.push_back(&m_storage[i]);
    }
  }

  // 获取对象（O(1)，不调用构造函数）
  T* acquire() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_freeList.empty())
      return nullptr;
    T* obj = m_freeList.back();
    m_freeList.pop_back();
    return obj;
  }

  // 归还对象（O(1)，不调用析构函数）
  void release(T* obj) {
    if (!obj)
      return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_freeList.push_back(obj);
  }

  // RAII 句柄
  struct Handle {
    T* ptr = nullptr;
    ObjectPool* pool = nullptr;

    ~Handle() {
      if (pool && ptr)
        pool->release(ptr);
    }
    T* operator->() { return ptr; }
    T& operator*() { return *ptr; }
    explicit operator bool() const { return ptr != nullptr; }
  };

  Handle acquireHandle() { return {acquire(), this}; }

  std::size_t available() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_freeList.size();
  }

  static constexpr std::size_t capacity() { return PoolSize; }

 private:
  std::vector<T> m_storage;
  std::vector<T*> m_freeList;
  mutable std::mutex m_mutex;
};

/**
 * 全局内存管理器（单例）。
 */
class MemoryManager : public QObject {
  Q_OBJECT

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
