#include "performancemonitor.h"

#include <QDebug>

PerformanceMonitor::PerformanceMonitor(QObject* parent)
    : QObject(parent),
      m_reportTimer(),
      m_videoE2EStats(),
      m_controlRTTStats(),
      m_uiFrameStats(),
      m_decodeStats(),
      m_videoFps(),
      m_controlHz(),
      m_packetLossRate(0.0),
      m_droppedFrames(0),
      m_decoderErrors(0) {
  m_reportTimer.setParent(this);
  connect(&m_reportTimer, &QTimer::timeout, this, &PerformanceMonitor::collectAndReport);
}

void PerformanceMonitor::start(int reportIntervalMs) {
  m_reportTimer.setInterval(reportIntervalMs);
  m_reportTimer.start();
  qInfo() << "[Client][PerformanceMonitor] started, report interval=" << reportIntervalMs << "ms";
}

void PerformanceMonitor::stop() { m_reportTimer.stop(); }

void PerformanceMonitor::recordVideoE2E(int64_t latencyUs) { m_videoE2EStats.addSample(latencyUs); }

void PerformanceMonitor::recordControlRTT(int64_t rttUs) { m_controlRTTStats.addSample(rttUs); }

void PerformanceMonitor::recordUIFrameTime(int64_t frameTimeUs) {
  m_uiFrameStats.addSample(frameTimeUs);
}

void PerformanceMonitor::recordDecodeTime(int64_t decodeTimeUs) {
  m_decodeStats.addSample(decodeTimeUs);
}

void PerformanceMonitor::recordVideoFrame() { m_videoFps.tick(TimeUtils::wallClockMs()); }

void PerformanceMonitor::recordControlTick() { m_controlHz.tick(TimeUtils::wallClockMs()); }

void PerformanceMonitor::recordPacketLoss(double rate) { m_packetLossRate = rate; }

void PerformanceMonitor::recordDroppedFrame() { ++m_droppedFrames; }

void PerformanceMonitor::recordDecoderError() { ++m_decoderErrors; }

PerformanceMonitor::Metrics PerformanceMonitor::currentMetrics() const {
  Metrics m;
  m.latency.videoE2EMs = m_videoE2EStats.p50() / 1000.0;
  m.latency.controlRTTMs = m_controlRTTStats.p50() / 1000.0;
  m.latency.uiFrameTimeMs = m_uiFrameStats.p99() / 1000.0;
  m.latency.decodeTimeMs = m_decodeStats.p50() / 1000.0;
  m.throughput.videoFps = m_videoFps.currentFps(TimeUtils::wallClockMs());
  m.throughput.controlHz = m_controlHz.currentFps(TimeUtils::wallClockMs(), 1000);
  m.quality.packetLossRate = m_packetLossRate;
  m.quality.droppedFrames = m_droppedFrames;
  m.quality.decoderErrors = m_decoderErrors;
  return m;
}

void PerformanceMonitor::collectAndReport() {
  Metrics m = currentMetrics();

  if (m.latency.uiFrameTimeMs > kMaxUIFrameMs) {
    const QString msg = QString("[Client][PerformanceMonitor] UI frame time P99 %1ms > %2ms")
                            .arg(m.latency.uiFrameTimeMs, 0, 'f', 1)
                            .arg(kMaxUIFrameMs, 0, 'f', 1);
    qWarning().noquote() << msg;
    emit performanceAlert(msg);
  }
  if (m.latency.videoE2EMs > kMaxVideoE2EMs) {
    const QString msg = QString("[Client][PerformanceMonitor] video E2E latency %1ms > %2ms")
                            .arg(m.latency.videoE2EMs, 0, 'f', 1)
                            .arg(kMaxVideoE2EMs);
    qWarning().noquote() << msg;
    emit performanceAlert(msg);
  }
  if (m.latency.controlRTTMs > kMaxControlRTTMs) {
    const QString msg = QString("[Client][PerformanceMonitor] control RTT %1ms > %2ms")
                            .arg(m.latency.controlRTTMs, 0, 'f', 1)
                            .arg(kMaxControlRTTMs);
    qWarning().noquote() << msg;
    emit performanceAlert(msg);
  }

  emit metricsUpdated(m);
}
