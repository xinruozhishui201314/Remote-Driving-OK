#include "diagnosticsservice.h"

#include "../utils/TimeUtils.h"

#include <QDebug>
#include <QSysInfo>

DiagnosticsService::DiagnosticsService(PerformanceMonitor* perf, QObject* parent)
    : QObject(parent), m_perfMonitor(perf), m_safety(nullptr), m_timer() {
  m_timer.setParent(this);
  connect(&m_timer, &QTimer::timeout, this, &DiagnosticsService::collect);
}

void DiagnosticsService::start(int intervalMs) {
  // 限制最小间隔为1秒，防止过于频繁的诊断收集
  int effectiveInterval = qMax(intervalMs, 1000);
  m_timer.setInterval(effectiveInterval);
  m_timer.start();
  qInfo() << "[Client][DiagnosticsService] started interval=" << effectiveInterval
          << "ms (requested=" << intervalMs << "ms)";
}

void DiagnosticsService::stop() { m_timer.stop(); }

QJsonObject DiagnosticsService::buildSnapshot() const {
  QJsonObject snap;
  snap["version"] = "1.0.0";
  snap["format_version"] = 1;
  snap["timestamp"] = static_cast<qint64>(TimeUtils::wallClockMs());

  if (m_perfMonitor) {
    const auto m = m_perfMonitor->currentMetrics();
    QJsonObject latency;
    latency["video_e2e_ms"] = m.latency.videoE2EMs;
    latency["control_rtt_ms"] = m.latency.controlRTTMs;
    latency["ui_frame_ms"] = m.latency.uiFrameTimeMs;
    latency["decode_ms"] = m.latency.decodeTimeMs;
    snap["latency"] = latency;

    QJsonObject throughput;
    throughput["video_fps"] = m.throughput.videoFps;
    throughput["control_hz"] = m.throughput.controlHz;
    snap["throughput"] = throughput;

    QJsonObject quality;
    quality["packet_loss"] = m.quality.packetLossRate;
    quality["dropped_frames"] = static_cast<int>(m.quality.droppedFrames);
    quality["decoder_errors"] = static_cast<int>(m.quality.decoderErrors);
    snap["quality"] = quality;
  }

  // System info
  QJsonObject sysInfo;
  sysInfo["kernel"] = QSysInfo::kernelType() + " " + QSysInfo::kernelVersion();
  sysInfo["cpu_arch"] = QSysInfo::currentCpuArchitecture();
  snap["system"] = sysInfo;

  // Safety monitor info
  if (m_safety) {
    QJsonObject safety;
    safety["available"] = true;
    snap["safety"] = safety;
  }

  return snap;
}

void DiagnosticsService::collect() {
  const QJsonObject snap = buildSnapshot();
  qDebug() << "[Client][DiagnosticsService] snapshot collected"
           << "ts=" << snap["timestamp"].toVariant().toLongLong();
  emit diagnosticsAvailable(snap);
}
