#pragma once
#include <QObject>
#include <QVariant>
#include <QQmlEngine>
#include <memory>
#include <map>

/**
 * 插件执行上下文（《客户端架构设计》§3.2.3）。
 * 提供服务注册/发现、配置访问和 QML 类型注册。
 */
class PluginContext : public QObject {
    Q_OBJECT

public:
    explicit PluginContext(QQmlEngine* engine = nullptr, QObject* parent = nullptr);

    // 服务注册/发现（IOC 容器）
    template <typename T>
    void registerService(const QString& name, std::shared_ptr<T> service) {
        m_services[name] = std::static_pointer_cast<void>(service);
    }

    template <typename T>
    std::shared_ptr<T> getService(const QString& name) const {
        auto it = m_services.find(name);
        if (it == m_services.end()) return nullptr;
        return std::static_pointer_cast<T>(it->second);
    }

    bool hasService(const QString& name) const {
        return m_services.count(name) > 0;
    }

    // QML 类型注册
    QQmlEngine* qmlEngine() const { return m_engine; }

    // 配置访问（代理到 Configuration::instance()）
    QVariant config(const QString& key, const QVariant& defaultValue = {}) const;

    // 事件总线访问
    class EventBus* eventBus() const;

signals:
    void serviceRegistered(const QString& name);

private:
    QQmlEngine* m_engine = nullptr;
    std::map<QString, std::shared_ptr<void>> m_services;
};
