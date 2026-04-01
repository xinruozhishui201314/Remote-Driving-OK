#pragma once
#include <QObject>
#include <QStringList>
#include <QtPlugin>

/**
 * 插件接口（《客户端架构设计》§3.2.3）。
 * 所有可热加载插件必须实现此接口。
 */

class PluginContext;

struct PluginInfo {
    QString id;
    QString name;
    QString version;
    QString description;
    QString author;
};

class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual PluginInfo info() const = 0;

    // 依赖的其他插件 ID
    virtual QStringList dependencies() const { return {}; }

    // 优先级（越小越先初始化）
    virtual int priority() const { return 100; }

    virtual bool initialize(PluginContext& context) = 0;
    virtual void shutdown() = 0;
};

#define IPlugin_iid "com.remotedrivingclient.IPlugin/1.0"
Q_DECLARE_INTERFACE(IPlugin, IPlugin_iid)
