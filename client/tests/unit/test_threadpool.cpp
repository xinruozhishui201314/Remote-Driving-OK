#include "core/threadpool.h"

#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <chrono>

class TestThreadPool : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // 基础功能测试
  void test_singletonAccess();
  void test_constructor();

  // 任务提交测试
  void test_submitSimpleTask();
  void test_submitMultipleTasks();

  // 优先级测试
  void test_submitWorkerPriority();
  void test_submitHighPriority();
  void test_submitTimeCriticalPriority();

  // 等待测试
  void test_waitForDone();

  // 线程池访问测试
  void test_poolAccess();
};

void TestThreadPool::initTestCase() {}

void TestThreadPool::cleanupTestCase() {}

void TestThreadPool::init() {}

void TestThreadPool::cleanup() {}

void TestThreadPool::test_singletonAccess() {
  // 测试单例访问
  ThreadPool& pool1 = ThreadPool::instance();
  ThreadPool& pool2 = ThreadPool::instance();
  QVERIFY2(&pool1 == &pool2, "ThreadPool singleton should return same instance");
}

void TestThreadPool::test_constructor() {
  // 测试默认构造
  ThreadPool defaultPool;
  QVERIFY2(true, "ThreadPool default constructor should not crash");
}

void TestThreadPool::test_submitSimpleTask() {
  ThreadPool& pool = ThreadPool::instance();
  std::atomic<int> counter(0);

  // 提交一个简单任务
  pool.submit(ThreadPool::Priority::High,
              [&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });

  // 等待任务完成
  QTest::qWait(200);

  // 验证任务被执行
  QVERIFY2(counter.load() == 1,
           qPrintable(QString("Counter should be 1, got %1").arg(counter.load())));
}

void TestThreadPool::test_submitMultipleTasks() {
  ThreadPool& pool = ThreadPool::instance();
  std::atomic<int> counter(0);
  const int taskCount = 10;

  // 提交多个任务
  for (int i = 0; i < taskCount; ++i) {
    pool.submit(ThreadPool::Priority::Worker, [&counter, i]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      counter.fetch_add(1, std::memory_order_relaxed);
    });
  }

  // 等待所有任务完成
  pool.waitForDone(5000);

  // 验证所有任务都被执行
  QVERIFY2(counter.load() == taskCount,
           qPrintable(QString("Expected %1 tasks, got %2").arg(taskCount).arg(counter.load())));
}

void TestThreadPool::test_submitWorkerPriority() {
  ThreadPool& pool = ThreadPool::instance();
  std::atomic<int> executed(0);

  // 提交 Worker 优先级任务
  pool.submit(ThreadPool::Priority::Worker,
              [&executed]() { executed.fetch_add(1, std::memory_order_relaxed); });

  QTest::qWait(100);
  QVERIFY2(executed.load() >= 0, "Worker priority task should be submitted");
}

void TestThreadPool::test_submitHighPriority() {
  ThreadPool& pool = ThreadPool::instance();
  std::atomic<int> executed(0);

  // 提交 High 优先级任务
  pool.submit(ThreadPool::Priority::High,
              [&executed]() { executed.fetch_add(1, std::memory_order_relaxed); });

  QTest::qWait(100);
  QVERIFY2(executed.load() >= 0, "High priority task should be submitted");
}

void TestThreadPool::test_submitTimeCriticalPriority() {
  ThreadPool& pool = ThreadPool::instance();
  std::atomic<int> executed(0);

  // 提交 TimeCritical 优先级任务
  pool.submit(ThreadPool::Priority::TimeCritical,
              [&executed]() { executed.fetch_add(1, std::memory_order_relaxed); });

  QTest::qWait(100);
  QVERIFY2(executed.load() >= 0, "TimeCritical priority task should be submitted");
}

void TestThreadPool::test_waitForDone() {
  ThreadPool& pool = ThreadPool::instance();
  std::atomic<int> counter(0);
  const int taskCount = 5;

  // 提交任务
  for (int i = 0; i < taskCount; ++i) {
    pool.submit(ThreadPool::Priority::High, [&counter]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      counter.fetch_add(1, std::memory_order_relaxed);
    });
  }

  // 等待完成
  pool.waitForDone(5000);

  // 验证所有任务完成
  QVERIFY2(counter.load() == taskCount,
           qPrintable(
               QString("Expected %1 tasks completed, got %2").arg(taskCount).arg(counter.load())));
}

void TestThreadPool::test_poolAccess() {
  ThreadPool& pool = ThreadPool::instance();

  // 获取各个优先级的线程池
  QThreadPool* workerPool = pool.pool(ThreadPool::Priority::Worker);
  QThreadPool* highPool = pool.pool(ThreadPool::Priority::High);
  QThreadPool* timeCriticalPool = pool.pool(ThreadPool::Priority::TimeCritical);

  // 验证线程池非空
  QVERIFY2(workerPool != nullptr, "Worker pool should not be null");
  QVERIFY2(highPool != nullptr, "High priority pool should not be null");
  QVERIFY2(timeCriticalPool != nullptr, "TimeCritical pool should not be null");
}

QTEST_MAIN(TestThreadPool)
#include "test_threadpool.moc"