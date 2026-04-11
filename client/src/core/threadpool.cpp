#include "threadpool.h"

#include <QDebug>

ThreadPool::ThreadPool(QObject* parent) : QObject(parent) {
  // Worker: 低优先级，4线程
  m_workerPool.setMaxThreadCount(4);

  // High: 高优先级，4线程
  m_highPool.setMaxThreadCount(4);

  // TimeCritical: 最高优先级，2线程（安全监控/控制）
  m_timeCriticalPool.setMaxThreadCount(2);

  qInfo() << "[Client][ThreadPool] initialized"
          << "worker=4 high=4 timeCritical=2";
}

ThreadPool::~ThreadPool() {
  m_timeCriticalPool.waitForDone(5000);
  m_highPool.waitForDone(5000);
  m_workerPool.waitForDone(5000);
}

void ThreadPool::submit(Priority prio, std::function<void()> task) {
  submit(prio, new LambdaRunnable(std::move(task)));
}

void ThreadPool::submit(Priority prio, QRunnable* runnable, int runnablePriority) {
  switch (prio) {
    case Priority::Worker:
      m_workerPool.start(runnable, runnablePriority);
      break;
    case Priority::High:
      m_highPool.start(runnable, runnablePriority);
      break;
    case Priority::TimeCritical:
      m_timeCriticalPool.start(runnable, runnablePriority);
      break;
  }
}

void ThreadPool::waitForDone(int msecs) {
  m_timeCriticalPool.waitForDone(msecs);
  m_highPool.waitForDone(msecs);
  m_workerPool.waitForDone(msecs);
}

QThreadPool* ThreadPool::pool(Priority prio) {
  switch (prio) {
    case Priority::Worker:
      return &m_workerPool;
    case Priority::High:
      return &m_highPool;
    case Priority::TimeCritical:
      return &m_timeCriticalPool;
  }
  return &m_workerPool;
}
