#include "plugincontext.h"
#include "eventbus.h"
#include "configuration.h"
#include <QDebug>

PluginContext::PluginContext(QQmlEngine* engine, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
{}

QVariant PluginContext::config(const QString& key, const QVariant& defaultValue) const
{
    return Configuration::instance().get(key, defaultValue);
}

EventBus* PluginContext::eventBus() const
{
    return &EventBus::instance();
}
