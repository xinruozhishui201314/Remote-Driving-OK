#include "NetworkStatusModel.h"

NetworkStatusModel::NetworkStatusModel(QObject* parent)
    : QObject(parent)
{}

void NetworkStatusModel::updateQuality(const NetworkQuality& quality)
{
    const bool wasDegraded = m_quality.degraded;
    m_quality = quality;
    emit networkChanged();
    if (quality.degraded != wasDegraded) {
        emit degradedChanged(quality.degraded);
    }
}

void NetworkStatusModel::setDegradationLevel(int level)
{
    if (m_degradationLevel != level) {
        m_degradationLevel = level;
        emit degradationLevelChanged(level);
    }
}

QString NetworkStatusModel::qualityText() const
{
    const double score = m_quality.score;
    if (score >= 0.9) return QStringLiteral("优秀");
    if (score >= 0.75) return QStringLiteral("良好");
    if (score >= 0.60) return QStringLiteral("一般");
    if (score >= 0.45) return QStringLiteral("较差");
    if (score >= 0.30) return QStringLiteral("很差");
    return QStringLiteral("极差");
}
