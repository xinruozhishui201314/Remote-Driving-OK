#include "faultmanager.h"
#include <QDebug>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>

FaultManager::FaultManager(QObject* parent) : QObject(parent) {
    // 监听错误上报信号
    connect(&ErrorRegistry::instance(), &ErrorRegistry::errorReported, this, &FaultManager::onErrorReported);
    qDebug() << "[Client][FaultManager] initialized";
}

void FaultManager::onErrorReported(const ErrorRegistry::Error& error, int errorId) {
    Q_UNUSED(errorId);
    
    // 检查消息中是否包含故障码，格式如 "[TEL-1001] Message"
    static QRegularExpression re(QStringLiteral("\\[([A-Z]+-[0-9]+)\\]"));
    auto match = re.match(error.message);
    if (match.hasMatch()) {
        QString code = match.captured(1);
        if (!m_activeCodes.contains(code)) {
            m_activeCodes.append(code);
            emit activeCodesChanged();
            emit faultReported(code);
            qDebug() << "[Client][FaultManager] Active fault reported:" << code;
        }
    }
}

QVariantMap FaultManager::getFaultInfo(const QString& code) {
    using namespace teleop::protocol;
    std::string stdCode = code.toStdString();
    QVariantMap result;
    if (!FaultCodeManager::exists(stdCode)) {
        return result;
    }
    const FaultCode& fc = FaultCodeManager::get(stdCode);
    result["code"] = QString::fromStdString(fc.code);
    result["name"] = QString::fromStdString(fc.name);
    result["severity"] = static_cast<int>(fc.severity);
    result["domain"] = static_cast<int>(fc.domain);
    result["message"] = QString::fromStdString(fc.message);
    result["recommendedAction"] = QString::fromStdString(fc.recommendedAction);
    return result;
}

void FaultManager::clearFault(const QString& code) {
    if (m_activeCodes.removeOne(code)) {
        emit activeCodesChanged();
        qDebug() << "[Client][FaultManager] Fault cleared:" << code;
    }
}

void FaultManager::clearAll() {
    if (!m_activeCodes.isEmpty()) {
        m_activeCodes.clear();
        emit activeCodesChanged();
        qDebug() << "[Client][FaultManager] All faults cleared";
    }
}
