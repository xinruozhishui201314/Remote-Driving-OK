#include "utils/MqttControlEnvelope.h"

#include <QJsonObject>
#include <QtTest/QtTest>

using namespace MqttControlEnvelope;

class TestMqttControlEnvelope : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestMqttControlEnvelope)
 public:
  explicit TestMqttControlEnvelope(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void parsePreferredChannel_variants();
  void prepareForSend_adds_vin_timestamp_seq();
  void prepareForSend_rejects_empty_vin();
  void prepareForSend_preserves_existing_seq_no_extra_increment();
  void buildSteering_clamps();
  void buildBrake_includes_seq();
  void buildDrive_clamps();
  void buildUiCommandEnvelope_fields();
};

void TestMqttControlEnvelope::parsePreferredChannel_variants() {
  QCOMPARE(int(parsePreferredChannel(QStringLiteral("webrtc"))),
           int(PreferredChannel::DataChannel));
  QCOMPARE(int(parsePreferredChannel(QStringLiteral("mqtt"))), int(PreferredChannel::Mqtt));
  QCOMPARE(int(parsePreferredChannel(QStringLiteral("WS"))), int(PreferredChannel::WebSocket));
  QCOMPARE(int(parsePreferredChannel(QStringLiteral("  "))), int(PreferredChannel::Auto));
}

void TestMqttControlEnvelope::prepareForSend_adds_vin_timestamp_seq() {
  QJsonObject in;
  in[QStringLiteral("type")] = QStringLiteral("gear");
  in[QStringLiteral("value")] = 1;
  uint32_t seq = 10;
  const auto r = prepareForSend(in, QStringLiteral("VIN99"), 12345LL, seq);
  QVERIFY(r.ok);
  QCOMPARE(r.cmd.value(QStringLiteral("vin")).toString(), QStringLiteral("VIN99"));
  QCOMPARE(r.cmd.value(QStringLiteral("timestampMs")).toVariant().toLongLong(), 12345LL);
  QCOMPARE(r.cmd.value(QStringLiteral("schemaVersion")).toString(), QStringLiteral("1.2.0"));
  QCOMPARE(r.cmd.value(QStringLiteral("seq")).toInt(), 11);
  QCOMPARE(seq, 11u);
}

void TestMqttControlEnvelope::prepareForSend_rejects_empty_vin() {
  QJsonObject in;
  in[QStringLiteral("type")] = QStringLiteral("x");
  uint32_t seq = 0;
  const auto r = prepareForSend(in, QString(), 1LL, seq);
  QVERIFY(!r.ok);
}

void TestMqttControlEnvelope::prepareForSend_preserves_existing_seq_no_extra_increment() {
  QJsonObject in;
  in[QStringLiteral("vin")] = QStringLiteral("V1");
  in[QStringLiteral("seq")] = 99;
  uint32_t seq = 5;
  const auto r = prepareForSend(in, QString(), 1LL, seq);
  QVERIFY(r.ok);
  QCOMPARE(r.cmd.value(QStringLiteral("seq")).toInt(), 99);
  QCOMPARE(seq, 5u);
}

void TestMqttControlEnvelope::buildSteering_clamps() {
  const auto o = buildSteering(2.0, 100LL);
  QCOMPARE(o.value(QStringLiteral("value")).toDouble(), 1.0);
  QCOMPARE(o.value(QStringLiteral("type")).toString(), QStringLiteral("steering"));
}

void TestMqttControlEnvelope::buildBrake_includes_seq() {
  const auto o = buildBrake(0.5, 1LL, 42u);
  QCOMPARE(o.value(QStringLiteral("seq")).toInt(), 42);
}

void TestMqttControlEnvelope::buildDrive_clamps() {
  const auto o = buildDrive(9.0, 9.0, 9.0, 2, 0LL);
  QCOMPARE(o.value(QStringLiteral("steering")).toDouble(), 1.0);
  QCOMPARE(o.value(QStringLiteral("throttle")).toDouble(), 1.0);
  QCOMPARE(o.value(QStringLiteral("brake")).toDouble(), 1.0);
  QCOMPARE(o.value(QStringLiteral("emergency_stop")).toBool(), false);
}

void TestMqttControlEnvelope::buildUiCommandEnvelope_fields() {
  QJsonObject pl;
  pl[QStringLiteral("value")] = 2;
  const auto o = buildUiCommandEnvelope(QStringLiteral("gear"), pl, QStringLiteral("VIN_X"),
                                        QStringLiteral("sess_y"), 999LL, 42LL,
                                        QStringLiteral("trace_z"));
  QCOMPARE(o.value(QStringLiteral("schemaVersion")).toString(), QStringLiteral("1.2.0"));
  QCOMPARE(o.value(QStringLiteral("type")).toString(), QStringLiteral("gear"));
  QCOMPARE(o.value(QStringLiteral("vin")).toString(), QStringLiteral("VIN_X"));
  QCOMPARE(o.value(QStringLiteral("sessionId")).toString(), QStringLiteral("sess_y"));
  QCOMPARE(o.value(QStringLiteral("timestampMs")).toVariant().toLongLong(), 999LL);
  QCOMPARE(o.value(QStringLiteral("seq")).toVariant().toLongLong(), 42LL);
  QCOMPARE(o.value(QStringLiteral("trace_id")).toString(), QStringLiteral("trace_z"));
  QVERIFY(o.value(QStringLiteral("payload")).isObject());
  QCOMPARE(o.value(QStringLiteral("payload")).toObject().value(QStringLiteral("value")).toInt(), 2);
}

QTEST_MAIN(TestMqttControlEnvelope)
#include "test_mqttcontrolenvelope.moc"
