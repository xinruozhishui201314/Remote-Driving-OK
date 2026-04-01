#include "controlloopticker.h"
#include <QTimer>
#include <QDebug>

ControlLoopTicker::ControlLoopTicker(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(20);
    connect(m_timer, &QTimer::timeout, this, &ControlLoopTicker::onTimeout);
}

void ControlLoopTicker::setIntervalMs(int ms)
{
    m_timer->setInterval(qBound(5, ms, 500));
}

void ControlLoopTicker::start()
{
    m_index = 0;
    m_timer->start();
    qDebug().noquote() << "[Client][ControlLoopTicker] started intervalMs=" << m_timer->interval();
}

void ControlLoopTicker::stop()
{
    m_timer->stop();
    qDebug().noquote() << "[Client][ControlLoopTicker] stopped";
}

void ControlLoopTicker::onTimeout()
{
    emit tick(++m_index);
}
