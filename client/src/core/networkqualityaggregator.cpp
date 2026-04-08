#include "networkqualityaggregator.h"
#include "../vehiclestatus.h"
#include "../nodehealthchecker.h"
#include <QtMath>
#include <QDebug>

NetworkQualityAggregator::NetworkQualityAggregator(VehicleStatus *vehicleStatus,
                                                   NodeHealthChecker *nodeHealthChecker,
                                                   QObject *parent)
    : QObject(parent)
    , m_vs(vehicleStatus)
    , m_nodes(nodeHealthChecker)
{
    if (m_vs) {
        connect(m_vs, &VehicleStatus::mqttConnectedChanged, this, &NetworkQualityAggregator::recompute);
        connect(m_vs, &VehicleStatus::videoConnectedChanged, this, &NetworkQualityAggregator::recompute);
        connect(m_vs, &VehicleStatus::networkRttChanged, this, &NetworkQualityAggregator::recompute);
    }
    if (m_nodes) {
        connect(m_nodes, &NodeHealthChecker::backendStatusChanged, this,
                &NetworkQualityAggregator::onNodeBackendChanged);
    }
    recompute();
}

void NetworkQualityAggregator::onNodeBackendChanged()
{
    if (!m_nodes)
        return;
    const QString s = m_nodes->backendStatus();
    m_backendOk = (s == tr("正常"));
    recompute();
}

void NetworkQualityAggregator::recompute()
{
    double mqtt = 0.0;
    double video = 0.0;
    if (m_vs) {
        mqtt = m_vs->mqttConnected() ? 1.0 : 0.0;
        video = m_vs->videoConnected() ? 1.0 : 0.0;
        m_rttMs = m_vs->networkRtt();
        // TODO: 从 VehicleStatus 获取 packetLossRate, bandwidthKbps, jitterMs（需要 VehicleStatus 支持）
        // 目前暂时使用 0，后续 VehicleStatus 添加这些属性后更新
        m_packetLossRate = 0.0;
        m_bandwidthKbps = 0.0;
        m_jitterMs = 0.0;
    }
    const double link = 0.5 * mqtt + 0.5 * video;
    double rttFactor = 1.0;
    if (m_rttMs > 1.0) {
        rttFactor = qMax(0.2, 1.0 - qMin(m_rttMs / 500.0, 0.8));
    }
    const double backendFactor = m_backendOk ? 1.0 : 0.7;
    const double newScore = qBound(0.0, link * rttFactor * backendFactor, 1.0);
    const bool newDegraded = newScore < 0.45;

    if (!qFuzzyCompare(1.0 + m_score, 1.0 + newScore)) {
        m_score = newScore;
        emit scoreChanged(m_score);
        qInfo().noquote() << "[Client][NetworkQuality] recompute score=" << m_score
                          << " src=VehicleStatus mqtt=" << mqtt << " videoConnected=" << video
                          << " rttMs=" << m_rttMs << " backendOk=" << m_backendOk
                          << " linkHalf=(0.5*mqtt+0.5*video)";
    }
    if (m_degraded != newDegraded) {
        m_degraded = newDegraded;
        emit degradedChanged(m_degraded);
        qInfo().noquote() << "[Client][NetworkQuality] degraded 变更 →" << m_degraded
                          << " score=" << m_score << " videoConnected=" << video << " mqtt=" << mqtt;
    }

    // 发射完整网络质量信号（供 DegradationManager 使用）
    emit networkQualityChanged(m_score, m_rttMs, m_packetLossRate, m_bandwidthKbps, m_jitterMs);
}
