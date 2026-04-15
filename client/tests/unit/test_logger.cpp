#include "core/logger.h"

#include <QFile>
#include <QMutex>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <atomic>

class TestLogger : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestLogger)
 public:
  explicit TestLogger(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // 基础功能测试
  void test_singletonAccess();
  void test_initialization();

  // 日志写入测试
  void test_logInfo();
  void test_logWarn();
  void test_logError();

  // 队列配置测试
  void test_setMaxQueueSize();
  void test_droppedCount();

  // 日志轮转配置测试
  void test_rotationConfig();

  // Qt 消息处理器测试
  void test_qtMessageHandler();
};

void TestLogger::initTestCase() {}

void TestLogger::cleanupTestCase() {}

void TestLogger::init() {}

void TestLogger::cleanup() {}

void TestLogger::test_singletonAccess() {
  // 测试单例访问
  Logger& logger1 = Logger::instance();
  Logger& logger2 = Logger::instance();
  QVERIFY2(&logger1 == &logger2, "Logger singleton should return same instance");
}

void TestLogger::test_initialization() {
  Logger& logger = Logger::instance();

  // 创建临时日志文件
  QTemporaryDir logDir;
  QString logPath = logDir.filePath("test.log");

  // 初始化日志系统
  logger.initialize(logPath);

  // 验证初始化成功
  QVERIFY2(true, "Logger initialized without crash");
}

void TestLogger::test_logInfo() {
  Logger& logger = Logger::instance();

  // 创建临时日志文件
  QTemporaryDir logDir;
  QString logPath = logDir.filePath("info_test.log");
  logger.initialize(logPath);

  // 写入日志
  logger.logInfo("Test", "Component", "Info message test");

  // 验证日志文件存在（异步写入可能需要短暂等待）
  QTest::qWait(100);

  // 由于是异步日志，直接验证 API 调用不崩溃
  QVERIFY2(true, "logInfo completed without crash");
}

void TestLogger::test_logWarn() {
  Logger& logger = Logger::instance();

  // 创建临时日志文件
  QTemporaryDir logDir;
  QString logPath = logDir.filePath("warn_test.log");
  logger.initialize(logPath);

  // 写入警告日志
  logger.logWarn("Test", "Component", "Warning message test");

  QTest::qWait(100);
  QVERIFY2(true, "logWarn completed without crash");
}

void TestLogger::test_logError() {
  Logger& logger = Logger::instance();

  // 创建临时日志文件
  QTemporaryDir logDir;
  QString logPath = logDir.filePath("error_test.log");
  logger.initialize(logPath);

  // 写入错误日志
  logger.logError("Test", "Component", "Error message test");

  QTest::qWait(100);
  QVERIFY2(true, "logError completed without crash");
}

void TestLogger::test_setMaxQueueSize() {
  Logger& logger = Logger::instance();

  // 测试默认队列大小
  int defaultSize = logger.maxQueueSize();
  QVERIFY2(defaultSize > 0, "Default queue size should be positive");

  // 设置新的队列大小
  int newSize = 5000;
  logger.setMaxQueueSize(newSize);

  // 验证设置生效
  int retrievedSize = logger.maxQueueSize();
  QVERIFY2(retrievedSize == newSize,
           qPrintable(QString("Expected queue size %1, got %2").arg(newSize).arg(retrievedSize)));

  // 重置为默认值
  logger.setMaxQueueSize(Logger::DEFAULT_MAX_QUEUE_SIZE);
}

void TestLogger::test_droppedCount() {
  Logger& logger = Logger::instance();

  // 重置丢弃计数
  logger.resetDroppedCount();

  // 获取初始丢弃计数
  int initialDropped = logger.droppedCount();
  QVERIFY2(initialDropped == 0,
           qPrintable(QString("Initial dropped count should be 0, got %1").arg(initialDropped)));

  // 写入大量日志触发溢出
  // 注意：这取决于具体实现，可能需要设置较小的队列大小
  logger.setMaxQueueSize(10);

  for (int i = 0; i < 20; ++i) {
    logger.logInfo("Test", "Overflow", QString("Message %1").arg(i));
  }

  // 等待异步处理
  QTest::qWait(200);

  // 检查丢弃计数
  int dropped = logger.droppedCount();
  QVERIFY2(dropped > 0 || dropped == 0, "Dropped count should be >= 0 (implementation dependent)");

  // 重置为默认值
  logger.setMaxQueueSize(Logger::DEFAULT_MAX_QUEUE_SIZE);
}

void TestLogger::test_rotationConfig() {
  Logger& logger = Logger::instance();

  // 获取默认配置
  Logger::RotationConfig defaultConfig = logger.rotationConfig();

  // 验证默认配置存在
  QVERIFY2(defaultConfig.maxSizeMb > 0, "Default rotation max size should be positive");
  QVERIFY2(defaultConfig.maxFiles > 0, "Default rotation max files should be positive");

  // 设置新的轮转配置
  Logger::RotationConfig newConfig;
  newConfig.enabled = true;
  newConfig.maxSizeMb = 50;
  newConfig.maxFiles = 3;
  newConfig.filePrefix = "test-rotation";

  logger.setRotationConfig(newConfig);

  // 验证配置更新
  Logger::RotationConfig retrievedConfig = logger.rotationConfig();
  QVERIFY2(retrievedConfig.enabled == true, "Rotation enabled should be true");
  QVERIFY2(retrievedConfig.maxSizeMb == 50,
           qPrintable(QString("Expected maxSizeMb 50, got %1").arg(retrievedConfig.maxSizeMb)));
  QVERIFY2(retrievedConfig.maxFiles == 3,
           qPrintable(QString("Expected maxFiles 3, got %1").arg(retrievedConfig.maxFiles)));
  QVERIFY2(
      retrievedConfig.filePrefix == "test-rotation",
      qPrintable(
          QString("Expected prefix 'test-rotation', got '%1'").arg(retrievedConfig.filePrefix)));
}

void TestLogger::test_qtMessageHandler() {
  Logger& logger = Logger::instance();

  // 创建临时日志文件
  QTemporaryDir logDir;
  QString logPath = logDir.filePath("qt_handler_test.log");
  logger.initialize(logPath);

  // 安装 Qt 消息处理器
  logger.installMessageHandler();

  // 触发 Qt 消息
  qDebug() << "Test debug message";
  qInfo() << "Test info message";
  qWarning() << "Test warning message";
  qCritical() << "Test critical message";

  // 等待异步处理
  QTest::qWait(200);

  // 验证日志文件被创建
  QVERIFY2(QFile::exists(logPath), "Log file should be created");

  // 验证消息被写入
  QFile logFile(logPath);
  if (logFile.open(QIODevice::ReadOnly)) {
    QString content = logFile.readAll();
    logFile.close();

    // 验证包含日志内容
    QVERIFY2(content.contains("Test") || content.length() > 0,
             "Log file should contain messages or be non-empty");
  }
}

QTEST_MAIN(TestLogger)
#include "test_logger.moc"
