#include "plugincontext.h"

#include "configuration.h"
#include "eventbus.h"

#include <QDebug>

PluginContext::PluginContext(QQmlEngine* engine, EventBus* eventBus, QObject* parent)
    : QObject(parent), m_engine(engine), m_eventBus(eventBus) {
  Q_ASSERT(m_eventBus != nullptr);
}

QVariant PluginContext::config(const QString& key, const QVariant& defaultValue) const {
  return Configuration::instance().get(key, defaultValue);
}
