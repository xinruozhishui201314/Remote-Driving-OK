#include <QObject>
#include <QDebug>

#include "core/iplugin.h"
#include "core/plugincontext.h"

/**
 * 示例插件：验证 CLIENT_PLUGIN_DIR 加载链路；无业务逻辑。
 */
class NoopPlugin : public QObject, public IPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IPlugin_iid FILE "noop_plugin.json")
    Q_INTERFACES(IPlugin)

public:
    PluginInfo info() const override
    {
        return {QStringLiteral("client.noop"), QStringLiteral("No-op Sample"),
                QStringLiteral("1.0.0"),
                QStringLiteral("Example IPlugin for integration tests"), {}};
    }

    bool initialize(PluginContext &) override
    {
        qInfo() << "[Client][NoopPlugin] initialize ok id=" << info().id;
        return true;
    }

    void shutdown() override { qInfo() << "[Client][NoopPlugin] shutdown"; }
};

#include "noop_plugin.moc"
