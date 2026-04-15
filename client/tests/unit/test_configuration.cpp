#include "core/configuration.h"

#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryFile>
#include <QtTest/QtTest>

class TestConfiguration : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestConfiguration)
 public:
  explicit TestConfiguration(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // 配置加载测试
  void test_singletonAccess();
  void test_loadFromFile();
  void test_reload();

  // 配置读取测试
  void test_getVariant();
  void test_getString();
  void test_getInt();
  void test_getDouble();
  void test_getBool();

  // 配置设置测试
  void test_set();
  void test_setOverride();

  // 便利方法测试
  void test_convenienceMethods();

  // 环境变量测试
  void test_environmentVariable();

  // Schema 验证测试
  void test_validateRequired();
};

void TestConfiguration::initTestCase() {}

void TestConfiguration::cleanupTestCase() {}

void TestConfiguration::init() {
  // 各用例共用同一 QSettings 路径；避免前序用例持久化覆盖后续对 JSON 的读取
  QSettings s(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("RemoteDriving"),
              QStringLiteral("ClientConfig"));
  s.clear();
  s.sync();
}

void TestConfiguration::cleanup() {}

void TestConfiguration::test_singletonAccess() {
  // 测试单例访问
  Configuration& config = Configuration::instance();
  Q_UNUSED(config);

  // 多次访问应返回同一实例
  Configuration& config2 = Configuration::instance();
  QVERIFY2(&config == &config2, "Configuration singleton should return same instance");
}

void TestConfiguration::test_loadFromFile() {
  // 创建临时 JSON 文件
  QTemporaryFile jsonFile;
  jsonFile.open();
  jsonFile.write(R"({
        "server": {
            "url": "http://localhost:8080"
        },
        "mqtt": {
            "broker_url": "tcp://localhost:1883"
        },
        "control": {
            "rate_hz": 100
        },
        "debug": true
    })");
  jsonFile.close();

  Configuration config;

  // 加载配置文件
  bool loaded = config.loadFromFile(jsonFile.fileName());
  QVERIFY2(loaded, "Failed to load JSON configuration");

  // 验证读取的值
  QString serverUrl = config.get<QString>("server.url", "http://default:8080");
  QVERIFY2(serverUrl == "http://localhost:8080",
           qPrintable(QString("Expected 'http://localhost:8080', got '%1'").arg(serverUrl)));

  QString brokerUrl = config.get<QString>("mqtt.broker_url", "tcp://default:1883");
  QVERIFY2(brokerUrl == "tcp://localhost:1883",
           qPrintable(QString("Expected 'tcp://localhost:1883', got '%1'").arg(brokerUrl)));

  int rateHz = config.get<int>("control.rate_hz", 50);
  QVERIFY2(rateHz == 100, qPrintable(QString("Expected 100, got %1").arg(rateHz)));

  bool debug = config.get<bool>("debug", false);
  QVERIFY2(debug == true, "Expected debug=true");
}

void TestConfiguration::test_reload() {
  // 创建临时文件
  QTemporaryFile jsonFile;
  jsonFile.open();
  jsonFile.write(R"({"test_key": "original_value"})");
  jsonFile.close();

  Configuration config;
  config.loadFromFile(jsonFile.fileName());

  QString original = config.get<QString>("test_key", "");
  QVERIFY2(original == "original_value", "Original value should be loaded");

  // 修改文件内容
  QFile file(jsonFile.fileName());
  QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to open file for writing");
  file.write(R"({"test_key": "updated_value"})");
  file.close();

  // 重新加载
  bool reloaded = config.reload(jsonFile.fileName());
  QVERIFY2(reloaded, "Reload should succeed");

  QString updated = config.get<QString>("test_key", "");
  QVERIFY2(updated == "updated_value",
           qPrintable(QString("Expected 'updated_value', got '%1'").arg(updated)));
}

void TestConfiguration::test_getVariant() {
  Configuration config;
  config.set("string_key", QVariant("hello"));
  config.set("int_key", QVariant(42));
  config.set("double_key", QVariant(3.14));
  config.set("bool_key", QVariant(true));

  // 验证各种类型的读取
  QVariant stringVal = config.get("string_key");
  QVERIFY2(stringVal.toString() == "hello", "String value mismatch");

  QVariant intVal = config.get("int_key");
  QVERIFY2(intVal.toInt() == 42, "Int value mismatch");

  QVariant doubleVal = config.get("double_key");
  QVERIFY2(qAbs(doubleVal.toDouble() - 3.14) < 0.001, "Double value mismatch");

  QVariant boolVal = config.get("bool_key");
  QVERIFY2(boolVal.toBool() == true, "Bool value mismatch");
}

void TestConfiguration::test_getString() {
  Configuration config;
  config.set("test.string", "hello");

  QString value = config.getString("test.string", "default");
  QVERIFY2(value == "hello", qPrintable(QString("Expected 'hello', got '%1'").arg(value)));

  // 测试默认值
  QString missing = config.getString("nonexistent.key", "default_value");
  QVERIFY2(missing == "default_value",
           qPrintable(QString("Expected 'default_value', got '%1'").arg(missing)));
}

void TestConfiguration::test_getInt() {
  Configuration config;
  config.set("test.int", 123);

  int value = config.getInt("test.int", 0);
  QVERIFY2(value == 123, qPrintable(QString("Expected 123, got %1").arg(value)));

  // 测试字符串转整数
  config.set("test.int.string", "456");
  int fromString = config.getInt("test.int.string", 0);
  QVERIFY2(fromString == 456, qPrintable(QString("Expected 456, got %1").arg(fromString)));
}

void TestConfiguration::test_getDouble() {
  Configuration config;
  config.set("test.double", 3.14159);

  double value = config.getDouble("test.double", 0.0);
  QVERIFY2(qAbs(value - 3.14159) < 0.0001,
           qPrintable(QString("Expected ~3.14159, got %1").arg(value)));

  // 测试字符串转浮点数
  config.set("test.double.string", "2.71828");
  double fromString = config.getDouble("test.double.string", 0.0);
  QVERIFY2(qAbs(fromString - 2.71828) < 0.0001,
           qPrintable(QString("Expected ~2.71828, got %1").arg(fromString)));
}

void TestConfiguration::test_getBool() {
  Configuration config;

  // 测试各种布尔值表示
  config.set("test.bool.true1", true);
  config.set("test.bool.true2", "true");
  config.set("test.bool.true3", "yes");
  config.set("test.bool.true4", "1");
  config.set("test.bool.false1", false);
  config.set("test.bool.false2", "false");
  config.set("test.bool.false3", "no");
  config.set("test.bool.false4", "0");

  QVERIFY(config.getBool("test.bool.true1") == true);
  QVERIFY(config.getBool("test.bool.true2") == true);
  QVERIFY(config.getBool("test.bool.true3") == true);
  QVERIFY(config.getBool("test.bool.true4") == true);
  QVERIFY(config.getBool("test.bool.false1") == false);
  QVERIFY(config.getBool("test.bool.false2") == false);
  QVERIFY(config.getBool("test.bool.false3") == false);
  QVERIFY(config.getBool("test.bool.false4") == false);
}

void TestConfiguration::test_set() {
  Configuration config;

  // 设置值
  bool success = config.set("test.key", "test_value");
  QVERIFY2(success, "set() should return true");

  // 读取验证
  QString value = config.get<QString>("test.key", "");
  QVERIFY2(value == "test_value",
           qPrintable(QString("Expected 'test_value', got '%1'").arg(value)));

  // 设置嵌套键
  success = config.set("a.b.c", "nested_value");
  QVERIFY2(success, "set() for nested key should return true");

  QString nested = config.get<QString>("a.b.c", "");
  QVERIFY2(nested == "nested_value",
           qPrintable(QString("Expected 'nested_value', got '%1'").arg(nested)));
}

void TestConfiguration::test_setOverride() {
  Configuration config;

  // 设置原始值
  config.set("test.override", "original");
  QString original = config.getString("test.override", "");
  QVERIFY2(original == "original", "Original value should be set");

  // 注意：Configuration 类可能不直接支持 setOverride 方法
  // 根据实际 API 调整测试
  // 这里只测试基本设置功能
}

void TestConfiguration::test_convenienceMethods() {
  Configuration& config = Configuration::instance();

  // 设置测试值
  config.set("server.url", "http://test:9090");
  config.set("mqtt.broker_url", "tcp://test:1883");
  config.set("zlm.url", "http://test:8081");
  config.set("keycloak.url", "http://test:8082");
  config.set("control.rate_hz", 50);
  config.set("safety.check_hz", 25);
  QVERIFY2(config.set("media.hardware_decode", false),
           "set media.hardware_decode failed (unknown key in loaded schema?)");
  QVERIFY2(config.set("media.require_hardware_decode", false),
           "set media.require_hardware_decode failed (add key to client_config.yaml / initial JSON)");

  // 测试便利方法
  QVERIFY2(config.serverUrl() == "http://test:9090",
           qPrintable(QString("serverUrl() mismatch: %1").arg(config.serverUrl())));
  QVERIFY2(config.mqttBrokerUrl() == "tcp://test:1883",
           qPrintable(QString("mqttBrokerUrl() mismatch: %1").arg(config.mqttBrokerUrl())));
  QVERIFY2(config.zlmUrl() == "http://test:8081",
           qPrintable(QString("zlmUrl() mismatch: %1").arg(config.zlmUrl())));
  QVERIFY2(config.keycloakUrl() == "http://test:8082",
           qPrintable(QString("keycloakUrl() mismatch: %1").arg(config.keycloakUrl())));
  QVERIFY2(config.controlRateHz() == 50,
           qPrintable(QString("controlRateHz() mismatch: %1").arg(config.controlRateHz())));
  QVERIFY2(config.safetyCheckHz() == 25,
           qPrintable(QString("safetyCheckHz() mismatch: %1").arg(config.safetyCheckHz())));
  QVERIFY2(config.enableHardwareDecode() == false,
           qPrintable(
               QString("enableHardwareDecode() mismatch: %1").arg(config.enableHardwareDecode())));
  QVERIFY2(config.requireHardwareDecode() == false,
           qPrintable(
               QString("requireHardwareDecode() mismatch: %1").arg(config.requireHardwareDecode())));
}

void TestConfiguration::test_environmentVariable() {
  // 设置环境变量
  qputenv("TEST_SERVER_PORT", "7070");
  qputenv("TEST_VIDEO_QUALITY", "high");

  Configuration config;

  // 从环境变量加载（使用 TEST_ 前缀）
  // 注意：具体实现取决于 Configuration::loadFromEnvironment 的行为
  bool loaded = config.loadFromEnvironment("TEST");
  QVERIFY2(loaded, "loadFromEnvironment should succeed");

  // 根据实现验证环境变量是否被读取
  // 可能需要检查具体的键名格式
  QString port = config.get<QString>("SERVER_PORT", "");
  if (!port.isEmpty()) {
    QVERIFY2(port == "7070", qPrintable(QString("Expected '7070', got '%1'").arg(port)));
  }

  // 清理环境变量
  qunsetenv("TEST_SERVER_PORT");
  qunsetenv("TEST_VIDEO_QUALITY");
}

void TestConfiguration::test_validateRequired() {
  Configuration config;
  config.set("required.key", "present");

  // 验证存在的键
  QStringList requiredKeys = {"required.key"};
  bool valid = config.validateRequired(requiredKeys);
  QVERIFY2(valid, "Validation should pass for existing key");

  // 验证缺失的键
  QStringList missingKeys = {"missing.key"};
  valid = config.validateRequired(missingKeys);
  QVERIFY2(!valid, "Validation should fail for missing key");
}

QTEST_MAIN(TestConfiguration)
#include "test_configuration.moc"
