#pragma once
#include <QObject>
#include <QVariantMap>
#include <QAtomicInt>
#include <QMutex>
#include <atomic>
#include <cstdint>

class Configuration;

/**
 * Prometheus 风格指标收集器（产品化关键组件）
 *
 * 功能：
 * - 收集关键系统指标
 * - 暴露 /metrics 端点数据
 * - 支持时间序列聚合（平均值、P95、最大值）
 * - 支持阈值可配置化
 *
 * 指标分类：
 * - Video: fps、延迟、丢帧率、渲染状态
 * - Control: 命令频率、错误率、响应时间
 * - Network: RTT、丢包率、带宽
 * - Safety: 急停次数、Deadman 触发次数、降级次数
 * - System: 内存、CPU、上传时间
 *
 * 使用方式：
 *   MetricsCollector::instance().increment("video_frames_total");
 *   MetricsCollector::instance().set("control_rps", 100.0);
 */
class MetricsCollector : public QObject {
    Q_OBJECT

public:
    static MetricsCollector& instance() {
        static MetricsCollector collector;
        return collector;
    }

    explicit MetricsCollector(QObject* parent = nullptr);
    ~MetricsCollector() override = default;

    // ═══════════════════════════════════════════════════════════════════════════════
    // 指标类型
    // ═══════════════════════════════════════════════════════════════════════════════

    enum class MetricType {
        Counter,   // 只增不减：总帧数、总命令数
        Gauge,     // 可增可减：当前 FPS、当前 RTT
        Histogram, // 分布：延迟分布、响应时间分布
    };

    struct MetricValue {
        MetricType type = MetricType::Counter;
        double value = 0.0;
        double minValue = 0.0;
        double maxValue = 0.0;
        double sum = 0.0;  // 用于计算平均值
        uint64_t count = 0;  // 用于计算平均值
        int64_t lastUpdateMs = 0;
    };

    // ═══════════════════════════════════════════════════════════════════════════════
    // 阈值配置结构
    // ═══════════════════════════════════════════════════════════════════════════════

    struct Threshold {
        double warning = 0.0;
        double critical = 0.0;
        bool lowerIsWorse = false;  // FPS 等指标越低越危险
    };

    // ═══════════════════════════════════════════════════════════════════════════════
    // 核心指标操作
    // ═══════════════════════════════════════════════════════════════════════════════

    // 设置 Gauge 指标值
    Q_INVOKABLE void set(const QString& name, double value);

    // 增加 Counter 指标值
    Q_INVOKABLE void increment(const QString& name, double delta = 1.0);

    // 记录 Histogram 指标值
    Q_INVOKABLE void observe(const QString& name, double value);

    // 获取指标值
    Q_INVOKABLE double get(const QString& name) const;

    // 获取所有指标（用于 /metrics 端点）
    Q_INVOKABLE QVariantMap getAllMetrics() const;

    // 获取 Prometheus 格式文本
    Q_INVOKABLE QString getPrometheusFormat() const;

    // ═══════════════════════════════════════════════════════════════════════════════
    // 阈值管理（可配置化）
    // ═══════════════════════════════════════════════════════════════════════════════

    // 设置指定指标的阈值
    Q_INVOKABLE void setThreshold(const QString& name, double warning, double critical, bool lowerIsWorse = false);

    // 获取所有阈值配置
    Q_INVOKABLE QVariantMap getThresholds() const;

    // 获取指定指标的阈值
    Q_INVOKABLE Threshold getThreshold(const QString& name) const;

    // 从 Configuration 加载阈值配置
    void loadThresholdsFromConfig();

    // ═══════════════════════════════════════════════════════════════════════════════
    // 预设指标名称常量
    // ═══════════════════════════════════════════════════════════════════════════════

    struct Metrics {
        // 视频指标
        static constexpr const char* VIDEO_FPS = "video_fps";
        static constexpr const char* VIDEO_LATENCY_MS = "video_latency_ms";
        static constexpr const char* VIDEO_FRAMES_TOTAL = "video_frames_total";
        static constexpr const char* VIDEO_DROPPED_TOTAL = "video_dropped_total";
        static constexpr const char* VIDEO_ERRORS_TOTAL = "video_errors_total";
        static constexpr const char* VIDEO_RENDER_FAILURES_TOTAL = "video_render_failures_total";

        // 控制指标
        static constexpr const char* CONTROL_COMMANDS_TOTAL = "control_commands_total";
        static constexpr const char* CONTROL_ERRORS_TOTAL = "control_errors_total";
        static constexpr const char* CONTROL_RPS = "control_rps";
        static constexpr const char* CONTROL_LATENCY_MS = "control_latency_ms";

        // 网络指标
        static constexpr const char* NETWORK_RTT_MS = "network_rtt_ms";
        static constexpr const char* NETWORK_PACKET_LOSS_PERCENT = "network_packet_loss_percent";
        static constexpr const char* NETWORK_BANDWIDTH_KBPS = "network_bandwidth_kbps";
        static constexpr const char* NETWORK_RECONNECTS_TOTAL = "network_reconnects_total";

        // 安全指标
        static constexpr const char* SAFETY_EMERGENCY_STOPS_TOTAL = "safety_emergency_stops_total";
        static constexpr const char* SAFETY_DEADMAN_TRIGGERS_TOTAL = "safety_deadman_triggers_total";
        static constexpr const char* SAFETY_LATENCY_VIOLATIONS_TOTAL = "safety_latency_violations_total";
        static constexpr const char* SAFETY_DEGRADATION_LEVEL = "safety_degradation_level";

        // 系统指标
        static constexpr const char* SYSTEM_MEMORY_BYTES = "system_memory_bytes";
        static constexpr const char* SYSTEM_CPU_PERCENT = "system_cpu_percent";
        static constexpr const char* SYSTEM_UPTIME_MS = "system_uptime_ms";
        static constexpr const char* SYSTEM_START_TIME_MS = "system_start_time_ms";
    };

signals:
    void metricUpdated(const QString& name, double value);
    void metricThresholdExceeded(const QString& name, double value, double threshold);
    void metricThresholdWarning(const QString& name, double value, double threshold);
    void thresholdChanged(const QString& name, double warning, double critical);

public slots:
    // 定期重置临时指标（由定时器调用）
    void resetPeriodicMetrics();

private:
    void updateMetric(const QString& name, MetricType type, double value);
    void checkThresholds(const QString& name, double value);

    mutable QMutex m_mutex;  // 使用类的成员 mutex
    mutable QMutex m_thresholdMutex;  // 阈值配置的锁
    QVariantMap m_metrics;
    QVariantMap m_histogramBuckets;  // Histogram bucket counts
    int64_t m_startTimeMs;

    // 可配置的阈值映射
    QMap<QString, Threshold> m_thresholds;
};
