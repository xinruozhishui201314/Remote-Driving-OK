#pragma once
#include "../../infrastructure/itransportmanager.h"

#include <QObject>

/**
 * 网络状态 MVVM 模型（《客户端架构设计》§3.4.2）。
 */
class NetworkStatusModel : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(NetworkStatusModel)

  Q_PROPERTY(double rttMs READ rttMs NOTIFY networkChanged)
  Q_PROPERTY(double packetLossRate READ packetLossRate NOTIFY networkChanged)
  Q_PROPERTY(double bandwidthKbps READ bandwidthKbps NOTIFY networkChanged)
  Q_PROPERTY(double jitterMs READ jitterMs NOTIFY networkChanged)
  Q_PROPERTY(double qualityScore READ qualityScore NOTIFY networkChanged)
  Q_PROPERTY(bool degraded READ degraded NOTIFY degradedChanged)
  Q_PROPERTY(int degradationLevel READ degradationLevel NOTIFY degradationLevelChanged)
  Q_PROPERTY(QString qualityText READ qualityText NOTIFY networkChanged)

 public:
  explicit NetworkStatusModel(QObject* parent = nullptr);

  double rttMs() const { return m_quality.rttMs; }
  double packetLossRate() const { return m_quality.packetLossRate; }
  double bandwidthKbps() const { return m_quality.bandwidthKbps; }
  double jitterMs() const { return m_quality.jitterMs; }
  double qualityScore() const { return m_quality.score; }
  bool degraded() const { return m_quality.degraded; }
  int degradationLevel() const { return m_degradationLevel; }
  QString qualityText() const;

  void updateQuality(const NetworkQuality& quality);
  void setDegradationLevel(int level);

 signals:
  void networkChanged();
  void degradedChanged(bool degraded);
  void degradationLevelChanged(int level);

 private:
  NetworkQuality m_quality;
  int m_degradationLevel = 0;
};
