#include "healthchecker.h"
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QThread>
#include <QSysInfo>

HealthChecker::HealthChecker(QObject* parent)
    : QObject(parent)
{
    // 初始化启动时间
    m_startTimeMs = QDateTime::currentMSecsSinceEpoch();

    // 初始化组件默认超时
    ComponentHealth defaultHealth;
    defaultHealth.timeoutMs = 30000;  // 默认 30s 超时
    defaultHealth.timeoutEnabled = true;

    // 设置超时检测定时器
    m_timeoutCheckTimer.setInterval(5000);  // 5秒检查一次
    m_timeoutCheckTimer.setSingleShot(false);
    connect(&m_timeoutCheckTimer, &QTimer::timeout, this, &HealthChecker::checkTimeouts);
}

void HealthChecker::recordStartTime()
{
    m_startTimeMs = QDateTime::currentMSecsSinceEpoch();
    qInfo() << "[HealthChecker] System started, uptime tracking enabled";
    m_timeoutCheckTimer.start();  // 启动超时检测定时器
}

int64_t HealthChecker::getUptimeMs() const
{
    return QDateTime::currentMSecsSinceEpoch() - m_startTimeMs;
}

HealthChecker::SystemHealth HealthChecker::checkHealth() const
{
    SystemHealth health;

    // 计算总体状态
    health.overallStatus = computeOverallStatus();
    health.currentState = m_currentState;
    health.uptimeMs = getUptimeMs();

    // 收集组件状态
    health.videoDecoderHealthy = [&]() -> bool {
        auto it = m_components.find("video_decoder");
        if (it != m_components.end()) {
            return it.value().toMap()["status"].toInt() == static_cast<int>(Status::Healthy);
        }
        return false;
    }();

    health.mqttConnected = [&]() -> bool {
        auto it = m_components.find("mqtt");
        if (it != m_components.end()) {
            return it.value().toMap()["status"].toInt() == static_cast<int>(Status::Healthy);
        }
        return false;
    }();

    health.controlLoopActive = [&]() -> bool {
        auto it = m_components.find("control_loop");
        if (it != m_components.end()) {
            return it.value().toMap()["status"].toInt() == static_cast<int>(Status::Healthy);
        }
        return false;
    }();

    health.gpuAccelerated = [&]() -> bool {
        auto it = m_components.find("gpu");
        if (it != m_components.end()) {
            return it.value().toMap()["status"].toInt() == static_cast<int>(Status::Healthy);
        }
        return false;
    }();

    health.networkQualityOk = [&]() -> bool {
        auto it = m_components.find("network");
        if (it != m_components.end()) {
            return it.value().toMap()["status"].toInt() == static_cast<int>(Status::Healthy);
        }
        return false;
    }();

    health.componentStatus = m_components;

    // 收集内存信息
    health.memoryUsedBytes = m_memoryUsedBytes;
    health.memoryTotalBytes = m_memoryTotalBytes;
    if (m_memoryTotalBytes > 0) {
        health.memoryUsagePercent = 100.0 * static_cast<double>(m_memoryUsedBytes) / m_memoryTotalBytes;
    }

    // 收集线程信息
    health.threadCount = QThread::idealThreadCount();

    return health;
}

HealthChecker::SystemReadiness HealthChecker::checkReadiness() const
{
    SystemReadiness readiness;

    readiness.ready = computeReadiness();
    readiness.livenessOk = (getUptimeMs() > 0);  // 只要进程还活着就认为存活

    // 检查各组件就绪状态
    QStringList reasons;

    // 检查视频解码器
    auto videoIt = m_components.find("video_decoder");
    if (videoIt == m_components.end() ||
        videoIt.value().toMap()["status"].toInt() != static_cast<int>(Status::Healthy)) {
        reasons.append("Video decoder not healthy");
    }

    // 检查 MQTT 连接
    auto mqttIt = m_components.find("mqtt");
    if (mqttIt == m_components.end() ||
        mqttIt.value().toMap()["status"].toInt() != static_cast<int>(Status::Healthy)) {
        reasons.append("MQTT not connected");
    }

    // 检查控制环路
    auto ctrlIt = m_components.find("control_loop");
    if (ctrlIt == m_components.end() ||
        ctrlIt.value().toMap()["status"].toInt() != static_cast<int>(Status::Healthy)) {
        reasons.append("Control loop not active");
    }

    readiness.unreadyReasons = reasons;

    // 详细信息
    readiness.details["overall_status"] = QVariant::fromValue(static_cast<int>(computeOverallStatus()));
    readiness.details["uptime_ms"] = QVariant::fromValue(static_cast<qlonglong>(getUptimeMs()));
    readiness.details["component_count"] = m_components.size();

    // 内存状态
    if (m_memoryTotalBytes > 0) {
        readiness.details["memory_usage_percent"] = 100.0 * static_cast<double>(m_memoryUsedBytes) / m_memoryTotalBytes;
    }

    return readiness;
}

HealthChecker::Status HealthChecker::computeOverallStatus() const
{
    // 如果有任何组件处于 Unhealthy 状态，总体就是 Unhealthy
    for (auto it = m_components.begin(); it != m_components.end(); ++it) {
        auto map = it.value().toMap();
        if (map["status"].toInt() == static_cast<int>(Status::Unhealthy)) {
            return Status::Unhealthy;
        }
    }

    // 如果有任何组件处于 Degraded 状态，总体就是 Degraded
    for (auto it = m_components.begin(); it != m_components.end(); ++it) {
        auto map = it.value().toMap();
        if (map["status"].toInt() == static_cast<int>(Status::Degraded)) {
            return Status::Degraded;
        }
    }

    // 如果所有已知组件都是 Healthy 状态
    if (!m_components.isEmpty()) {
        bool allHealthy = true;
        for (auto it = m_components.begin(); it != m_components.end(); ++it) {
            auto map = it.value().toMap();
            if (map["status"].toInt() != static_cast<int>(Status::Healthy)) {
                allHealthy = false;
                break;
            }
        }
        if (allHealthy) {
            return Status::Healthy;
        }
    }

    return Status::Unknown;
}

bool HealthChecker::computeReadiness() const
{
    // K8s 就绪探针：至少 MQTT 连接就绪
    auto mqttIt = m_components.find("mqtt");
    if (mqttIt != m_components.end()) {
        return mqttIt.value().toMap()["status"].toInt() == static_cast<int>(Status::Healthy);
    }
    return false;
}

void HealthChecker::updateComponentHealth(const QString& component, Status status, const QString& message)
{
    QVariantMap map;
    map["status"] = static_cast<int>(status);
    map["message"] = message;
    map["lastCheckMs"] = QDateTime::currentMSecsSinceEpoch();
    map["lastUpdateMs"] = QDateTime::currentMSecsSinceEpoch();

    // 保留现有的超时配置
    auto it = m_components.find(component);
    if (it != m_components.end()) {
        auto oldMap = it.value().toMap();
        if (oldMap.contains("timeoutMs")) {
            map["timeoutMs"] = oldMap["timeoutMs"];
        } else {
            map["timeoutMs"] = 30000;
        }
        if (oldMap.contains("timeoutEnabled")) {
            map["timeoutEnabled"] = oldMap["timeoutEnabled"];
        } else {
            map["timeoutEnabled"] = true;
        }
    } else {
        map["timeoutMs"] = 30000;
        map["timeoutEnabled"] = true;
    }

    Status oldStatus = Status::Unknown;
    if (it != m_components.end()) {
        oldStatus = static_cast<Status>(it.value().toMap()["status"].toInt());
    }

    m_components[component] = map;

    // 发射状态变化信号
    if (oldStatus != status) {
        emit componentStatusChanged(component, status);

        Status overallStatus = computeOverallStatus();
        emit healthStatusChanged(overallStatus);

        bool ready = computeReadiness();
        emit readinessChanged(ready);

        qInfo() << "[HealthChecker] Component" << component
                << "status changed:" << static_cast<int>(oldStatus)
                << "->" << static_cast<int>(status)
                << "message:" << message;
    }
}

void HealthChecker::checkTimeouts()
{
    int64_t now = QDateTime::currentMSecsSinceEpoch();

    for (auto it = m_components.begin(); it != m_components.end(); ++it) {
        const QString& component = it.key();
        auto map = it.value().toMap();

        bool timeoutEnabled = map.contains("timeoutEnabled") ? map["timeoutEnabled"].toBool() : true;
        if (!timeoutEnabled) {
            continue;
        }

        int timeoutMs = map.contains("timeoutMs") ? map["timeoutMs"].toInt() : 30000;
        int64_t lastUpdateMs = map["lastUpdateMs"].toLongLong();

        if (lastUpdateMs > 0 && (now - lastUpdateMs) > timeoutMs) {
            // 组件超时，自动降级为 Degraded
            Status currentStatus = static_cast<Status>(map["status"].toInt());
            if (currentStatus == Status::Healthy) {
                QString message = QString("Component timeout: no update for %1ms").arg(now - lastUpdateMs);
                qWarning() << "[HealthChecker] Component" << component << "timeout,"
                           << "last update:" << lastUpdateMs << "ms ago, threshold:" << timeoutMs << "ms";

                emit componentTimeout(component, lastUpdateMs);
                updateComponentHealth(component, Status::Degraded, message);
            }
        }
    }
}

void HealthChecker::checkComponentTimeout(const QString& component)
{
    auto it = m_components.find(component);
    if (it == m_components.end()) {
        return;
    }

    auto map = it.value().toMap();
    bool timeoutEnabled = map.contains("timeoutEnabled") ? map["timeoutEnabled"].toBool() : true;
    if (!timeoutEnabled) {
        return;
    }

    int64_t now = QDateTime::currentMSecsSinceEpoch();
    int timeoutMs = map.contains("timeoutMs") ? map["timeoutMs"].toInt() : 30000;
    int64_t lastUpdateMs = map["lastUpdateMs"].toLongLong();

    if (lastUpdateMs > 0 && (now - lastUpdateMs) > timeoutMs) {
        Status currentStatus = static_cast<Status>(map["status"].toInt());
        if (currentStatus == Status::Healthy) {
            QString message = QString("Component timeout: no update for %1ms").arg(now - lastUpdateMs);
            qWarning() << "[HealthChecker] Component" << component << "timeout,"
                       << "last update:" << lastUpdateMs << "ms ago";

            emit componentTimeout(component, lastUpdateMs);
            updateComponentHealth(component, Status::Degraded, message);
        }
    }
}

void HealthChecker::updateComponentTimeout(const QString& component, int timeoutMs)
{
    auto it = m_components.find(component);
    if (it != m_components.end()) {
        QVariantMap map = it.value().toMap();
        map["timeoutMs"] = timeoutMs;
        m_components[component] = map;
        qInfo() << "[HealthChecker] Updated timeout for" << component << "to" << timeoutMs << "ms";
    } else {
        ComponentHealth health;
        health.timeoutMs = timeoutMs;
        health.timeoutEnabled = true;
        QVariantMap map;
        map["timeoutMs"] = timeoutMs;
        map["timeoutEnabled"] = true;
        m_components[component] = map;
        qInfo() << "[HealthChecker] Created component" << component << "with timeout" << timeoutMs << "ms";
    }
}

int HealthChecker::getComponentTimeout(const QString& component) const
{
    auto it = m_components.find(component);
    if (it != m_components.end()) {
        return it.value().toMap()["timeoutMs"].toInt();
    }
    return 30000;  // 默认值
}

void HealthChecker::setComponentTimeoutEnabled(const QString& component, bool enabled)
{
    auto it = m_components.find(component);
    if (it != m_components.end()) {
        QVariantMap map = it.value().toMap();
        map["timeoutEnabled"] = enabled;
        m_components[component] = map;
        qInfo() << "[HealthChecker] Set timeout enabled for" << component << ":" << enabled;
    }
}

void HealthChecker::updateVideoDecoderStatus(bool healthy, const QString& message)
{
    Status status = healthy ? Status::Healthy : Status::Unhealthy;
    updateComponentHealth("video_decoder", status, message.isEmpty() ? (healthy ? "OK" : "Failed") : message);
}

void HealthChecker::updateMqttStatus(bool connected, const QString& message)
{
    Status status = connected ? Status::Healthy : Status::Unhealthy;
    updateComponentHealth("mqtt", status, message.isEmpty() ? (connected ? "Connected" : "Disconnected") : message);
}

void HealthChecker::updateControlLoopStatus(bool active, const QString& message)
{
    Status status = active ? Status::Healthy : Status::Unhealthy;
    updateComponentHealth("control_loop", status, message.isEmpty() ? (active ? "Active" : "Inactive") : message);
}

void HealthChecker::updateGpuStatus(bool accelerated, const QString& message)
{
    Status status = accelerated ? Status::Healthy : Status::Degraded;
    updateComponentHealth("gpu", status, message.isEmpty() ? (accelerated ? "GPU accelerated" : "CPU fallback") : message);
}

void HealthChecker::updateNetworkQuality(bool ok, const QString& message)
{
    Status status = ok ? Status::Healthy : Status::Degraded;
    updateComponentHealth("network", status, message.isEmpty() ? (ok ? "Quality OK" : "Quality degraded") : message);
}

void HealthChecker::updateOverallState(const QString& state)
{
    if (m_currentState != state) {
        m_currentState = state;
        qInfo() << "[HealthChecker] System state changed to:" << state;
    }
}

void HealthChecker::updateMemoryInfo(int64_t usedBytes, int64_t totalBytes)
{
    m_memoryUsedBytes = usedBytes;
    m_memoryTotalBytes = totalBytes;

    if (totalBytes > 0) {
        double usagePercent = 100.0 * static_cast<double>(usedBytes) / totalBytes;

        if (usagePercent > 95.0) {
            emit memoryCritical(usagePercent);
            qWarning() << "[HealthChecker] Memory critical:" << usagePercent << "%";
        } else if (usagePercent > 80.0) {
            emit memoryWarning(usagePercent);
            qInfo() << "[HealthChecker] Memory warning:" << usagePercent << "%";
        }
    }

    qDebug() << "[HealthChecker] Memory updated: used=" << usedBytes
             << "total=" << totalBytes;
}

QVariantMap HealthChecker::getMemoryInfo() const
{
    QVariantMap info;
    info["usedBytes"] = static_cast<qlonglong>(m_memoryUsedBytes);
    info["totalBytes"] = static_cast<qlonglong>(m_memoryTotalBytes);

    if (m_memoryTotalBytes > 0) {
        info["usagePercent"] = 100.0 * static_cast<double>(m_memoryUsedBytes) / m_memoryTotalBytes;
    } else {
        info["usagePercent"] = 0.0;
    }

    return info;
}

QVariantMap HealthChecker::getSystemMemoryUsage()
{
    QVariantMap info;

#ifdef Q_OS_LINUX
    // 读取 /proc/meminfo 获取系统内存信息
    QFile memFile("/proc/meminfo");
    if (memFile.open(QIODevice::ReadOnly)) {
        QTextStream stream(&memFile);
        QString line;
        int64_t memTotal = 0;
        int64_t memAvailable = 0;
        int64_t memFree = 0;
        int64_t buffers = 0;
        int64_t cached = 0;

        while (!(line = stream.readLine()).isNull()) {
            if (line.startsWith("MemTotal:")) {
                memTotal = line.split(' ', Qt::SkipEmptyParts)[1].toLongLong() * 1024;
            } else if (line.startsWith("MemAvailable:")) {
                memAvailable = line.split(' ', Qt::SkipEmptyParts)[1].toLongLong() * 1024;
            } else if (line.startsWith("MemFree:")) {
                memFree = line.split(' ', Qt::SkipEmptyParts)[1].toLongLong() * 1024;
            } else if (line.startsWith("Buffers:")) {
                buffers = line.split(' ', Qt::SkipEmptyParts)[1].toLongLong() * 1024;
            } else if (line.startsWith("Cached:")) {
                cached = line.split(' ', Qt::SkipEmptyParts)[1].toLongLong() * 1024;
            }
        }

        memFile.close();

        // 计算已用内存 = Total - Available（包含 buffers 和 cached）
        if (memAvailable == 0) {
            memAvailable = memFree + buffers + cached;
        }

        info["totalBytes"] = static_cast<qlonglong>(memTotal);
        info["availableBytes"] = static_cast<qlonglong>(memAvailable);
        info["usedBytes"] = static_cast<qlonglong>(memTotal - memAvailable);
        info["freeBytes"] = static_cast<qlonglong>(memAvailable);
        info["buffersBytes"] = static_cast<qlonglong>(buffers);
        info["cachedBytes"] = static_cast<qlonglong>(cached);

        if (memTotal > 0) {
            info["usagePercent"] = 100.0 * static_cast<double>(memTotal - memAvailable) / memTotal;
        }
    }
#elif defined(Q_OS_WIN) || defined(Q_OS_MAC)
    // Windows 和 macOS 使用 QSysInfo
    info["totalBytes"] = static_cast<qlonglong>(QSysInfo::machineHostName().size());  // 需要其他方式获取
    info["availableBytes"] = 0;
    info["usedBytes"] = 0;
    info["usagePercent"] = 0.0;
    qWarning() << "[HealthChecker] getSystemMemoryUsage: platform not fully supported";
#else
    info["totalBytes"] = 0;
    info["availableBytes"] = 0;
    info["usedBytes"] = 0;
    info["usagePercent"] = 0.0;
#endif

    return info;
}

void HealthChecker::updateThreadPoolStatus(int activeThreads, int totalThreads, int queueSize)
{
    m_threadPoolActiveThreads = activeThreads;
    m_threadPoolTotalThreads = totalThreads;
    m_threadPoolQueueSize = queueSize;
    m_threadPoolEnabled = true;

    qDebug() << "[HealthChecker] ThreadPool status: active=" << activeThreads
             << "total=" << totalThreads << "queue=" << queueSize;
}

QVariantMap HealthChecker::getThreadPoolStatus() const
{
    QVariantMap status;
    status["enabled"] = m_threadPoolEnabled;
    status["activeThreads"] = m_threadPoolActiveThreads;
    status["totalThreads"] = m_threadPoolTotalThreads;
    status["queueSize"] = m_threadPoolQueueSize;

    if (m_threadPoolTotalThreads > 0) {
        status["utilizationPercent"] = 100.0 * m_threadPoolActiveThreads / m_threadPoolTotalThreads;
    } else {
        status["utilizationPercent"] = 0.0;
    }

    return status;
}

void HealthChecker::updatePluginStatus(const QString& pluginName, bool loaded, const QString& version)
{
    QVariantMap pluginInfo;
    pluginInfo["loaded"] = loaded;
    pluginInfo["version"] = version;
    pluginInfo["lastUpdateMs"] = QDateTime::currentMSecsSinceEpoch();

    m_plugins[pluginName] = pluginInfo;
    m_pluginsEnabled = true;

    qInfo() << "[HealthChecker] Plugin" << pluginName
            << "status: loaded=" << loaded << "version=" << version;
}

QVariantMap HealthChecker::getAllPluginStatus() const
{
    QVariantMap result = m_plugins;
    result["_enabled"] = m_pluginsEnabled;
    return result;
}

void HealthChecker::incrementErrorCount(const QString& component)
{
    if (component == "video_decoder") {
        m_videoDecoderErrors.fetch_add(1);
    } else if (component == "mqtt") {
        m_mqttErrors.fetch_add(1);
    } else if (component == "control_loop") {
        m_controlErrors.fetch_add(1);
    } else if (component == "network") {
        m_networkErrors.fetch_add(1);
    }
}

void HealthChecker::resetErrorCount(const QString& component)
{
    if (component == "video_decoder") {
        m_videoDecoderErrors.store(0);
    } else if (component == "mqtt") {
        m_mqttErrors.store(0);
    } else if (component == "control_loop") {
        m_controlErrors.store(0);
    } else if (component == "network") {
        m_networkErrors.store(0);
    }
}

int HealthChecker::getErrorCount(const QString& component) const
{
    if (component == "video_decoder") {
        return m_videoDecoderErrors.load();
    } else if (component == "mqtt") {
        return m_mqttErrors.load();
    } else if (component == "control_loop") {
        return m_controlErrors.load();
    } else if (component == "network") {
        return m_networkErrors.load();
    }
    return 0;
}
