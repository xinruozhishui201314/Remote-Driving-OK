#pragma once
#include <QObject>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include <functional>
#include <memory>

/**
 * 三档优先级线程池（《客户端架构设计》§4.1 线程架构）。
 *
 * - Worker:       低优先级，用于日志/诊断/文件IO（2-4 线程）
 * - High:         高优先级，用于网络IO/媒体处理（2-4 线程）
 * - TimeCritical: 最高优先级，用于安全监控/控制关键路径（1-2 线程）
 */
class ThreadPool : public QObject {
  Q_OBJECT

 public:
  enum class Priority { Worker, High, TimeCritical };

  static ThreadPool& instance() {
    static ThreadPool pool;
    return pool;
  }

  explicit ThreadPool(QObject* parent = nullptr);
  ~ThreadPool() override;

  // 提交任务到指定优先级池
  void submit(Priority prio, std::function<void()> task);

  // 提交带优先级的 QRunnable
  void submit(Priority prio, QRunnable* runnable, int runnablePriority = 0);

  // 等待所有任务完成
  void waitForDone(int msecs = -1);

  // 获取底层 QThreadPool（供需要直接使用的场景）
  QThreadPool* pool(Priority prio);

 private:
  QThreadPool m_workerPool;
  QThreadPool m_highPool;
  QThreadPool m_timeCriticalPool;
};

/**
 * 辅助：将 lambda 包装为 QRunnable。
 */
class LambdaRunnable : public QRunnable {
 public:
  explicit LambdaRunnable(std::function<void()> fn) : m_fn(std::move(fn)) { setAutoDelete(true); }
  void run() override { m_fn(); }

 private:
  std::function<void()> m_fn;
};
