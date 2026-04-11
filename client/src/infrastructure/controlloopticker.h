#pragma once
#ifndef CLIENT_INFRA_CONTROLLOOPTICKER_H
#define CLIENT_INFRA_CONTROLLOOPTICKER_H

#include <QObject>
#include <QThread>

#include <atomic>
#include <chrono>

class ControlLoopTicker : public QObject {
  Q_OBJECT
 public:
  struct PerfStats {
    qint64 tickIndex = 0;
    double actualHz = 0.0;
    double jitterMs = 0.0;       // 抖动
    double avgJitterMs = 0.0;    // 平均抖动
    qint64 tickMissedCount = 0;  // 掉 tick 数
    qint64 tickOverruns = 0;     // 超时 tick 数
    int64_t lastTickTimestampNs = 0;
  };

  explicit ControlLoopTicker(QObject* parent = nullptr);
  ~ControlLoopTicker() override;

  void setIntervalMs(int ms);
  int intervalMs() const { return m_intervalMs.load(); }
  void start();
  void stop();
  bool isRunning() const { return m_running.load(); }
  PerfStats getPerfStats() const;
  void resetPerfStats();

 signals:
  void tick(qint64 tickIndex);
  void perfStatsUpdated(const PerfStats& stats);

 private:
  void tickerThread();

  QThread* m_thread = nullptr;
  std::atomic<bool> m_running{false};
  std::atomic<bool> m_stopRequested{false};
  std::atomic<int> m_intervalMs{10};
  std::atomic<qint64> m_index{0};

  std::atomic<double> m_actualHz{0.0};
  std::atomic<double> m_jitterMs{0.0};
  std::atomic<double> m_avgJitterMs{0.0};
  std::atomic<qint64> m_tickMissedCount{0};
  std::atomic<qint64> m_tickOverruns{0};
  std::atomic<int64_t> m_lastTickTimestampNs{0};

  // 注意：抖动计算使用局部数组 jitterSamples[JITTER_WINDOW_SIZE]
  // 以避免线程间的伪共享(false sharing)和栈空间问题

  static constexpr int JITTER_WINDOW_SIZE = 100;
};

#endif  // CLIENT_INFRA_CONTROLLOOPTICKER_H
