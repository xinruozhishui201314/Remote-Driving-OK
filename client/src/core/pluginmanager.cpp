#include "pluginmanager.h"

#include "plugincontext.h"

#include <QDebug>

#include <algorithm>

PluginManager::PluginManager(PluginContext* context, QObject* parent)
    : QObject(parent), m_context(context) {}

PluginManager::~PluginManager() { unloadAll(); }

bool PluginManager::loadPluginsFromDirectory(const QString& path) {
  QDir dir(path);
  if (!dir.exists()) {
    qWarning() << "[Client][PluginManager] plugin directory not found:" << path;
    return false;
  }

  // Scan shared libraries
  const QStringList filters{
#ifdef Q_OS_WIN
      "*.dll"
#elif defined(Q_OS_MAC)
      "*.dylib"
#else
      "*.so"
#endif
  };

  const QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
  for (const QFileInfo& fi : files) {
    loadPlugin(fi.absoluteFilePath());
  }

  // Sort and initialize
  QList<IPlugin*> sorted;
  if (!topologicalSort(sorted)) {
    qWarning() << "[Client][PluginManager] dependency cycle detected";
    return false;
  }

  for (IPlugin* plugin : sorted) {
    if (m_context && plugin->initialize(*m_context)) {
      qInfo() << "[Client][PluginManager] loaded plugin" << plugin->info().id
              << plugin->info().version;
      emit pluginLoaded(plugin->info().id, plugin->info().name);
    } else {
      qWarning() << "[Client][PluginManager] plugin init failed:" << plugin->info().id;
    }
  }
  return true;
}

bool PluginManager::loadPlugin(const QString& filePath) {
  auto loader = std::make_unique<QPluginLoader>(filePath);
  QObject* obj = loader->instance();
  if (!obj) {
    const QString err = loader->errorString();
    qWarning() << "[Client][PluginManager] cannot load" << filePath << ":" << err;
    emit pluginFailed(filePath, err);
    return false;
  }

  IPlugin* plugin = qobject_cast<IPlugin*>(obj);
  if (!plugin) {
    plugin = dynamic_cast<IPlugin*>(obj);
  }
  if (!plugin) {
    qWarning() << "[Client][PluginManager] object is not IPlugin:" << filePath;
    loader->unload();
    return false;
  }

  m_plugins.append(plugin);
  m_loaders.push_back(std::move(loader));
  return true;
}

void PluginManager::unloadAll() {
  // Shutdown in reverse order
  for (int i = m_plugins.size() - 1; i >= 0; --i) {
    m_plugins[i]->shutdown();
  }
  m_plugins.clear();

  for (auto& loader : m_loaders) {
    loader->unload();
  }
  m_loaders.clear();
  qInfo() << "[Client][PluginManager] all plugins unloaded";
}

QStringList PluginManager::loadedPluginIds() const {
  QStringList ids;
  for (IPlugin* p : m_plugins) {
    ids << p->info().id;
  }
  return ids;
}

bool PluginManager::topologicalSort(QList<IPlugin*>& sorted) {
  QList<IPlugin*> remaining = m_plugins;

  // Sort by priority first
  std::stable_sort(remaining.begin(), remaining.end(),
                   [](IPlugin* a, IPlugin* b) { return a->priority() < b->priority(); });

  // Simple Kahn's algorithm for dependency resolution
  QStringList loadedIds;
  int maxIter = remaining.size() * remaining.size() + 1;

  while (!remaining.isEmpty() && --maxIter > 0) {
    bool progress = false;
    for (int i = 0; i < remaining.size();) {
      IPlugin* p = remaining[i];
      bool depsOk = true;
      for (const QString& dep : p->dependencies()) {
        if (!loadedIds.contains(dep)) {
          depsOk = false;
          break;
        }
      }
      if (depsOk) {
        sorted.append(p);
        loadedIds << p->info().id;
        remaining.removeAt(i);
        progress = true;
      } else {
        ++i;
      }
    }
    if (!progress)
      break;
  }

  return remaining.isEmpty();
}
