#ifndef CLIENT_CORE_NETWORKQUALITYAGGREGATOR_H
#define CLIENT_CORE_NETWORKQUALITYAGGREGATOR_H

#include <QHash>
#include <QObject>
#include <QTimer>

class VehicleStatus;
class NodeHealthChecker;

/**
 * 综合网络质量得到 0~1 分数，供状态机降级守卫与 DegradationManager 使用。
 *
 * V2 单一真源：链路侧指标一律从 VehicleStatus 读取（MQTT vehicle/status 解析入 VehicleStatus；
 * videoConnected 由 main 中 WebRtcStreamManager::anyConnected 写入 VehicleStatus）。本类不直接
 * 订阅 WebRtcClient，避免与 UI 聚合状态漂移。
 *
 * NodeHealthChecker 仅作登录前 HTTP 探测的补充信号。
 *
 * V1：呈现路径（如 DMA-BUF SceneGraph → CPU RGBA 回退）通过 noteMediaPresentationDegraded
 * 拉低短期乘子，使 DegradationManager / FSM 的 NETWORK_DEGRADE 与媒体质量显式绑定。
 *
 * 多流策略（环境变量 CLIENT_MEDIA_HEALTH_AGGREGATE）：
 *   weighted — 默认；主路 cam_front 权重更高，辅路降级对综合分影响较小。
 *   min — 取各逻辑槽位因子最小值（最保守，一路坏等价于全局乘子被拉低）。
 */
class NetworkQualityAggregator : public QObject {
  Q_OBJECT
  Q_PROPERTY(double score READ score NOTIFY scoreChanged)
  Q_PROPERTY(bool degraded READ degraded NOTIFY degradedChanged)
  Q_PROPERTY(double rttMs READ rttMs NOTIFY networkQualityChanged)
  Q_PROPERTY(double packetLossRate READ packetLossRate NOTIFY networkQualityChanged)
  Q_PROPERTY(double bandwidthKbps READ bandwidthKbps NOTIFY networkQualityChanged)
  Q_PROPERTY(double jitterMs READ jitterMs NOTIFY networkQualityChanged)
  Q_PROPERTY(double mediaPresentationFactor READ mediaPresentationFactor NOTIFY
                 mediaPresentationFactorChanged)
  Q_PROPERTY(QString mediaHealthAggregateMode READ mediaHealthAggregateMode CONSTANT)

 public:
  explicit NetworkQualityAggregator(VehicleStatus *vehicleStatus,
                                    NodeHealthChecker *nodeHealthChecker,
                                    QObject *parent = nullptr);

  double score() const { return m_score; }
  bool degraded() const { return m_degraded; }
  double rttMs() const { return m_rttMs; }
  double packetLossRate() const { return m_packetLossRate; }
  double bandwidthKbps() const { return m_bandwidthKbps; }
  double jitterMs() const { return m_jitterMs; }
  /** 聚合后的呈现乘子，已乘入各 cam 槽位 penalty（见 CLIENT_MEDIA_HEALTH_AGGREGATE） */
  double mediaPresentationFactor() const { return m_mediaPresentationFactor; }
  QString mediaHealthAggregateMode() const { return m_mediaAggModeLabel; }

 public slots:
  /**
   * 任一路视频呈现降级（如 DMA-BUF SG 失败切 CPU）时调用；按槽位记录惩罚截止时间。
   * streamTag 通常为 {vin}_cam_front 等，用于映射到 cam_front / cam_rear / …
   */
  void noteMediaPresentationDegraded(const QString &streamTag, const QString &reason);

 signals:
  void scoreChanged(double score);
  void degradedChanged(bool degraded);
  void networkQualityChanged(double score, double rttMs, double packetLossRate,
                             double bandwidthKbps, double jitterMs);
  void mediaPresentationFactorChanged(double factor);

 private slots:
  void recompute();
  void onNodeBackendChanged();
  void onMediaSlotsPoll();

 private:
  void pruneExpiredSlotPenalties();
  double slotFactorNow(const QString &logicalSlot) const;
  double computeMediaAggregateFromSlots();
  void ensureMediaPollActive();
  double weightForLogicalSlot(const QString &logicalSlot) const;

  VehicleStatus *m_vs = nullptr;
  NodeHealthChecker *m_nodes = nullptr;
  double m_score = 1.0;
  bool m_degraded = false;
  bool m_backendOk = true;
  double m_rttMs = 0.0;
  double m_packetLossRate = 0.0;
  double m_bandwidthKbps = 0.0;
  double m_jitterMs = 0.0;
  double m_mediaPresentationFactor = 1.0;
  QString m_mediaAggModeLabel = QStringLiteral("weighted");
  bool m_mediaUseMinAggregate = false;
  double m_frontSlotWeight = 2.0;

  /** 逻辑槽位 -> 惩罚结束时间（ms epoch）；未出现或已过期视为因子 1.0 */
  QHash<QString, qint64> m_slotPenaltyUntilMs;
  QTimer m_mediaSlotsPollTimer;
};

#endif  // CLIENT_CORE_NETWORKQUALITYAGGREGATOR_H
