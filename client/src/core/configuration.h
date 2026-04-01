#pragma once
#include <QObject>
#include <QVariant>
#include <QJsonObject>
#include <QString>

/**
 * 集中配置读取（《客户端架构设计》§7.1 工程化实践）。
 *
 * 优先级（高到低）：
 *   1. 环境变量
 *   2. JSON 配置文件（CLIENT_CONFIG_FILE 指定路径）
 *   3. 代码内默认值
 *
 * 使用：Configuration::instance().get<QString>("server.url", "http://localhost:8080")
 */
class Configuration : public QObject {
    Q_OBJECT

public:
    static Configuration& instance() {
        static Configuration cfg;
        return cfg;
    }

    explicit Configuration(QObject* parent = nullptr);

    // 从文件加载（可选；不加载时仅使用环境变量和默认值）
    bool loadFromFile(const QString& path);

    // 通用获取（先查环境变量，再查JSON，最后用默认值）
    QVariant get(const QString& key, const QVariant& defaultValue = {}) const;

    template <typename T>
    T get(const QString& key, const T& defaultValue = {}) const {
        return get(key, QVariant::fromValue(defaultValue)).template value<T>();
    }

    // 便利方法
    QString serverUrl() const       { return get<QString>("server.url", "http://localhost:8080"); }
    QString mqttBrokerUrl() const   { return get<QString>("mqtt.broker_url", "tcp://localhost:1883"); }
    QString zlmUrl() const          { return get<QString>("zlm.url", "http://localhost:8080"); }
    QString keycloakUrl() const     { return get<QString>("keycloak.url", "http://localhost:8080"); }
    int     controlRateHz() const   { return get<int>("control.rate_hz", 100); }
    int     safetyCheckHz() const   { return get<int>("safety.check_hz", 50); }
    int     inputSampleHz() const   { return get<int>("input.sample_hz", 200); }
    int     heartbeatIntervalMs() const { return get<int>("heartbeat.interval_ms", 100); }
    int     heartbeatTimeoutMs() const  { return get<int>("heartbeat.timeout_ms", 500); }
    bool    enableHardwareDecode() const { return get<bool>("media.hardware_decode", true); }
    bool    enableFEC() const       { return get<bool>("transport.fec", true); }
    int     framePoolSize() const   { return get<int>("media.frame_pool_size", 16); }

signals:
    void configChanged(const QString& key, const QVariant& value);

private:
    QString envKey(const QString& key) const;
    QJsonObject m_json;
};
