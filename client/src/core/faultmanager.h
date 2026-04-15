#ifndef FAULTMANAGER_H
#define FAULTMANAGER_H

#include <QObject>
#include <QStringList>
#include <QMap>
#include <QtQml/qqmlregistration.h>
#include "errorregistry.h"
#include "fault_code.h"

class FaultManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QStringList activeCodes READ activeCodes NOTIFY activeCodesChanged)
    Q_PROPERTY(bool hasFaults READ hasFaults NOTIFY activeCodesChanged)

public:
    static FaultManager& instance() {
        static FaultManager inst;
        return inst;
    }

    explicit FaultManager(QObject* parent = nullptr);
    
    QStringList activeCodes() const { return m_activeCodes; }
    bool hasFaults() const { return !m_activeCodes.isEmpty(); }

    Q_INVOKABLE QVariantMap getFaultInfo(const QString& code);
    Q_INVOKABLE void clearFault(const QString& code);
    Q_INVOKABLE void clearAll();

signals:
    void activeCodesChanged();
    void faultReported(const QString& code);

public slots:
    void onErrorReported(const ErrorRegistry::Error& error, int errorId);

private:
    QStringList m_activeCodes;
};

#endif // FAULTMANAGER_H
