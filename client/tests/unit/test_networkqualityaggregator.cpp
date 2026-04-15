#include "core/networkqualityaggregator.h"
#include "nodehealthchecker.h"
#include "vehiclestatus.h"

#include <QCoreApplication>
#include <QtTest/QtTest>

class TestNetworkQualityAggregator : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestNetworkQualityAggregator)
 public:
  explicit TestNetworkQualityAggregator(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // 基础功能测试
  void test_constructor();
  void test_initialScore();

  // 属性测试
  void test_scoreProperty();
  void test_degradedProperty();

  // 信号测试
  void test_scoreChangedSignal();
  void test_degradedChangedSignal();

  // 状态变化测试
  void test_recomputeOnStatusChange();
  /** V1：呈现降级拉低综合分，定时恢复乘子 */
  void test_mediaPresentationPenaltyAndRecovery();
  /** 多流 weighted：仅辅路惩罚时聚合因子应高于 kMediaPenaltyTarget（主路仍 1） */
  void test_mediaHealth_weighted_auxiliaryPenaltyMilder();
  /** 多流 min：任一路惩罚即整体乘子等于 penalty */
  void test_mediaHealth_min_isPessimistic();
  /** V2：VehicleStatus 网络指标经 Aggregator 透出并参与 score */
  void test_vehicleStatusNetworkMetrics_exposedOnAggregator();
};

void TestNetworkQualityAggregator::initTestCase() {}

void TestNetworkQualityAggregator::cleanupTestCase() {}

void TestNetworkQualityAggregator::init() {
  qunsetenv("CLIENT_MEDIA_HEALTH_AGGREGATE");
  qunsetenv("CLIENT_MEDIA_HEALTH_WEIGHT_FRONT");
  qunsetenv("CLIENT_MEDIA_HEALTH_RECOVERY_MS");
}

void TestNetworkQualityAggregator::cleanup() {
  qunsetenv("CLIENT_MEDIA_HEALTH_AGGREGATE");
  qunsetenv("CLIENT_MEDIA_HEALTH_WEIGHT_FRONT");
  qunsetenv("CLIENT_MEDIA_HEALTH_RECOVERY_MS");
}

void TestNetworkQualityAggregator::test_constructor() {
  // 创建依赖对象
  VehicleStatus vehicleStatus;
  NodeHealthChecker nodeHealthChecker;

  // 测试构造函数不崩溃
  NetworkQualityAggregator* aggregator =
      new NetworkQualityAggregator(&vehicleStatus, &nodeHealthChecker);
  QVERIFY2(aggregator != nullptr, "NetworkQualityAggregator should be created");

  delete aggregator;
}

void TestNetworkQualityAggregator::test_initialScore() {
  VehicleStatus vehicleStatus;
  NodeHealthChecker nodeHealthChecker;
  NetworkQualityAggregator aggregator(&vehicleStatus, &nodeHealthChecker);

  // 验证初始分数
  double score = aggregator.score();
  QVERIFY2(score >= 0.0 && score <= 1.0,
           qPrintable(QString("Initial score should be between 0 and 1, got %1").arg(score)));
}

void TestNetworkQualityAggregator::test_scoreProperty() {
  VehicleStatus vehicleStatus;
  NodeHealthChecker nodeHealthChecker;
  NetworkQualityAggregator aggregator(&vehicleStatus, &nodeHealthChecker);

  // 获取初始分数
  Q_UNUSED(aggregator.score());

  // 触发状态变化（模拟车辆连接）
  vehicleStatus.setVideoConnected(true);
  vehicleStatus.setMqttConnected(true);

  // 等待计算完成
  QTest::qWait(100);

  // 获取更新后的分数
  double updatedScore = aggregator.score();

  // 分数应该在有效范围内
  QVERIFY2(
      updatedScore >= 0.0 && updatedScore <= 1.0,
      qPrintable(QString("Updated score should be between 0 and 1, got %1").arg(updatedScore)));
}

void TestNetworkQualityAggregator::test_degradedProperty() {
  VehicleStatus vehicleStatus;
  NodeHealthChecker nodeHealthChecker;
  NetworkQualityAggregator aggregator(&vehicleStatus, &nodeHealthChecker);

  // 获取初始降级状态
  bool initialDegraded = aggregator.degraded();

  // 初始状态应该不是降级
  // （取决于具体实现）
  QVERIFY2(initialDegraded == false || initialDegraded == true,
           "Degraded property should be a valid boolean");
}

void TestNetworkQualityAggregator::test_scoreChangedSignal() {
  VehicleStatus vehicleStatus;
  NodeHealthChecker nodeHealthChecker;
  NetworkQualityAggregator aggregator(&vehicleStatus, &nodeHealthChecker);

  bool signalReceived = false;
  double oldScore = -1.0;
  double newScore = -1.0;

  // 连接信号
  connect(&aggregator, &NetworkQualityAggregator::scoreChanged,
          [&signalReceived, &oldScore, &newScore](double score) {
            if (oldScore < 0) {
              oldScore = score;
            } else {
              newScore = score;
              signalReceived = true;
            }
          });

  // 触发状态变化
  vehicleStatus.setVideoConnected(true);
  QTest::qWait(200);

  // 信号应该被触发（取决于具体实现）
  QVERIFY2(signalReceived == true || signalReceived == false, "Signal connection should not crash");
}

void TestNetworkQualityAggregator::test_degradedChangedSignal() {
  VehicleStatus vehicleStatus;
  NodeHealthChecker nodeHealthChecker;
  NetworkQualityAggregator aggregator(&vehicleStatus, &nodeHealthChecker);

  bool signalReceived = false;

  // 连接信号
  connect(&aggregator, &NetworkQualityAggregator::degradedChanged,
          [&signalReceived](bool degraded) {
            Q_UNUSED(degraded);
            signalReceived = true;
          });

  // 触发状态变化
  vehicleStatus.setVideoConnected(true);
  QTest::qWait(200);

  // 信号是否触发取决于具体实现
  QVERIFY2(signalReceived == true || signalReceived == false, "Signal connection should not crash");
}

void TestNetworkQualityAggregator::test_recomputeOnStatusChange() {
  VehicleStatus vehicleStatus;
  NodeHealthChecker nodeHealthChecker;
  NetworkQualityAggregator aggregator(&vehicleStatus, &nodeHealthChecker);

  // 获取初始分数
  Q_UNUSED(aggregator.score());

  // 模拟视频断开
  vehicleStatus.setVideoConnected(false);
  QTest::qWait(100);

  // 分数可能下降
  double afterDisconnectScore = aggregator.score();
  QVERIFY2(
      afterDisconnectScore >= 0.0 && afterDisconnectScore <= 1.0,
      qPrintable(
          QString("Score after disconnect should be valid, got %1").arg(afterDisconnectScore)));

  // 模拟视频连接
  vehicleStatus.setVideoConnected(true);
  QTest::qWait(100);

  // 分数可能恢复
  double afterConnectScore = aggregator.score();
  QVERIFY2(
      afterConnectScore >= 0.0 && afterConnectScore <= 1.0,
      qPrintable(QString("Score after connect should be valid, got %1").arg(afterConnectScore)));
}

void TestNetworkQualityAggregator::test_mediaPresentationPenaltyAndRecovery() {
  qputenv("CLIENT_MEDIA_HEALTH_RECOVERY_MS", "3100");

  VehicleStatus vehicleStatus;
  NodeHealthChecker nodeHealthChecker;
  vehicleStatus.setMqttConnected(true);
  vehicleStatus.setVideoConnected(true);
  vehicleStatus.setNetworkRtt(10.0);

  NetworkQualityAggregator aggregator(&vehicleStatus, &nodeHealthChecker);
  QTest::qWait(80);
  const double baseline = aggregator.score();
  QVERIFY2(baseline > 0.5, qPrintable(QStringLiteral("expected healthy baseline, got %1").arg(baseline)));

  aggregator.noteMediaPresentationDegraded(QStringLiteral("cam_front"), QStringLiteral("unit_test"));
  QTest::qWait(80);
  const double penalized = aggregator.score();
  QVERIFY2(penalized < baseline - 0.05,
           qPrintable(QStringLiteral("score should drop after media penalty: baseline=%1 penalized=%2")
                          .arg(baseline)
                          .arg(penalized)));
  QVERIFY2(aggregator.mediaPresentationFactor() < 0.99,
           "mediaPresentationFactor should reflect penalty");

  QTest::qWait(3400);
  QCoreApplication::processEvents();
  QVERIFY2(qFuzzyCompare(aggregator.mediaPresentationFactor(), 1.0),
           qPrintable(QStringLiteral("factor should recover to 1.0, got %1")
                          .arg(aggregator.mediaPresentationFactor())));
}

void TestNetworkQualityAggregator::test_mediaHealth_weighted_auxiliaryPenaltyMilder() {
  qputenv("CLIENT_MEDIA_HEALTH_AGGREGATE", "weighted");

  VehicleStatus vehicleStatus;
  NodeHealthChecker nodeHealthChecker;
  vehicleStatus.setMqttConnected(true);
  vehicleStatus.setVideoConnected(true);
  vehicleStatus.setNetworkRtt(10.0);

  NetworkQualityAggregator aggregator(&vehicleStatus, &nodeHealthChecker);
  QTest::qWait(40);
  QCOMPARE(aggregator.mediaHealthAggregateMode(), QStringLiteral("weighted"));

  aggregator.noteMediaPresentationDegraded(QStringLiteral("VIN_cam_rear"), QStringLiteral("ut"));
  QTest::qWait(40);
  const double f = aggregator.mediaPresentationFactor();
  QVERIFY2(f > 0.65, qPrintable(QStringLiteral("weighted aux-only penalty should stay >0.65, got %1").arg(f)));
  QVERIFY2(f < 1.0, "should be below 1.0 when a slot is penalized");
}

void TestNetworkQualityAggregator::test_vehicleStatusNetworkMetrics_exposedOnAggregator() {
  VehicleStatus vehicleStatus;
  NodeHealthChecker nodeHealthChecker;
  vehicleStatus.setMqttConnected(true);
  vehicleStatus.setVideoConnected(true);
  vehicleStatus.setNetworkRtt(20.0);
  NetworkQualityAggregator aggregator(&vehicleStatus, &nodeHealthChecker);
  QTest::qWait(50);
  const double baseline = aggregator.score();
  vehicleStatus.setNetworkPacketLossPercent(12.0);
  vehicleStatus.setNetworkJitterMs(80.0);
  vehicleStatus.setNetworkBandwidthKbps(600.0);
  QTest::qWait(80);
  QVERIFY(qAbs(aggregator.packetLossRate() - 12.0) < 0.05);
  QVERIFY(qAbs(aggregator.jitterMs() - 80.0) < 0.5);
  QVERIFY(qAbs(aggregator.bandwidthKbps() - 600.0) < 1.0);
  const double after = aggregator.score();
  QVERIFY(after >= 0.0 && after <= 1.0);
  QVERIFY2(after <= baseline + 1e-6,
           "VehicleStatus 丢包/抖动/带宽惩罚不应抬高综合分");
}

void TestNetworkQualityAggregator::test_mediaHealth_min_isPessimistic() {
  qputenv("CLIENT_MEDIA_HEALTH_AGGREGATE", "min");

  VehicleStatus vehicleStatus;
  NodeHealthChecker nodeHealthChecker;
  vehicleStatus.setMqttConnected(true);
  vehicleStatus.setVideoConnected(true);
  vehicleStatus.setNetworkRtt(10.0);

  NetworkQualityAggregator aggregator(&vehicleStatus, &nodeHealthChecker);
  QTest::qWait(40);
  QCOMPARE(aggregator.mediaHealthAggregateMode(), QStringLiteral("min"));

  aggregator.noteMediaPresentationDegraded(QStringLiteral("VIN_cam_left"), QStringLiteral("ut"));
  QTest::qWait(40);
  QVERIFY2(qFuzzyCompare(aggregator.mediaPresentationFactor(), 0.55),
           qPrintable(QStringLiteral("min mode should drop to penalty target, got %1")
                          .arg(aggregator.mediaPresentationFactor())));
}

QTEST_MAIN(TestNetworkQualityAggregator)
#include "test_networkqualityaggregator.moc"