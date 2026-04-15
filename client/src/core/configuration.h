#pragma once
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

/**
 * 集中配置读取（《客户端架构设计》§7.1 工程化实践）。
 *
 * 优先级（高到低）：
 *   1. 环境变量
 *   2. JSON 配置文件（CLIENT_CONFIG_FILE 指定路径）
 *   3. 代码内默认值
 *
 * 使用：Configuration::instance().get<QString>("server.url", "http://localhost:8080")
 *
 * 支持功能：
 *   - JSON Schema 验证
 *   - 配置热更新
 *   - QSettings 持久化
 *   - 从环境变量加载
 */
class Configuration : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(Configuration)

 public:
  static Configuration& instance() {
    static Configuration cfg;
    return cfg;
  }

  explicit Configuration(QObject* parent = nullptr);
  ~Configuration() override = default;

  // ═══════════════════════════════════════════════════════════════════════════
  // 文件加载与热更新
  // ═══════════════════════════════════════════════════════════════════════════

  // 从文件加载（可选；不加载时仅使用环境变量和默认值）
  bool loadFromFile(const QString& path, bool emitChanged = true);

  // 重新加载配置文件
  Q_INVOKABLE bool reload(const QString& path = QString{});

  // ═══════════════════════════════════════════════════════════════════════════
  // 通用获取/设置
  // ═══════════════════════════════════════════════════════════════════════════

  // 通用获取（先查环境变量，再查JSON/QSettings，最后用默认值）
  QVariant get(const QString& key, const QVariant& defaultValue = {}) const;

  template <typename T>
  T get(const QString& key, const T& defaultValue = {}) const {
    return get(key, QVariant::fromValue(defaultValue)).template value<T>();
  }

  // 动态设置配置值（用于运行时修改）
  Q_INVOKABLE bool set(const QString& key, const QVariant& value);

  // 从环境变量加载配置
  bool loadFromEnvironment(const QString& prefix);

  // ═══════════════════════════════════════════════════════════════════════════
  // Schema 验证
  // ═══════════════════════════════════════════════════════════════════════════

  // 使用 JSON Schema 验证当前配置
  Q_INVOKABLE bool validateWithSchema(const QJsonObject& schema);

  // 验证必需字段是否存在
  Q_INVOKABLE bool validateRequired(const QStringList& keys);

  // 验证数值范围
  Q_INVOKABLE bool validateRange(const QString& key, double min, double max);

  // 获取 Schema 验证错误信息
  Q_INVOKABLE QString getLastSchemaError() const { return m_lastSchemaError; }

  // ═══════════════════════════════════════════════════════════════════════════
  // 便利方法
  // ═══════════════════════════════════════════════════════════════════════════

  // 类型安全的获取方法
  Q_INVOKABLE QString getString(const QString& key, const QString& defaultValue = QString{}) const;
  Q_INVOKABLE int getInt(const QString& key, int defaultValue = 0) const;
  Q_INVOKABLE double getDouble(const QString& key, double defaultValue = 0.0) const;
  Q_INVOKABLE bool getBool(const QString& key, bool defaultValue = false) const;
  Q_INVOKABLE QStringList getStringList(const QString& key) const;

  // 预设便捷访问方法
  QString serverUrl() const { return get<QString>("server.url", "http://localhost:8080"); }
  QString mqttBrokerUrl() const { return get<QString>("mqtt.broker_url", "tcp://localhost:1883"); }
  QString zlmUrl() const { return get<QString>("zlm.url", "http://localhost:8080"); }
  QString keycloakUrl() const { return get<QString>("keycloak.url", "http://localhost:8080"); }
  int controlRateHz() const { return get<int>("control.rate_hz", 100); }
  int safetyCheckHz() const { return get<int>("safety.check_hz", 50); }
  int inputSampleHz() const { return get<int>("input.sample_hz", 200); }
  int heartbeatIntervalMs() const { return get<int>("heartbeat.interval_ms", 100); }
  int heartbeatTimeoutMs() const { return get<int>("heartbeat.timeout_ms", 500); }
  /** 是否尝试 WebRTC H.264 硬件解码（VAAPI/NVDEC）。环境变量 CLIENT_MEDIA_HARDWARE_DECODE 优先于 JSON。 */
  bool enableHardwareDecode() const { return get<bool>("media.hardware_decode", true); }
  /**
   * 为 true 时：在已具备 SPS/PPS 且硬解已编译的前提下，硬解打开失败则禁止退回 libavcodec 软解（无画面直至环境满足）。
   * 开发/CI 无 GPU 时可设 media.require_hardware_decode: false 或 CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0。
   */
  bool requireHardwareDecode() const { return get<bool>("media.require_hardware_decode", false); }
  bool enableFEC() const { return get<bool>("transport.fec", true); }
  int framePoolSize() const { return get<int>("media.frame_pool_size", 16); }

  // ═══════════════════════════════════════════════════════════════════════════
  // 持久化
  // ═══════════════════════════════════════════════════════════════════════════

  // 获取 QSettings 以持久化动态修改
  QSettings* settings() { return &m_settings; }
  void syncSettings();

 signals:
  // 配置变更信号（key, newValue）
  void configChanged(const QString& key, const QVariant& value);

  // 配置重载完成信号
  void configReloaded(const QString& filePath);

  // Schema 验证失败信号
  void schemaValidationFailed(const QString& error);

 private:
  QJsonObject parseSimpleYaml(const QByteArray& data) const;
  QString envKey(const QString& key) const;
  QJsonValue getJsonValue(const QString& key) const;
  void setJsonValue(const QString& key, const QJsonValue& value);
  bool compareAndEmitChanges(const QJsonObject& oldJson, const QJsonObject& newJson);

  QJsonObject m_json;
  QJsonObject m_initialJson;  // 初始加载的 JSON，用于对比变化
  QString m_configFilePath;
  QSettings m_settings;
  QString m_lastSchemaError;
};
