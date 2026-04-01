#include "diagnosticsservice.h"
#include "../utils/TimeUtils.h"
#include <QDebug>
#include <QSysInfo>

DiagnosticsService::DiagnosticsService(PerformanceMonitor* perf, QObject* parent)
    : QObject(parent)
    , m_perfMonitor(perf)
{
    connect(&m_timer, &QTimer::timeout, this, &DiagnosticsService::collect);
}

void DiagnosticsService::start(int intervalMs)
{
    m_timer.setInterval(intervalMs);
    m_timer.start();
    qInfo() << "[Client][DiagnosticsService] started interval=" << intervalMs << "ms";
}

void DiagnosticsService::stop()
{
    m_timer.stop();
}

QJsonObject DiagnosticsService::buildSnapshot() const
{
    QJsonObject snap;
    snap["timestamp"] = static_cast<qint64>(TimeUtils::wallClockMs());

    if (m_perfMonitor) {
        const auto m = m_perfMonitor->currentMetrics();
        QJsonObject latency;
        latency["video_e2e_ms"]   = m.latency.videoE2EMs;
        latency["control_rtt_ms"] = m.latency.controlRTTMs;
        latency["ui_frame_ms"]    = m.latency.uiFrameTimeMs;
        latency["decode_ms"]      = m.latency.decodeTimeMs;
        snap["latency"] = latency;

        QJsonObject throughput;
        throughput["video_fps"]    = m.throughput.videoFps;
        throughput["control_hz"]   = m.throughput.controlHz;
        snap["throughput"] = throughput;

        QJsonObject quality;
        quality["packet_loss"]    = m.quality.packetLossRate;
        quality["dropped_frames"] = static_cast<int>(m.quality.droppedFrames);
        quality["decoder_errors"] = static_cast<int>(m.quality.decoderErrors);
        snap["quality"] = quality;
    }

    // System info
    QJsonObject sysInfo;
    sysInfo["kernel"]   = QSysInfo::kernelType() + " " + QSysInfo::kernelVersion();
    sysInfo["cpu_arch"] = QSysInfo::currentCpuArchitecture();
    snap["system"] = sysInfo;

    return snap;
}

void DiagnosticsService::collect()
{
    const QJsonObject snap = buildSnapshot();
    qDebug() << "[Client][DiagnosticsService] snapshot collected"
             << "ts=" << snap["timestamp"].toVariant().toLongLong();
    emit diagnosticsAvailable(snap);
}
