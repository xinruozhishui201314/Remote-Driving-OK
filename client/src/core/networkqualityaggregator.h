#ifndef CLIENT_CORE_NETWORKQUALITYAGGREGATOR_H
#define CLIENT_CORE_NETWORKQUALITYAGGREGATOR_H

#include <QObject>

class VehicleStatus;
class NodeHealthChecker;

/**
 * 综合 MQTT/视频连接与 RTT 得到 0~1 分数，供状态机降级守卫与 DegradationManager 使用。
 * NodeHealthChecker 仅作登录前探测的补充信号（占位）。
 */
class NetworkQualityAggregator : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double score READ score NOTIFY scoreChanged)
    Q_PROPERTY(bool degraded READ degraded NOTIFY degradedChanged)

public:
    explicit NetworkQualityAggregator(VehicleStatus *vehicleStatus,
                                      NodeHealthChecker *nodeHealthChecker,
                                      QObject *parent = nullptr);

    double score() const { return m_score; }
    bool degraded() const { return m_degraded; }

signals:
    void scoreChanged(double score);
    void degradedChanged(bool degraded);

private slots:
    void recompute();
    void onNodeBackendChanged();

private:
    VehicleStatus *m_vs = nullptr;
    NodeHealthChecker *m_nodes = nullptr;
    double m_score = 1.0;
    bool m_degraded = false;
    bool m_backendOk = true;
};

#endif // CLIENT_CORE_NETWORKQUALITYAGGREGATOR_H
