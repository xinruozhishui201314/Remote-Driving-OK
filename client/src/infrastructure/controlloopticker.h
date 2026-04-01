#ifndef CLIENT_INFRA_CONTROLLOOPTICKER_H
#define CLIENT_INFRA_CONTROLLOOPTICKER_H

#include <QObject>

class QTimer;

/**
 * 控制环节拍占位：当前与 GUI 同线程定时器（默认不启动）。
 * 独立线程 100Hz 控制环需在实车环境 profiling 后再启用（见《客户端架构设计》§4）。
 */
class ControlLoopTicker : public QObject
{
    Q_OBJECT

public:
    explicit ControlLoopTicker(QObject *parent = nullptr);

    void setIntervalMs(int ms);
    void start();
    void stop();

signals:
    void tick(qint64 tickIndex);

private slots:
    void onTimeout();

private:
    QTimer *m_timer = nullptr;
    qint64 m_index = 0;
};

#endif // CLIENT_INFRA_CONTROLLOOPTICKER_H
