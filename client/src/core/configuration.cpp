#include "configuration.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>

#include <functional>

Configuration::Configuration(QObject* parent)
    : QObject(parent),
      m_settings(QSettings::IniFormat, QSettings::UserScope, "RemoteDriving", "ClientConfig") {
  // 自动尝试加载默认配置文件
  const QString envFile =
      QProcessEnvironment::systemEnvironment().value("CLIENT_CONFIG_FILE", QString{});
  if (!envFile.isEmpty()) {
    m_configFilePath = envFile;
    loadFromFile(envFile, false);  // 初始加载不发射信号
  } else {
    // 尝试默认路径：当前目录下的 config/client_config.yaml，或上级目录
    const QString defaultPath = QStringLiteral("config/client_config.yaml");
    if (QFile::exists(defaultPath)) {
      m_configFilePath = defaultPath;
      loadFromFile(defaultPath, false);
      qInfo() << "[Client][Configuration] loaded default config from" << defaultPath;
    } else {
      const QString parentDefaultPath = QStringLiteral("../config/client_config.yaml");
      if (QFile::exists(parentDefaultPath)) {
        m_configFilePath = parentDefaultPath;
        loadFromFile(parentDefaultPath, false);
        qInfo() << "[Client][Configuration] loaded default config from" << parentDefaultPath;
      }
    }
  }
  qInfo() << "[Client][Configuration] initialized"
          << "server=" << serverUrl() << "mqtt=" << mqttBrokerUrl();
}

bool Configuration::loadFromFile(const QString& path, bool emitChanged) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning() << "[Client][Configuration] cannot open config file:" << path;
    return false;
  }
  const QByteArray data = f.readAll();
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(data, &err);

  QJsonObject loadedObj;
  if (err.error != QJsonParseError::NoError) {
    // 尝试简易 YAML 解析（处理 # 注释、缩进与键值对）
    qInfo() << "[Client][Configuration] JSON parse failed, trying simple YAML parser for" << path;
    loadedObj = parseSimpleYaml(data);
    if (loadedObj.isEmpty()) {
      qWarning() << "[Client][Configuration] Simple YAML parser also found no keys or failed. "
                    "JSON error:"
                 << err.errorString();
      m_lastSchemaError = err.errorString();
      return false;
    }
  } else {
    loadedObj = doc.object();
  }

  QJsonObject oldJson = m_json;
  m_json = loadedObj;
  m_configFilePath = path;

  // 记录初始 JSON（仅第一次加载时）
  if (m_initialJson.isEmpty()) {
    m_initialJson = m_json;
  }

  qInfo() << "[Client][Configuration] loaded from" << path << "keys=" << m_json.keys();

  if (emitChanged) {
    compareAndEmitChanges(oldJson, m_json);
  }

  emit configReloaded(path);
  return true;
}

bool Configuration::reload(const QString& path) {
  QString reloadPath = path.isEmpty() ? m_configFilePath : path;
  if (reloadPath.isEmpty()) {
    qWarning() << "[Client][Configuration] no config file path to reload";
    return false;
  }

  qInfo() << "[Client][Configuration] reloading from" << reloadPath;
  return loadFromFile(reloadPath, true);
}

QString Configuration::envKey(const QString& key) const {
  // "server.url" -> "CLIENT_SERVER_URL"
  return "CLIENT_" + key.toUpper().replace('.', '_').replace('-', '_');
}

QJsonValue Configuration::getJsonValue(const QString& key) const {
  if (m_json.isEmpty()) {
    return QJsonValue::Null;
  }
  QStringList parts = key.split('.');
  QJsonValue val = m_json;
  for (const QString& part : parts) {
    if (!val.isObject()) {
      return QJsonValue::Null;
    }
    val = val.toObject().value(part);
    if (val.isUndefined()) {
      return QJsonValue::Null;
    }
  }
  return val;
}

namespace {

QJsonObject setJsonPath(QJsonObject base, const QStringList& parts, int idx,
                        const QJsonValue& value) {
  if (idx >= parts.size())
    return base;
  if (idx == parts.size() - 1) {
    base[parts[idx]] = value;
    return base;
  }
  QJsonObject child;
  if (base.contains(parts[idx])) {
    const QJsonValue v = base[parts[idx]];
    if (v.isObject())
      child = v.toObject();
  }
  child = setJsonPath(child, parts, idx + 1, value);
  base[parts[idx]] = child;
  return base;
}

}  // namespace

void Configuration::setJsonValue(const QString& key, const QJsonValue& value) {
  const QStringList parts = key.split('.');
  if (parts.isEmpty())
    return;
  m_json = setJsonPath(m_json, parts, 0, value);
}

bool Configuration::compareAndEmitChanges(const QJsonObject& oldJson, const QJsonObject& newJson) {
  // 比较两个 JSON 对象的差异并发射信号
  std::function<void(const QJsonObject&, const QJsonObject&, const QString&)> compareValues;
  compareValues = [&](const QJsonObject& oldObj, const QJsonObject& newObj, const QString& prefix) {
    QStringList changedKeys;
    QSet<QString> allKeys;
    for (auto it = oldObj.begin(); it != oldObj.end(); ++it) {
      allKeys.insert(it.key());
    }
    for (auto it = newObj.begin(); it != newObj.end(); ++it) {
      allKeys.insert(it.key());
    }

    for (const QString& key : allKeys) {
      QString fullKey = prefix.isEmpty() ? key : prefix + "." + key;
      QJsonValue oldVal = oldObj.value(key);
      QJsonValue newVal = newObj.value(key);

      if (oldVal.isObject() && newVal.isObject()) {
        // 递归比较对象
        compareValues(oldVal.toObject(), newVal.toObject(), fullKey);
      } else if (oldVal != newVal) {
        // 值发生变化
        QVariant var;
        if (newVal.isBool())
          var = newVal.toBool();
        else if (newVal.isDouble())
          var = newVal.toDouble();
        else if (newVal.isString())
          var = newVal.toString();
        else if (newVal.isArray())
          var = QJsonDocument(newVal.toArray()).toJson();
        else if (newVal.isNull() || newVal.isUndefined())
          var = QVariant();

        emit configChanged(fullKey, var);
        qInfo() << "[Client][Configuration] config changed:" << fullKey << "=" << var;
      }
    }
  };

  compareValues(oldJson, newJson, QString());
  return true;
}

QVariant Configuration::get(const QString& key, const QVariant& defaultValue) const {
  // 1. 环境变量优先
  const auto env = QProcessEnvironment::systemEnvironment();
  const QString envName = envKey(key);
  if (env.contains(envName)) {
    const QString val = env.value(envName);
    static QSet<QString> s_loggedEnv;
    if (!s_loggedEnv.contains(envName)) {
      qInfo() << "[Client][Configuration] override from env:" << envName << "=" << val << "for" << key;
      s_loggedEnv.insert(envName);
    }
    return val;
  }

  // 2. QSettings（动态修改的持久化）
  if (m_settings.contains(key)) {
    return m_settings.value(key);
  }

  // 3. JSON 配置（支持点分隔的嵌套键）
  QJsonValue val = getJsonValue(key);
  if (!val.isUndefined() && !val.isNull()) {
    if (val.isBool())
      return val.toBool();
    if (val.isDouble())
      return val.toDouble();
    if (val.isString())
      return val.toString();
    if (val.isArray()) {
      QStringList list;
      for (const auto& item : val.toArray()) {
        list.append(item.toString());
      }
      return list;
    }
  }

  return defaultValue;
}

bool Configuration::set(const QString& key, const QVariant& value) {
  // 检查 key 是否存在于初始配置中
  if (!m_initialJson.isEmpty()) {
    QJsonValue initialVal = getJsonValue(key);
    if (initialVal.isUndefined() || initialVal.isNull()) {
      qWarning() << "[Client][Configuration] cannot set unknown config key:" << key;
      return false;
    }
  }

  // 更新 JSON 内存对象
  QJsonValue jsonValue;
  switch (value.typeId()) {
    case QMetaType::Bool:
      jsonValue = value.toBool();
      break;
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
      jsonValue = value.toDouble();
      break;
    case QMetaType::QString:
      jsonValue = value.toString();
      break;
    case QMetaType::QStringList:
      jsonValue = QJsonArray::fromStringList(value.toStringList());
      break;
    default:
      if (value.canConvert<QJsonValue>()) {
        jsonValue = value.value<QJsonValue>();
      }
      break;
  }

  setJsonValue(key, jsonValue);

  // 持久化到 QSettings
  m_settings.setValue(key, value);

  // 发射变更信号
  emit configChanged(key, value);
  qInfo() << "[Client][Configuration] dynamic set:" << key << "=" << value;

  return true;
}

bool Configuration::loadFromEnvironment(const QString& prefix) {
  const auto env = QProcessEnvironment::systemEnvironment();
  const QString upperPrefix = prefix.isEmpty() ? "CLIENT_" : prefix.toUpper() + "_";

  bool loaded = false;
  for (const QString& varName : env.keys()) {
    if (varName.startsWith(upperPrefix)) {
      QString key = varName.mid(upperPrefix.length()).toLower().replace('_', '.');

      if (!key.isEmpty()) {
        QJsonValue jsonValue = env.value(varName);
        setJsonValue(key, jsonValue);
        qInfo() << "[Client][Configuration] loaded from env:" << varName << "->" << key << "="
                << env.value(varName);
        loaded = true;
      }
    }
  }

  return loaded;
}

QString Configuration::getString(const QString& key, const QString& defaultValue) const {
  return get<QString>(key, defaultValue);
}

int Configuration::getInt(const QString& key, int defaultValue) const {
  return get<int>(key, defaultValue);
}

double Configuration::getDouble(const QString& key, double defaultValue) const {
  return get<double>(key, defaultValue);
}

bool Configuration::getBool(const QString& key, bool defaultValue) const {
  const QVariant v = get(key, QVariant());
  if (!v.isValid())
    return defaultValue;
  if (v.typeId() == QMetaType::Bool)
    return v.toBool();
  if (v.typeId() == QMetaType::QString) {
    const QString s = v.toString().trimmed().toLower();
    if (s == QLatin1String("false") || s == QLatin1String("no") || s == QLatin1String("0"))
      return false;
    if (s == QLatin1String("true") || s == QLatin1String("yes") || s == QLatin1String("1"))
      return true;
  }
  if (v.canConvert<double>())
    return v.toDouble() != 0.0;
  return v.toBool();
}

QStringList Configuration::getStringList(const QString& key) const {
  QJsonValue val = getJsonValue(key);
  if (val.isArray()) {
    QStringList list;
    for (const auto& item : val.toArray()) {
      list.append(item.toString());
    }
    return list;
  }
  return QStringList();
}

bool Configuration::validateWithSchema(const QJsonObject& schema) {
  m_lastSchemaError.clear();

  // 支持基本的 JSON Schema 验证（draft-07 子集）
  // 验证 type
  if (schema.contains("type")) {
    QString expectedType = schema["type"].toString();

    for (auto it = m_json.begin(); it != m_json.end(); ++it) {
      QJsonValue val = it.value();
      QString actualType;

      if (val.isBool())
        actualType = "boolean";
      else if (val.isDouble())
        actualType = "number";
      else if (val.isString())
        actualType = "string";
      else if (val.isArray())
        actualType = "array";
      else if (val.isObject())
        actualType = "object";
      else
        actualType = "null";

      if (expectedType == "object" && !val.isObject()) {
        m_lastSchemaError = QString("Expected object at root, got %1").arg(actualType);
        qWarning() << "[Client][Configuration] Schema validation failed:" << m_lastSchemaError;
        emit schemaValidationFailed(m_lastSchemaError);
        return false;
      }
    }
  }

  // 验证 required 字段
  if (schema.contains("required") && schema["required"].isArray()) {
    QStringList required;
    for (const auto& item : schema["required"].toArray()) {
      required.append(item.toString());
    }

    for (const QString& req : required) {
      QJsonValue val = getJsonValue(req);
      if (val.isUndefined() || val.isNull()) {
        m_lastSchemaError = QString("Missing required field: %1").arg(req);
        qWarning() << "[Client][Configuration] Schema validation failed:" << m_lastSchemaError;
        emit schemaValidationFailed(m_lastSchemaError);
        return false;
      }
    }
  }

  // 验证 properties
  if (schema.contains("properties") && schema["properties"].isObject()) {
    QJsonObject properties = schema["properties"].toObject();
    for (auto it = properties.begin(); it != properties.end(); ++it) {
      QString propName = it.key();
      QJsonObject propSchema = it.value().toObject();

      // 检查字段是否存在
      if (propSchema.contains("required") && propSchema["required"].toBool(false)) {
        QJsonValue val = getJsonValue(propName);
        if (val.isUndefined() || val.isNull()) {
          m_lastSchemaError = QString("Missing required property: %1").arg(propName);
          qWarning() << "[Client][Configuration] Schema validation failed:" << m_lastSchemaError;
          emit schemaValidationFailed(m_lastSchemaError);
          return false;
        }
      }

      // 验证类型
      if (propSchema.contains("type")) {
        QJsonValue val = getJsonValue(propName);
        if (!val.isUndefined() && !val.isNull()) {
          QString expectedType = propSchema["type"].toString();
          QString actualType;

          if (val.isBool())
            actualType = "boolean";
          else if (val.isDouble() || val.isString() == false) {
            if (val.isString() == false)
              actualType = "number";
            else
              actualType = "string";
          } else if (val.isString())
            actualType = "string";
          else if (val.isArray())
            actualType = "array";
          else if (val.isObject())
            actualType = "object";

          if (expectedType == "number" && !val.isDouble() && !val.isString()) {
            actualType = "number";
          } else if (expectedType == "integer" && !val.isDouble()) {
            actualType = "integer";
          }

          // 类型不匹配警告（不阻止加载）
          if (expectedType == "number" && actualType != "number" && actualType != "integer") {
            qWarning() << "[Client][Configuration] Type mismatch for" << propName
                       << "expected:" << expectedType << "got:" << actualType;
          }
        }
      }

      // 验证最小值/最大值
      QJsonValue val = getJsonValue(propName);
      if (!val.isUndefined() && !val.isNull() && (val.isDouble() || val.isString())) {
        double numVal = val.toDouble();

        if (propSchema.contains("minimum") && numVal < propSchema["minimum"].toDouble()) {
          m_lastSchemaError = QString("Property %1 below minimum: %2 < %3")
                                  .arg(propName)
                                  .arg(numVal)
                                  .arg(propSchema["minimum"].toDouble());
          qWarning() << "[Client][Configuration] Schema validation failed:" << m_lastSchemaError;
          emit schemaValidationFailed(m_lastSchemaError);
          return false;
        }

        if (propSchema.contains("maximum") && numVal > propSchema["maximum"].toDouble()) {
          m_lastSchemaError = QString("Property %1 above maximum: %2 > %3")
                                  .arg(propName)
                                  .arg(numVal)
                                  .arg(propSchema["maximum"].toDouble());
          qWarning() << "[Client][Configuration] Schema validation failed:" << m_lastSchemaError;
          emit schemaValidationFailed(m_lastSchemaError);
          return false;
        }
      }
    }
  }

  qInfo() << "[Client][Configuration] Schema validation passed";
  return true;
}

bool Configuration::validateRequired(const QStringList& keys) {
  for (const QString& key : keys) {
    QJsonValue val = getJsonValue(key);
    if (val.isUndefined() || val.isNull()) {
      qWarning() << "[Client][Configuration] Missing required config:" << key;
      m_lastSchemaError = QString("Missing required field: %1").arg(key);
      emit schemaValidationFailed(m_lastSchemaError);
      return false;
    }
  }
  return true;
}

bool Configuration::validateRange(const QString& key, double min, double max) {
  QJsonValue val = getJsonValue(key);
  if (val.isUndefined() || val.isNull()) {
    m_lastSchemaError = QString("Cannot validate range: key not found: %1").arg(key);
    qWarning() << "[Client][Configuration]" << m_lastSchemaError;
    return false;
  }

  if (!val.isDouble() && !val.isString()) {
    m_lastSchemaError = QString("Cannot validate range: value is not numeric: %1").arg(key);
    qWarning() << "[Client][Configuration]" << m_lastSchemaError;
    return false;
  }

  double value = val.toDouble();
  if (value < min || value > max) {
    m_lastSchemaError = QString("Value out of range: %1 = %2, expected [%3, %4]")
                            .arg(key)
                            .arg(value)
                            .arg(min)
                            .arg(max);
    qWarning() << "[Client][Configuration] Range validation failed:" << m_lastSchemaError;
    emit schemaValidationFailed(m_lastSchemaError);
    return false;
  }

  return true;
}

void Configuration::syncSettings() {
  m_settings.sync();
  qInfo() << "[Client][Configuration] Settings synced to persistent storage";
}

QJsonObject Configuration::parseSimpleYaml(const QByteArray& data) const {
  QJsonObject result;
  QString content = QString::fromUtf8(data);
  QStringList lines = content.split('\n');

  QStringList currentPath;
  QList<int> indents;
  indents.append(-1);

  for (QString line : lines) {
    int hashIdx = line.indexOf('#');
    if (hashIdx >= 0)
      line = line.left(hashIdx);
    QString trimmedLine = line.trimmed();
    if (trimmedLine.isEmpty())
      continue;

    int indent = 0;
    while (indent < line.length() && line[indent].isSpace()) {
      indent++;
    }

    int colonIdx = trimmedLine.indexOf(':');
    if (colonIdx <= 0)
      continue;

    QString key = trimmedLine.left(colonIdx).trimmed();
    QString value = trimmedLine.mid(colonIdx + 1).trimmed();

    while (indents.size() > 1 && indent <= indents.last()) {
      indents.removeLast();
      if (!currentPath.isEmpty())
        currentPath.removeLast();
    }

    if (value.isEmpty()) {
      currentPath.append(key);
      indents.append(indent);
    } else {
      QString fullKey = (currentPath.isEmpty() ? "" : currentPath.join('.') + ".") + key;
      QJsonValue jsonVal;
      if (value.toLower() == "true")
        jsonVal = true;
      else if (value.toLower() == "false")
        jsonVal = false;
      else {
        bool ok;
        double d = value.toDouble(&ok);
        if (ok)
          jsonVal = d;
        else {
          if (value.startsWith('"') && value.endsWith('"'))
            value = value.mid(1, value.length() - 2);
          jsonVal = value;
        }
      }
      result = setJsonPath(result, fullKey.split('.'), 0, jsonVal);
    }
  }
  return result;
}
