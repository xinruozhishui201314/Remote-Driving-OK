#include "configuration.h"
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QProcessEnvironment>

Configuration::Configuration(QObject* parent)
    : QObject(parent)
{
    // 自动尝试加载默认配置文件
    const QString configFile = QProcessEnvironment::systemEnvironment()
                                   .value("CLIENT_CONFIG_FILE", QString{});
    if (!configFile.isEmpty()) {
        loadFromFile(configFile);
    }
    qInfo() << "[Client][Configuration] initialized"
            << "server=" << serverUrl()
            << "mqtt=" << mqttBrokerUrl();
}

bool Configuration::loadFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[Client][Configuration] cannot open config file:" << path;
        return false;
    }
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[Client][Configuration] JSON parse error:" << err.errorString();
        return false;
    }
    m_json = doc.object();
    qInfo() << "[Client][Configuration] loaded from" << path
            << "keys=" << m_json.keys();
    return true;
}

QString Configuration::envKey(const QString& key) const
{
    // "server.url" -> "CLIENT_SERVER_URL"
    return "CLIENT_" + key.toUpper().replace('.', '_').replace('-', '_');
}

QVariant Configuration::get(const QString& key, const QVariant& defaultValue) const
{
    // 1. 环境变量优先
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString envName = envKey(key);
    if (env.contains(envName)) {
        return env.value(envName);
    }

    // 2. JSON 配置（支持点分隔的嵌套键）
    if (!m_json.isEmpty()) {
        QStringList parts = key.split('.');
        QJsonValue val = m_json;
        bool found = true;
        for (const QString& part : parts) {
            if (!val.isObject()) { found = false; break; }
            val = val.toObject().value(part);
            if (val.isUndefined()) { found = false; break; }
        }
        if (found && !val.isUndefined() && !val.isNull()) {
            if (val.isBool())   return val.toBool();
            if (val.isDouble()) return val.toDouble();
            if (val.isString()) return val.toString();
        }
    }

    return defaultValue;
}
