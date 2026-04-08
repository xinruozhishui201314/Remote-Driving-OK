#include "controlloopticker.h"
#include "../core/metricscollector.h"
#include <QDebug>
#include <QThread>
#include <QCoreApplication>

using namespace std::chrono;

// 纳秒转毫秒
static inline double nsToMs(int64_t ns) {
    return static_cast<double>(ns) / 1'000'000.0;
}

ControlLoopTicker::ControlLoopTicker(QObject* parent)
    : QObject(parent)
{
    qInfo() << "[Client][ControlLoopTicker] created with default intervalMs=10";
}

ControlLoopTicker::~ControlLoopTicker()
{
    stop();
    qInfo() << "[Client][ControlLoopTicker] destroyed";
}

void ControlLoopTicker::setIntervalMs(int ms)
{
    const int bounded = qBound(1, ms, 1000);
    m_intervalMs.store(bounded);
    qInfo() << "[Client][ControlLoopTicker] intervalMs set to" << bounded;
}

void ControlLoopTicker::start()
{
    if (m_running.load()) {
        qWarning() << "[Client][ControlLoopTicker] already running, ignoring start()";
        return;
    }

    // 重置所有状态
    resetPerfStats();
    m_stopRequested.store(false);
    m_index.store(0);

    // 创建独立高精度线程
    m_thread = QThread::create([this]() { tickerThread(); });
    m_thread->setObjectName(QString("ControlLoopTicker-%1Hz").arg(1000 / m_intervalMs.load()));
    m_thread->setPriority(QThread::TimeCriticalPriority);

    // 确保信号在主线程发射
    connect(m_thread, &QThread::finished, this, [this]() {
        qInfo() << "[Client][ControlLoopTicker] thread finished"
                << "totalTicks=" << m_index.load()
                << "missed=" << m_tickMissedCount.load()
                << "overruns=" << m_tickOverruns.load();
    }, Qt::QueuedConnection);

    m_running.store(true);
    m_thread->start();

    qInfo() << "[Client][ControlLoopTicker] started"
            << "intervalMs=" << m_intervalMs.load()
            << "threadId=" << reinterpret_cast<void*>(m_thread->currentThreadId());
}

void ControlLoopTicker::stop()
{
    if (!m_running.load()) {
        return;
    }

    qInfo() << "[Client][ControlLoopTicker] stopping...";
    m_stopRequested.store(true);

    if (m_thread && QThread::currentThread() != m_thread) {
        // 使用 requestInterruption + quit 优雅退出，不使用 terminate()
        m_thread->requestInterruption();
        m_thread->quit();
        
        // 等待线程自然退出（最多5秒）
        if (!m_thread->wait(5000)) {
            qWarning() << "[Client][ControlLoopTicker] thread wait timeout, force stopping...";
            // 如果线程仍然没有退出，强制设置停止标志并再次等待
            // 注意：不再使用 terminate()，而是让线程在下一次循环检测到停止标志
        }
        
        if (m_thread->isFinished()) {
            delete m_thread;
            m_thread = nullptr;
        }
    }

    m_running.store(false);
    qInfo() << "[Client][ControlLoopTicker] stopped";
}

void ControlLoopTicker::tickerThread()
{
    using Clock = steady_clock;

    const int interval = m_intervalMs.load();
    const auto expectedInterval = milliseconds(interval);
    const auto expectedIntervalNs = int64_t(interval) * 1'000'000;

    auto nextWakeTime = Clock::now();
    auto lastTickTime = nextWakeTime;
    int64_t tickCount = 0;

    // 滑动平均抖动计算（环形缓冲区）
    double jitterSamples[JITTER_WINDOW_SIZE] = {0.0};
    int jitterIdx = 0;
    double jitterSum = 0.0;
    int jitterCount = 0;

    qInfo() << "[Client][ControlLoopTicker][TickerThread] loop started"
            << "expectedInterval=" << interval << "ms";

    while (!m_stopRequested.load()) {
        // 等待到下一个 tick 时间
        std::this_thread::sleep_until(nextWakeTime);

        // 检查停止请求（sleep 后再次检查）
        if (m_stopRequested.load()) {
            break;
        }

        const auto now = Clock::now();
        const auto actualInterval = now - lastTickTime;
        const auto actualIntervalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(actualInterval).count();

        // 计算抖动：|实际间隔 - 期望间隔|
        const double jitter = std::abs(nsToMs(actualIntervalNs - expectedIntervalNs));

        // 更新滑动平均抖动（最近100个样本的移动平均）
        if (jitterCount >= JITTER_WINDOW_SIZE) {
            // 窗口已满，减去最旧的样本
            jitterSum -= jitterSamples[jitterIdx];
        }
        jitterSamples[jitterIdx] = jitter;
        jitterSum += jitter;
        jitterIdx = (jitterIdx + 1) % JITTER_WINDOW_SIZE;
        jitterCount++;
        const double avgJitter = jitterSum / std::min(jitterCount, JITTER_WINDOW_SIZE);

        // 检测掉 tick：如果实际间隔 > 期望间隔的 1.5 倍
        if (actualIntervalNs > expectedIntervalNs * 1.5) {
            m_tickMissedCount.fetch_add(1);
            qWarning().noquote() << "[Client][ControlLoopTicker] ★ TICK MISSED ★"
                                  << "actualMs=" << nsToMs(actualIntervalNs)
                                  << "expectedMs=" << interval
                                  << "missedCount=" << m_tickMissedCount.load();
        }

        // 检测超时抖动：如果抖动 > 间隔的 50%
        if (jitter > interval * 0.5) {
            m_tickOverruns.fetch_add(1);
        }

        // 更新性能统计
        const auto index = ++m_index;
        const double actualHz = 1000.0 / interval;
        m_jitterMs.store(jitter);
        m_avgJitterMs.store(avgJitter);
        m_lastTickTimestampNs.store(duration_cast<nanoseconds>(now.time_since_epoch()).count());
        tickCount++;

        // 每秒更新一次 actualHz（简化：直接使用期望值，因为steady_clock本身是准确的）
        if (tickCount % static_cast<int64_t>(actualHz) == 0) {
            m_actualHz.store(actualHz);
        }

        // 在主线程发射 tick 信号
        Q_EMIT tick(index);

        // 定期发射性能统计更新（每100ms一次）
        if (tickCount % static_cast<int64_t>(actualHz / 10) == 0) {
            PerfStats stats;
            stats.tickIndex = index;
            stats.actualHz = actualHz;
            stats.jitterMs = jitter;
            stats.avgJitterMs = avgJitter;
            stats.tickMissedCount = m_tickMissedCount.load();
            stats.tickOverruns = m_tickOverruns.load();
            stats.lastTickTimestampNs = duration_cast<nanoseconds>(now.time_since_epoch()).count();

            Q_EMIT perfStatsUpdated(stats);

            // 更新 MetricsCollector
            MetricsCollector& metrics = MetricsCollector::instance();
            metrics.set(MetricsCollector::Metrics::CONTROL_RPS, actualHz);
            metrics.observe("control_jitter_ms", jitter);
            metrics.set("control_avg_jitter_ms", avgJitter);
            metrics.set("control_tick_missed_total", static_cast<double>(m_tickMissedCount.load()));
            metrics.set("control_tick_overruns_total", static_cast<double>(m_tickOverruns.load()));
        }

        // 计算下一次唤醒时间
        nextWakeTime += expectedInterval;

        // 如果已经落后太多（超过一个周期），跳过补偿以避免级联延迟
        const auto now2 = Clock::now();
        if (nextWakeTime < now2) {
            const auto drift = now2 - nextWakeTime;
            const auto driftMs = std::chrono::duration_cast<milliseconds>(drift).count();
            if (driftMs > interval) {
                qWarning().noquote() << "[Client][ControlLoopTicker] ★ TIMING DRIFT DETECTED ★"
                                      << "driftMs=" << driftMs
                                      << "resetting to current time";
                nextWakeTime = now2 + expectedInterval;
            }
        }

        lastTickTime = now;
    }

    qInfo() << "[Client][ControlLoopTicker][TickerThread] loop exited"
            << "totalTicks=" << tickCount;
}

ControlLoopTicker::PerfStats ControlLoopTicker::getPerfStats() const
{
    PerfStats stats;
    stats.tickIndex = m_index.load();
    stats.actualHz = m_actualHz.load();
    stats.jitterMs = m_jitterMs.load();
    stats.avgJitterMs = m_avgJitterMs.load();
    stats.tickMissedCount = m_tickMissedCount.load();
    stats.tickOverruns = m_tickOverruns.load();
    stats.lastTickTimestampNs = m_lastTickTimestampNs.load();
    return stats;
}

void ControlLoopTicker::resetPerfStats()
{
    m_index.store(0);
    m_actualHz.store(0.0);
    m_jitterMs.store(0.0);
    m_avgJitterMs.store(0.0);
    m_tickMissedCount.store(0);
    m_tickOverruns.store(0);
    m_lastTickTimestampNs.store(0);

    qInfo() << "[Client][ControlLoopTicker] perf stats reset";
}
