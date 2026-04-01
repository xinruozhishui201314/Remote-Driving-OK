#include "SafetyStatusModel.h"
#include <QDebug>

SafetyStatusModel::SafetyStatusModel(QObject* parent)
    : QObject(parent)
{}

void SafetyStatusModel::setAllSafe(bool safe)
{
    if (m_allSafe != safe) {
        m_allSafe = safe;
        emit safetyChanged();
    }
}

void SafetyStatusModel::setEmergencyStop(bool active, const QString& reason)
{
    if (m_emergencyStop != active) {
        m_emergencyStop = active;
        if (active) {
            qCritical() << "[Client][SafetyStatusModel] EMERGENCY STOP:" << reason;
            m_lastWarning = reason;
            m_allSafe = false;
        }
        emit emergencyStopChanged(active);
        emit safetyChanged();
    }
}

void SafetyStatusModel::setDeadmanActive(bool active)
{
    if (m_deadmanActive != active) {
        m_deadmanActive = active;
        emit deadmanChanged(active);
    }
}

void SafetyStatusModel::addWarning(const QString& message)
{
    m_lastWarning = message;
    ++m_warningCount;
    if (m_warningCount > 0) m_allSafe = false;
    emit safetyChanged();
}

void SafetyStatusModel::clearWarnings()
{
    m_warningCount = 0;
    m_lastWarning.clear();
    m_allSafe = !m_emergencyStop;
    emit safetyChanged();
}

void SafetyStatusModel::setSystemState(const QString& state)
{
    if (m_systemState != state) {
        m_systemState = state;
        emit systemStateChanged(state);
    }
}

void SafetyStatusModel::setLatencyMs(double ms)
{
    if (m_latencyMs != ms) {
        m_latencyMs = ms;
        emit safetyChanged();
    }
}
