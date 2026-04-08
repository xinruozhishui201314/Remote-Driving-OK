#pragma once
#include <QObject>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <QMutex>

/**
 * 令牌桶限流器
 *
 * 使用令牌桶算法实现精确的速率限制
 * 线程安全，可跨线程调用
 */
class RateLimiter : public QObject {
    Q_OBJECT
public:
    /**
     * @param rate 每秒产生的令牌数
     * @param burst 桶容量（最大突发流量）
     */
    explicit RateLimiter(double rate, int burst, QObject* parent = nullptr);
    ~RateLimiter() override;

    // 尝试获取令牌（非阻塞）
    // 返回 true 表示允许，返回 false 表示被限流
    Q_INVOKABLE bool tryAcquire();

    // 等待获取令牌（阻塞直到可用或超时）
    // @param timeoutMs 超时时间，-1 表示无限等待
    // 返回 true 表示获取成功，false 表示超时
    Q_INVOKABLE bool acquire(int timeoutMs = -1);

    // 获取当前可用令牌数
    Q_INVOKABLE double availableTokens() const;

    // 获取被拒绝的请求数
    Q_INVOKABLE qint64 rejectedCount() const;

    // 重置统计
    Q_INVOKABLE void resetStats();

    // 动态调整速率
    Q_INVOKABLE void setRate(double rate);
    Q_INVOKABLE void setBurst(int burst);

    // 获取当前配置
    double rate() const { return m_rate; }
    int burst() const { return m_burst; }

signals:
    void rateLimitExceeded();
    void rateLimitReleased();

private:
    void refillTokens();
    double consumeTokens(double count);
    void startRefillTimer();

    double m_rate;           // 每秒令牌数
    int m_burst;             // 桶容量
    double m_tokens;         // 当前令牌数
    std::chrono::steady_clock::time_point m_lastRefill;

    mutable QMutex m_mutex;  // 保护令牌状态
    std::atomic<qint64> m_rejectedCount{0};
    std::atomic<bool> m_limited{false};
    std::atomic<bool> m_wasLimited{false};

    QTimer* m_refillTimer;   // 定期补充令牌
};

// 便利宏用于指标收集
#define RATE_LIMITER_SET(name, value) MetricsCollector::instance().set(name, value)
#define RATE_LIMITER_INC(name) MetricsCollector::instance().increment(name)