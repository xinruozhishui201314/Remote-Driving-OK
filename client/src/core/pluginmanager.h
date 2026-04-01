#pragma once
#include <QObject>
#include <QPluginLoader>
#include <QDir>
#include <QList>
#include <memory>
#include <vector>
#include "iplugin.h"

class PluginContext;

/**
 * 插件管理器（《客户端架构设计》§3.2.3）。
 * 扫描插件目录 → 依赖排序 → 按优先级加载。
 */
class PluginManager : public QObject {
    Q_OBJECT

public:
    explicit PluginManager(PluginContext* context, QObject* parent = nullptr);
    ~PluginManager() override;

    // 扫描并加载目录下所有插件
    bool loadPluginsFromDirectory(const QString& path);

    // 手动加载单个插件
    bool loadPlugin(const QString& filePath);

    // 卸载所有插件
    void unloadAll();

    QStringList loadedPluginIds() const;

signals:
    void pluginLoaded(const QString& id, const QString& name);
    void pluginFailed(const QString& path, const QString& error);

private:
    bool topologicalSort(QList<IPlugin*>& sorted);

    PluginContext* m_context = nullptr;
    std::vector<std::unique_ptr<QPluginLoader>> m_loaders;
    QList<IPlugin*> m_plugins;
};
