#include "core/tracing.h"
#include "infrastructure/itransportmanager.h"
#include "services/vehiclecontrolservice.h"
#include "vehiclemanager.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include "message_types_generated.h"
#pragma GCC diagnostic pop

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QtTest/QtTest>

#include <cmath>

namespace {

class RecordingTransport final : public ITransportManager {
 public:
  QByteArray lastControlJson;

  explicit RecordingTransport(QObject *parent = nullptr)
      : ITransportManager(parent), lastControlJson() {}

  bool initialize(const TransportConfig &) override { return true; }
  void shutdown() override {}
  void connectAsync(const EndpointInfo &) override {}
  void disconnect() override {}

  SendResult send(TransportChannel channel, const uint8_t *data, size_t len,
                  SendFlags /*flags*/) override {
    if (channel == TransportChannel::CONTROL_CRITICAL) {
      lastControlJson = QByteArray(reinterpret_cast<const char *>(data), static_cast<int>(len));
    }
    return SendResult{true, QString()};
  }

  void registerReceiver(
      TransportChannel /*channel*/,
      std::function<void(const uint8_t *, size_t, const PacketMetadata &)> /*callback*/) override {}

  NetworkQuality getNetworkQuality() const override { return {}; }

  ChannelStats getChannelStats(TransportChannel /*ch*/) const override { return {}; }

  ConnectionState connectionState() const override { return ConnectionState::CONNECTED; }
};

}  // namespace

class TestVehicleControlService : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestVehicleControlService)
 public:
  explicit TestVehicleControlService(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void sendUiCommand_buildsEnvelopeWithTraceAndSession();
  void controlLoop_start_sendsDriveCommandPayloadViaTransport();
  void setControlConfig_zeroHz_clamped_initializeNoCrash();
  void start_idempotent_secondStartDoesNotBreak();
  void watchdog_silentInput_triggersAfterThreshold();
};

void TestVehicleControlService::sendUiCommand_buildsEnvelopeWithTraceAndSession() {
  VehicleManager vm;
  vm.addTestVehicle(QStringLiteral("VIN_UNIT_TEST"), QStringLiteral("unit"));
  vm.setCurrentVin(QStringLiteral("VIN_UNIT_TEST"));

  RecordingTransport transport;
  VehicleControlService vcs(nullptr, &vm, nullptr, nullptr); // 加入 InputSampler* 参数
  vcs.setTransport(&transport);
  QVERIFY(vcs.initialize());

  Tracing::instance().setCurrentTraceId(QStringLiteral("trace-unit-test-aaaaaaaa"));
  vcs.setSessionCredentials(QStringLiteral("VIN_UNIT_TEST"), QStringLiteral("session-unit-xyz"),
                            QStringLiteral("token"));
  QTest::qWait(100);

  QVariantMap payload;
  payload.insert(QStringLiteral("value"), 3);
  vcs.sendUiCommand(QStringLiteral("gear"), payload);

  QVERIFY(!transport.lastControlJson.isEmpty());
  QJsonParseError err{};
  QJsonDocument doc = QJsonDocument::fromJson(transport.lastControlJson, &err);
  QVERIFY2(err.error == QJsonParseError::NoError, err.errorString().toUtf8().constData());
  QVERIFY(doc.isObject());
  const QJsonObject o = doc.object();

  QCOMPARE(o.value(QStringLiteral("type")).toString(), QStringLiteral("gear"));
  QCOMPARE(o.value(QStringLiteral("schemaVersion")).toString(), QStringLiteral("1.2.0"));
  QCOMPARE(o.value(QStringLiteral("vin")).toString(), QStringLiteral("VIN_UNIT_TEST"));
  QCOMPARE(o.value(QStringLiteral("sessionId")).toString(), QStringLiteral("session-unit-xyz"));
  QCOMPARE(o.value(QStringLiteral("trace_id")).toString(),
           QStringLiteral("trace-unit-test-aaaaaaaa"));
  QVERIFY(o.contains(QStringLiteral("timestampMs")));
  QVERIFY(o.contains(QStringLiteral("seq")));

  QVERIFY(o.value(QStringLiteral("payload")).isObject());
  QCOMPARE(o.value(QStringLiteral("payload")).toObject().value(QStringLiteral("value")).toInt(), 3);
}

void TestVehicleControlService::controlLoop_start_sendsDriveCommandPayloadViaTransport() {
  VehicleManager vm;
  vm.addTestVehicle(QStringLiteral("VIN_LOOP"), QStringLiteral("loop"));
  vm.setCurrentVin(QStringLiteral("VIN_LOOP"));

  RecordingTransport transport;
  VehicleControlService vcs(nullptr, &vm, nullptr, nullptr, this);
  vcs.setTransport(&transport);
  QVERIFY(vcs.initialize());
  vcs.setSessionCredentials(QStringLiteral("VIN_LOOP"), QStringLiteral("session-loop"),
                            QStringLiteral("token-loop"));
  QTest::qWait(50);

  vcs.start();
  vcs.sendDriveCommand(0.6, 0.35, 0.05);
  // 100Hz + 转向速率限制与非线性曲线：需足够时间使输出逼近目标
  QTest::qWait(400);

  QVERIFY2(!transport.lastControlJson.isEmpty(), "control tick should publish via transport");
  
  // 验证 FlatBuffer 格式
  auto driveCmd = teleop::protocol::GetDriveCommand(transport.lastControlJson.constData());
  QVERIFY(driveCmd != nullptr);
  QVERIFY(driveCmd->header() != nullptr);
  QCOMPARE(QString::fromUtf8(driveCmd->header()->vin()->c_str()), QStringLiteral("VIN_LOOP"));
  
  const double st = driveCmd->steering();
  const double thr = driveCmd->throttle();
  QVERIFY2(std::abs(st) > 0.2 && std::abs(st) < 0.55,
           "steering should ramp toward curved target (~0.46 for raw 0.6)");
  QVERIFY2(thr > 0.25 && thr < 0.45,
           "throttle should reflect sendDriveCommand after curve/deadzone");

  vcs.stop();
  QTest::qWait(40);
}

void TestVehicleControlService::setControlConfig_zeroHz_clamped_initializeNoCrash() {
  VehicleControlService vcs(nullptr, nullptr, nullptr, nullptr, this);
  VehicleControlService::ControlConfig c;
  c.controlRateHz = 0;
  vcs.setControlConfig(c);
  QVERIFY(vcs.initialize());
  vcs.start();
  QTest::qWait(30);
  vcs.stop();
}

void TestVehicleControlService::start_idempotent_secondStartDoesNotBreak() {
  VehicleManager vm;
  vm.addTestVehicle(QStringLiteral("VIN_IDEM"), QStringLiteral("i"));
  vm.setCurrentVin(QStringLiteral("VIN_IDEM"));
  RecordingTransport transport;
  VehicleControlService vcs(nullptr, &vm, nullptr, nullptr, this);
  vcs.setTransport(&transport);
  QVERIFY(vcs.initialize());
  vcs.setSessionCredentials(QStringLiteral("VIN_IDEM"), QStringLiteral("s"), QStringLiteral("t"));
  QTest::qWait(50);
  vcs.start();
  vcs.start();
  vcs.sendDriveCommand(0.0, 0.0, 0.0);
  QTest::qWait(60);
  QVERIFY(!transport.lastControlJson.isEmpty());
  vcs.stop();
}

void TestVehicleControlService::watchdog_silentInput_triggersAfterThreshold() {
  VehicleManager vm;
  vm.addTestVehicle(QStringLiteral("VIN_WATCHDOG"), QStringLiteral("w"));
  vm.setCurrentVin(QStringLiteral("VIN_WATCHDOG"));

  SafetyMonitorService safety(nullptr, nullptr, nullptr);
  VehicleControlService vcs(nullptr, &vm, nullptr, nullptr, this);
  vcs.setSafetyMonitor(&safety);
  
  VehicleControlService::ControlConfig cfg;
  cfg.silentThresholdMs = 200; // 设置较短阈值便于测试
  vcs.setControlConfig(cfg);
  
  QVERIFY(vcs.initialize());
  vcs.setSessionCredentials(QStringLiteral("VIN_WATCHDOG"), QStringLiteral("s"), QStringLiteral("t"));
  QTest::qWait(50);
  
  // 预热：发送一次有效输入，确保 m_lastEffectiveInputMs 被初始化为当前时间
  vcs.sendDriveCommand(0.1, 0.0, 0.0);
  vcs.start();
  QTest::qWait(50);
  vcs.sendDriveCommand(0.0, 0.0, 0.0); // 恢复 0

  // 1. 初始状态：非静默
  QCOMPARE(vcs.inputLinkSilent(), false);

  // 2. 模拟 UI 活动，但无有效控制输入
  safety.onOperatorActivity(); // 触发 operatorActivityReported 信号
  QTest::qWait(100); 
  // 此时还在阈值 (200ms) 内，且 m_lastEffectiveInputMs 是 100ms 前，应仍为 false
  QCOMPARE(vcs.inputLinkSilent(), false);

  // 3. 等待超过阈值
  QTest::qWait(150);
  // 此时 UI 活动还在 1s 活动窗口内，且距离上次有效输入已超过 200ms (100+150=250ms)，应触发 true
  QCOMPARE(vcs.inputLinkSilent(), true);

  // 4. 模拟有效输入进入，应恢复为 false
  vcs.sendDriveCommand(0.5, 0.0, 0.0);
  QTest::qWait(50);
  QCOMPARE(vcs.inputLinkSilent(), false);

  vcs.stop();
}

QTEST_MAIN(TestVehicleControlService)
#include "test_vehiclecontrolservice.moc"
