#include "networkqualityaggregator.h"

#include "../nodehealthchecker.h"
#include "../utils/TimeUtils.h"
#include "../vehiclestatus.h"
#include "metricscollector.h"

#include <QDebug>
#include <QProcessEnvironment>
#include <QtMath>

namespace {
constexpr double kMediaPenaltyTarget = 0.55;
constexpr int kDefaultMediaRecoveryMs = 20000;
constexpr int kMediaPollIntervalMs = 400;

int mediaRecoveryMsFromEnv() {
  bool ok = false;
  const int v =
      QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLIENT_MEDIA_HEALTH_RECOVERY_MS"))
          .toInt(&ok);
  if (!ok || v < 3000)
    return kDefaultMediaRecoveryMs;
  return qBound(3000, v, 120000);
}

QString mapStreamTagToLogicalSlot(const QString &streamTag) {
  const QString s = streamTag.toLower();
  if (s.contains(QLatin1String("cam_front")))
    return QStringLiteral("cam_front");
  if (s.contains(QLatin1String("cam_rear")))
    return QStringLiteral("cam_rear");
  if (s.contains(QLatin1String("cam_left")))
    return QStringLiteral("cam_left");
  if (s.contains(QLatin1String("cam_right")))
    return QStringLiteral("cam_right");
  return QStringLiteral("other");
}

const QStringList &logicalSlots() {
  static const QStringList k = {QStringLiteral("cam_front"), QStringLiteral("cam_rear"),
                                QStringLiteral("cam_left"), QStringLiteral("cam_right"),
                                QStringLiteral("other")};
  return k;
}
}  // namespace

NetworkQualityAggregator::NetworkQualityAggregator(VehicleStatus *vehicleStatus,
                                                   NodeHealthChecker *nodeHealthChecker,
                                                   QObject *parent)
    : QObject(parent),
      m_vs(vehicleStatus),
      m_nodes(nodeHealthChecker),
      m_score(1.0),
      m_degraded(false),
      m_backendOk(true),
      m_rttMs(0.0),
      m_packetLossRate(0.0),
      m_bandwidthKbps(0.0),
      m_jitterMs(0.0),
      m_videoE2EMs(0.0),
      m_mediaPresentationFactor(1.0),
      m_mediaAggModeLabel(QStringLiteral("weighted")),
      m_mediaUseMinAggregate(false),
      m_frontSlotWeight(2.0),
      m_slotPenaltyUntilMs(),
      m_mediaSlotsPollTimer() {
  m_mediaSlotsPollTimer.setInterval(kMediaPollIntervalMs);
  connect(&m_mediaSlotsPollTimer, &QTimer::timeout, this,
          &NetworkQualityAggregator::onMediaSlotsPoll);

  {
    const QString mode = QProcessEnvironment::systemEnvironment()
                             .value(QStringLiteral("CLIENT_MEDIA_HEALTH_AGGREGATE"))
                             .trimmed()
                             .toLower();
    m_mediaUseMinAggregate = (mode == QLatin1String("min"));
    m_mediaAggModeLabel = m_mediaUseMinAggregate ? QStringLiteral("min") : QStringLiteral("weighted");
  }
  {
    bool ok = false;
    const double w = QProcessEnvironment::systemEnvironment()
                         .value(QStringLiteral("CLIENT_MEDIA_HEALTH_WEIGHT_FRONT"))
                         .toDouble(&ok);
    if (ok && w >= 0.5 && w <= 10.0)
      m_frontSlotWeight = w;
  }

  if (m_vs) {
    connect(m_vs, &VehicleStatus::mqttConnectedChanged, this, &NetworkQualityAggregator::recompute);
    connect(m_vs, &VehicleStatus::videoConnectedChanged, this,
            &NetworkQualityAggregator::recompute);
    connect(m_vs, &VehicleStatus::networkRttChanged, this, &NetworkQualityAggregator::recompute);
    connect(m_vs, &VehicleStatus::networkPacketLossPercentChanged, this,
            &NetworkQualityAggregator::recompute);
    connect(m_vs, &VehicleStatus::networkBandwidthKbpsChanged, this,
            &NetworkQualityAggregator::recompute);
    connect(m_vs, &VehicleStatus::networkJitterMsChanged, this, &NetworkQualityAggregator::recompute);
  }
  if (m_nodes) {
    connect(m_nodes, &NodeHealthChecker::backendStatusChanged, this,
            &NetworkQualityAggregator::onNodeBackendChanged);
  }
  recompute();
}

void NetworkQualityAggregator::pruneExpiredSlotPenalties() {
  const qint64 now = TimeUtils::nowMs();
  for (auto it = m_slotPenaltyUntilMs.begin(); it != m_slotPenaltyUntilMs.end();) {
    if (now >= it.value())
      it = m_slotPenaltyUntilMs.erase(it);
    else
      ++it;
  }
}

double NetworkQualityAggregator::slotFactorNow(const QString &logicalSlot) const {
  auto it = m_slotPenaltyUntilMs.constFind(logicalSlot);
  if (it == m_slotPenaltyUntilMs.constEnd())
    return 1.0;
  if (TimeUtils::nowMs() >= it.value())
    return 1.0;
  return kMediaPenaltyTarget;
}

double NetworkQualityAggregator::weightForLogicalSlot(const QString &logicalSlot) const {
  if (logicalSlot == QLatin1String("cam_front"))
    return m_frontSlotWeight;
  return 1.0;
}

double NetworkQualityAggregator::computeMediaAggregateFromSlots() {
  if (m_mediaUseMinAggregate) {
    double m = 1.0;
    for (const QString &sid : logicalSlots())
      m = qMin(m, slotFactorNow(sid));
    return m;
  }
  double sumW = 0.0;
  double sumWF = 0.0;
  for (const QString &sid : logicalSlots()) {
    const double w = weightForLogicalSlot(sid);
    const double f = slotFactorNow(sid);
    sumW += w;
    sumWF += w * f;
  }
  return sumW > 1e-9 ? (sumWF / sumW) : 1.0;
}

void NetworkQualityAggregator::ensureMediaPollActive() {
  if (!m_mediaSlotsPollTimer.isActive())
    m_mediaSlotsPollTimer.start();
}

void NetworkQualityAggregator::noteMediaPresentationDegraded(const QString &streamTag,
                                                             const QString &reason) {
  const QString slot = mapStreamTagToLogicalSlot(streamTag);
  const qint64 until = TimeUtils::nowMs() + static_cast<qint64>(mediaRecoveryMsFromEnv());
  m_slotPenaltyUntilMs.insert(slot, until);

  MetricsCollector::instance().increment(
      QStringLiteral("client_media_presentation_degraded_total"));
  MetricsCollector::instance().increment(
      QStringLiteral("client_media_presentation_degraded_slot_%1_total").arg(slot));

  qWarning().noquote()
      << "[Client][NetworkQuality] media presentation penalty slot=" << slot
      << " stream=" << streamTag << " reason=" << reason << " aggregateMode=" << m_mediaAggModeLabel
      << " untilMs=" << until << " recoveryMs=" << mediaRecoveryMsFromEnv();

  ensureMediaPollActive();

  const double newAgg = computeMediaAggregateFromSlots();
  if (!qFuzzyCompare(1.0 + m_mediaPresentationFactor, 1.0 + newAgg)) {
    m_mediaPresentationFactor = newAgg;
    emit mediaPresentationFactorChanged(m_mediaPresentationFactor);
  }
  recompute();
}

void NetworkQualityAggregator::onMediaSlotsPoll() {
  const double prevAgg = m_mediaPresentationFactor;
  pruneExpiredSlotPenalties();
  const double newAgg = computeMediaAggregateFromSlots();
  if (!qFuzzyCompare(1.0 + prevAgg, 1.0 + newAgg)) {
    m_mediaPresentationFactor = newAgg;
    emit mediaPresentationFactorChanged(m_mediaPresentationFactor);
  }
  recompute();

  if (m_slotPenaltyUntilMs.isEmpty())
    m_mediaSlotsPollTimer.stop();
}

void NetworkQualityAggregator::onNodeBackendChanged() {
  if (!m_nodes)
    return;
  const QString s = m_nodes->backendStatus();
  m_backendOk = (s == tr("正常"));
  recompute();
}

void NetworkQualityAggregator::recompute() {
  double mqtt = 0.0;
  double video = 0.0;
  if (m_vs) {
    mqtt = m_vs->mqttConnected() ? 1.0 : 0.0;
    video = m_vs->videoConnected() ? 1.0 : 0.0;
    m_rttMs = m_vs->networkRtt();
    m_packetLossRate = m_vs->networkPacketLossPercent();
    m_bandwidthKbps = m_vs->networkBandwidthKbps();
    m_jitterMs = m_vs->networkJitterMs();
  }
  const double link = 0.5 * mqtt + 0.5 * video;
  double rttFactor = 1.0;
  if (m_rttMs > 1.0) {
    rttFactor = qMax(0.2, 1.0 - qMin(m_rttMs / 500.0, 0.8));
  }
  double lossFactor = 1.0;
  if (m_packetLossRate > 0.05) {
    lossFactor = qMax(0.12, 1.0 - qMin(m_packetLossRate / 30.0, 0.88));
  }
  double jitterFactor = 1.0;
  if (m_jitterMs > 1.0) {
    jitterFactor = qMax(0.2, 1.0 - qMin(m_jitterMs / 250.0, 0.8));
  }
  double bandwidthFactor = 1.0;
  if (m_bandwidthKbps > 10.0 && m_bandwidthKbps < 1200.0) {
    bandwidthFactor = qMax(0.35, m_bandwidthKbps / 1200.0);
  }
  const double backendFactor = m_backendOk ? 1.0 : 0.7;
  const double mediaFactor = qBound(0.08, m_mediaPresentationFactor, 1.0);
  const double newScore =
      qBound(0.0,
             link * rttFactor * lossFactor * jitterFactor * bandwidthFactor * backendFactor *
                 mediaFactor,
             1.0);
  const bool newDegraded = newScore < 0.45;

  if (!qFuzzyCompare(1.0 + m_score, 1.0 + newScore)) {
    m_score = newScore;
    emit scoreChanged(m_score);
    qInfo().noquote() << "[Client][NetworkQuality] recompute score=" << m_score
                      << " src=VehicleStatus mqtt=" << mqtt << " videoConnected=" << video
                      << " rttMs=" << m_rttMs << " loss%=" << m_packetLossRate
                      << " jitterMs=" << m_jitterMs << " bwKbps=" << m_bandwidthKbps
                      << " factors rtt=" << rttFactor << " loss=" << lossFactor << " jit=" << jitterFactor
                      << " bw=" << bandwidthFactor << " backendOk=" << m_backendOk
                      << " mediaFactor=" << mediaFactor << " mediaAgg=" << m_mediaAggModeLabel
                      << " linkHalf=(0.5*mqtt+0.5*video)";
  }
  if (m_degraded != newDegraded) {
    m_degraded = newDegraded;
    emit degradedChanged(m_degraded);
    qInfo().noquote() << "[Client][NetworkQuality] degraded 变更 →" << m_degraded
                      << " score=" << m_score << " videoConnected=" << video << " mqtt=" << mqtt;
  }

  emit networkQualityChanged(m_score, m_rttMs, m_packetLossRate, m_bandwidthKbps, m_jitterMs);
}

void NetworkQualityAggregator::setVideoE2EMs(double ms) {
  if (qAbs(m_videoE2EMs - ms) > 0.1) {
    m_videoE2EMs = ms;
    emit videoE2EChanged(m_videoE2EMs);
  }
}
