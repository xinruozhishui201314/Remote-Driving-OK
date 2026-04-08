#include "metricscollector.h"
#include <QDateTime>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QJsonObject>
#include <QJsonValue>

// 前向声明 Configuration
class Configuration;
#include "configuration.h"

MetricsCollector::MetricsCollector(QObject* parent)
    : QObject(parent)
    , m_startTimeMs(QDateTime::currentMSecsSinceEpoch())
{
    qInfo() << "[MetricsCollector] Initialized, start time:" << m_startTimeMs;

    // 初始化默认阈值
    setThreshold(Metrics::VIDEO_LATENCY_MS, 150.0, 300.0);
    setThreshold(Metrics::NETWORK_RTT_MS, 100.0, 250.0);
    setThreshold(Metrics::NETWORK_PACKET_LOSS_PERCENT, 5.0, 10.0);
    setThreshold(Metrics::CONTROL_LATENCY_MS, 20.0, 50.0);
    setThreshold(Metrics::VIDEO_FPS, 15.0, 5.0, true);  // FPS 越低越危险

    // 从 Configuration 加载阈值配置
    loadThresholdsFromConfig();
}

void MetricsCollector::set(const QString& name, double value)
{
    {
        QMutexLocker locker(&m_mutex);

        auto it = m_metrics.find(name);
        if (it == m_metrics.end()) {
            MetricValue mv;
            mv.type = MetricType::Gauge;
            mv.value = value;
            mv.minValue = value;
            mv.maxValue = value;
            mv.sum = value;
            mv.count = 1;
            mv.lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
            m_metrics[name] = QVariant::fromValue(mv);
        } else {
            QVariant var = it.value();
            MetricValue mv = var.value<MetricValue>();
            mv.value = value;
            mv.lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
            it.value() = QVariant::fromValue(mv);
        }
    }

    emit metricUpdated(name, value);
    checkThresholds(name, value);
}

void MetricsCollector::increment(const QString& name, double delta)
{
    QMutexLocker locker(&m_mutex);

    auto it = m_metrics.find(name);
    if (it == m_metrics.end()) {
        MetricValue mv;
        mv.type = MetricType::Counter;
        mv.value = delta;
        mv.lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
        m_metrics[name] = QVariant::fromValue(mv);
    } else {
        QVariant var = it.value();
        MetricValue mv = var.value<MetricValue>();
        mv.value += delta;
        mv.lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
        it.value() = QVariant::fromValue(mv);
    }

    emit metricUpdated(name, delta);
}

void MetricsCollector::observe(const QString& name, double value)
{
    QMutexLocker locker(&m_mutex);

    auto it = m_metrics.find(name);
    if (it == m_metrics.end()) {
        MetricValue mv;
        mv.type = MetricType::Histogram;
        mv.value = value;
        mv.minValue = value;
        mv.maxValue = value;
        mv.sum = value;
        mv.count = 1;
        mv.lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
        m_metrics[name] = QVariant::fromValue(mv);
    } else {
        QVariant var = it.value();
        MetricValue mv = var.value<MetricValue>();
        mv.value = value;
        mv.minValue = qMin(mv.minValue, value);
        mv.maxValue = qMax(mv.maxValue, value);
        mv.sum += value;
        mv.count += 1;
        mv.lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
        it.value() = QVariant::fromValue(mv);
    }

    emit metricUpdated(name, value);
}

double MetricsCollector::get(const QString& name) const
{
    QMutexLocker locker(&m_mutex);

    auto it = m_metrics.find(name);
    if (it != m_metrics.end()) {
        MetricValue mv = it.value().value<MetricValue>();
        return mv.value;
    }
    return 0.0;
}

QVariantMap MetricsCollector::getAllMetrics() const
{
    QMutexLocker locker(&m_mutex);

    QVariantMap result;
    for (auto it = m_metrics.begin(); it != m_metrics.end(); ++it) {
        MetricValue mv = it.value().value<MetricValue>();

        QVariantMap metricData;
        switch (mv.type) {
        case MetricType::Counter:
            metricData["type"] = "counter";
            metricData["value"] = mv.value;
            break;
        case MetricType::Gauge:
            metricData["type"] = "gauge";
            metricData["value"] = mv.value;
            break;
        case MetricType::Histogram:
            metricData["type"] = "histogram";
            metricData["value"] = mv.value;
            metricData["min"] = mv.minValue;
            metricData["max"] = mv.maxValue;
            metricData["avg"] = mv.count > 0 ? mv.sum / static_cast<double>(mv.count) : 0.0;
            metricData["count"] = static_cast<qulonglong>(mv.count);
            break;
        }
        metricData["lastUpdateMs"] = static_cast<qlonglong>(mv.lastUpdateMs);

        result[it.key()] = metricData;
    }

    // 添加系统指标
    result["_system_uptime_ms"] = static_cast<qlonglong>(QDateTime::currentMSecsSinceEpoch() - m_startTimeMs);
    result["_system_start_time_ms"] = static_cast<qlonglong>(m_startTimeMs);

    return result;
}

QString MetricsCollector::getPrometheusFormat() const
{
    QString output;
    QTextStream stream(&output);

    QMutexLocker locker(&m_mutex);

    for (auto it = m_metrics.begin(); it != m_metrics.end(); ++it) {
        MetricValue mv = it.value().value<MetricValue>();
        const QString& name = it.key();

        // 转换指标名称为 Prometheus 格式（驼峰转下划线）
        QString promName = name;
        promName.replace(QRegularExpression("([A-Z])"), "_\\1");
        promName = promName.toLower();
        if (promName.startsWith("_")) {
            promName = promName.mid(1);
        }

        switch (mv.type) {
        case MetricType::Counter:
            stream << "# TYPE " << promName << " counter\n";
            stream << promName << "_total " << QString::number(mv.value, 'f', 2) << "\n";
            break;
        case MetricType::Gauge:
            stream << "# TYPE " << promName << " gauge\n";
            stream << promName << " " << QString::number(mv.value, 'f', 2) << "\n";
            break;
        case MetricType::Histogram:
            stream << "# TYPE " << promName << " histogram\n";
            stream << promName << "_sum " << QString::number(mv.sum, 'f', 2) << "\n";
            stream << promName << "_count " << mv.count << "\n";
            stream << promName << "_avg " << QString::number(mv.count > 0 ? mv.sum / static_cast<double>(mv.count) : 0.0, 'f', 2) << "\n";
            stream << promName << "_min " << QString::number(mv.minValue, 'f', 2) << "\n";
            stream << promName << "_max " << QString::number(mv.maxValue, 'f', 2) << "\n";
            break;
        }
    }

    return output;
}

void MetricsCollector::setThreshold(const QString& name, double warning, double critical, bool lowerIsWorse)
{
    QMutexLocker locker(&m_thresholdMutex);

    Threshold t;
    t.warning = warning;
    t.critical = critical;
    t.lowerIsWorse = lowerIsWorse;
    m_thresholds[name] = t;

    qInfo() << "[MetricsCollector] Threshold set for" << name
            << "warning=" << warning << "critical=" << critical
            << "lowerIsWorse=" << lowerIsWorse;

    emit thresholdChanged(name, warning, critical);
}

MetricsCollector::Threshold MetricsCollector::getThreshold(const QString& name) const
{
    QMutexLocker locker(&m_thresholdMutex);
    return m_thresholds.value(name);
}

QVariantMap MetricsCollector::getThresholds() const
{
    QMutexLocker locker(&m_thresholdMutex);

    QVariantMap result;
    for (auto it = m_thresholds.begin(); it != m_thresholds.end(); ++it) {
        QVariantMap thresholdData;
        thresholdData["warning"] = it.value().warning;
        thresholdData["critical"] = it.value().critical;
        thresholdData["lowerIsWorse"] = it.value().lowerIsWorse;
        result[it.key()] = thresholdData;
    }
    return result;
}

void MetricsCollector::loadThresholdsFromConfig()
{
    // 尝试从 Configuration 加载阈值配置
    // 配置格式: metrics.thresholds.<metric_name>.warning / .critical
    try {
        auto& config = Configuration::instance();

        // 定义可配置的阈值键名映射
        QMap<QString, QStringList> thresholdKeys = {
            {Metrics::VIDEO_LATENCY_MS, {"metrics.thresholds.video_latency_ms.warning", "metrics.thresholds.video_latency_ms.critical"}},
            {Metrics::VIDEO_FPS, {"metrics.thresholds.video_fps.warning", "metrics.thresholds.video_fps.critical"}},
            {Metrics::NETWORK_RTT_MS, {"metrics.thresholds.network_rtt_ms.warning", "metrics.thresholds.network_rtt_ms.critical"}},
            {Metrics::NETWORK_PACKET_LOSS_PERCENT, {"metrics.thresholds.network_packet_loss_percent.warning", "metrics.thresholds.network_packet_loss_percent.critical"}},
            {Metrics::CONTROL_LATENCY_MS, {"metrics.thresholds.control_latency_ms.warning", "metrics.thresholds.control_latency_ms.critical"}},
        };

        for (auto it = thresholdKeys.begin(); it != thresholdKeys.end(); ++it) {
            const QString& metricName = it.key();
            const QStringList& keys = it.value();

            if (keys.size() < 2) {
                continue;
            }

            // 通过 Configuration 的 get 方法获取阈值
            double warning = config.get<double>(keys[0], std::numeric_limits<double>::quiet_NaN());
            double critical = config.get<double>(keys[1], std::numeric_limits<double>::quiet_NaN());

            if (!std::isnan(warning) && !std::isnan(critical)) {
                bool lowerIsWorse = (metricName == Metrics::VIDEO_FPS);
                setThreshold(metricName, warning, critical, lowerIsWorse);
                qInfo() << "[MetricsCollector] Loaded threshold from config:" << metricName
                        << "warning=" << warning << "critical=" << critical;
            }
        }
    } catch (const std::exception& e) {
        qWarning() << "[MetricsCollector] Failed to load thresholds from config:" << e.what();
    } catch (...) {
        // Configuration 可能未初始化，忽略错误
    }
}

void MetricsCollector::checkThresholds(const QString& name, double value)
{
    QMutexLocker locker(&m_thresholdMutex);

    if (!m_thresholds.contains(name)) {
        return;
    }

    const Threshold& t = m_thresholds[name];

    bool exceeded = false;
    double thresholdValue = 0.0;
    bool isWarning = false;  // 是否达到 warning 级别

    if (t.lowerIsWorse) {
        // FPS 等指标，越低越危险
        // 优先级：critical > warning > normal
        if (value < t.critical) {
            exceeded = true;
            thresholdValue = t.critical;
        } else if (value < t.warning) {
            // 在 warning 和 critical 之间：只是警告，不是严重
            isWarning = true;
            thresholdValue = t.warning;
        } else {
            // value >= t.warning：正常范围
            thresholdValue = t.warning;
        }
    } else {
        // 延迟等指标，越高越危险
        // 优先级：critical > warning > normal
        if (value > t.critical) {
            exceeded = true;
            thresholdValue = t.critical;
        } else if (value > t.warning) {
            // 在 warning 和 critical 之间：只是警告，不是严重
            isWarning = true;
            thresholdValue = t.warning;
        } else {
            // value <= t.warning：正常范围
            thresholdValue = t.warning;
        }
    }

    if (exceeded) {
        emit metricThresholdExceeded(name, value, t.critical);
        qWarning() << "[MetricsCollector] Metric exceeded critical threshold:"
                   << name << "=" << value << "threshold=" << t.critical;
    } else if (isWarning) {
        // 单独发射 warning 级别信号
        emit metricThresholdWarning(name, value, thresholdValue);
        qInfo() << "[MetricsCollector] Metric warning threshold:"
                << name << "=" << value << "threshold=" << thresholdValue;
    }
}

void MetricsCollector::resetPeriodicMetrics()
{
    // 重置周期性的 Gauge 指标（通常是瞬时值）
    QMutexLocker locker(&m_mutex);

    QStringList periodicMetrics = {
        Metrics::VIDEO_FPS,
        Metrics::VIDEO_LATENCY_MS,
        Metrics::CONTROL_RPS,
        Metrics::CONTROL_LATENCY_MS,
        Metrics::NETWORK_RTT_MS,
        Metrics::NETWORK_PACKET_LOSS_PERCENT,
        Metrics::NETWORK_BANDWIDTH_KBPS,
        Metrics::SYSTEM_CPU_PERCENT,
    };

    for (const QString& name : periodicMetrics) {
        auto it = m_metrics.find(name);
        if (it != m_metrics.end()) {
            QVariant var = it.value();
            MetricValue mv = var.value<MetricValue>();
            // Counter 不重置，Gauge 重置为 0
            if (mv.type != MetricType::Counter) {
                mv.value = 0.0;
            }
            it.value() = QVariant::fromValue(mv);
        }
    }
}
