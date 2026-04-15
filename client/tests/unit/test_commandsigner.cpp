#include "core/commandsigner.h"

#include <QJsonObject>
#include <QtTest/QtTest>

class TestCommandSigner : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestCommandSigner)
 public:
  explicit TestCommandSigner(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void not_ready_sign_fails();
  void sign_and_verify_roundtrip();
  void verify_fails_on_tamper();
  void clearCredentials_clears_ready();
  void computeHmac_deterministic();
  void verify_missing_hmac_fails();
  void verify_not_ready_fails();
  void verify_malformed_hex_hmac_fails();
  void verify_wrong_hmac_byte_length_fails();
};

void TestCommandSigner::not_ready_sign_fails() {
  CommandSigner s;
  QVERIFY(!s.isReady());
  QJsonObject o;
  o[QStringLiteral("vin")] = QStringLiteral("VIN1");
  QVERIFY(!s.sign(o));
}

void TestCommandSigner::sign_and_verify_roundtrip() {
  CommandSigner s;
  s.setCredentials(QStringLiteral("VIN_X"), QStringLiteral("sess_y"), QStringLiteral("tok_z"));
  QVERIFY(s.isReady());

  QJsonObject o;
  o[QStringLiteral("vin")] = QStringLiteral("VIN_X");
  o[QStringLiteral("sessionId")] = QStringLiteral("sess_y");
  o[QStringLiteral("steering")] = 0.1;
  o[QStringLiteral("throttle")] = 0.2;
  o[QStringLiteral("brake")] = 0.0;
  o[QStringLiteral("gear")] = 1;
  o[QStringLiteral("emergency_stop")] = false;
  o[QStringLiteral("timestampMs")] = 12345;
  o[QStringLiteral("seq")] = 7;

  QVERIFY(s.sign(o));
  QVERIFY(o.contains(QStringLiteral("hmac")));

  QString reason;
  QVERIFY2(s.verify(o, &reason), qPrintable(reason));
}

void TestCommandSigner::verify_fails_on_tamper() {
  CommandSigner s;
  s.setCredentials(QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"));
  QJsonObject o;
  o[QStringLiteral("vin")] = QStringLiteral("A");
  o[QStringLiteral("sessionId")] = QStringLiteral("B");
  o[QStringLiteral("steering")] = 0.0;
  o[QStringLiteral("throttle")] = 0.0;
  o[QStringLiteral("brake")] = 0.0;
  o[QStringLiteral("gear")] = 0;
  o[QStringLiteral("emergency_stop")] = false;
  o[QStringLiteral("timestampMs")] = 1;
  o[QStringLiteral("seq")] = 1;
  QVERIFY(s.sign(o));
  o[QStringLiteral("throttle")] = 0.99;
  QString reason;
  QVERIFY(!s.verify(o, &reason));
  QVERIFY(!reason.isEmpty());
}

void TestCommandSigner::clearCredentials_clears_ready() {
  CommandSigner s;
  s.setCredentials(QStringLiteral("v"), QStringLiteral("s"), QStringLiteral("t"));
  QVERIFY(s.isReady());
  s.clearCredentials();
  QVERIFY(!s.isReady());
}

void TestCommandSigner::computeHmac_deterministic() {
  CommandSigner s;
  s.setCredentials(QStringLiteral("v"), QStringLiteral("s"), QStringLiteral("t"));
  const QByteArray p1 = QByteArrayLiteral("payload-a");
  const QByteArray p2 = QByteArrayLiteral("payload-a");
  QCOMPARE(s.computeHmac(p1), s.computeHmac(p2));
}

void TestCommandSigner::verify_missing_hmac_fails() {
  CommandSigner s;
  s.setCredentials(QStringLiteral("v"), QStringLiteral("s"), QStringLiteral("t"));
  QJsonObject o;
  o[QStringLiteral("vin")] = QStringLiteral("v");
  QString reason;
  QVERIFY(!s.verify(o, &reason));
  QVERIFY(reason.contains(QStringLiteral("hmac")));
}

void TestCommandSigner::verify_not_ready_fails() {
  CommandSigner s;
  QJsonObject o;
  o[QStringLiteral("hmac")] = QStringLiteral("ab");
  QString reason;
  QVERIFY(!s.verify(o, &reason));
  QVERIFY(!reason.isEmpty());
}

void TestCommandSigner::verify_malformed_hex_hmac_fails() {
  CommandSigner s;
  s.setCredentials(QStringLiteral("v"), QStringLiteral("s"), QStringLiteral("t"));
  QJsonObject o;
  o[QStringLiteral("vin")] = QStringLiteral("v");
  o[QStringLiteral("sessionId")] = QStringLiteral("s");
  o[QStringLiteral("steering")] = 0.0;
  o[QStringLiteral("throttle")] = 0.0;
  o[QStringLiteral("brake")] = 0.0;
  o[QStringLiteral("gear")] = 0;
  o[QStringLiteral("emergency_stop")] = false;
  o[QStringLiteral("timestampMs")] = 1;
  o[QStringLiteral("seq")] = 1;
  QVERIFY(s.sign(o));
  o[QStringLiteral("hmac")] = QStringLiteral("ZZ");  // invalid hex
  QString reason;
  QVERIFY(!s.verify(o, &reason));
  QVERIFY(!reason.isEmpty());
}

void TestCommandSigner::verify_wrong_hmac_byte_length_fails() {
  CommandSigner s;
  s.setCredentials(QStringLiteral("v"), QStringLiteral("s"), QStringLiteral("t"));
  QJsonObject o;
  o[QStringLiteral("vin")] = QStringLiteral("v");
  o[QStringLiteral("sessionId")] = QStringLiteral("s");
  o[QStringLiteral("steering")] = 0.0;
  o[QStringLiteral("throttle")] = 0.0;
  o[QStringLiteral("brake")] = 0.0;
  o[QStringLiteral("gear")] = 0;
  o[QStringLiteral("emergency_stop")] = false;
  o[QStringLiteral("timestampMs")] = 1;
  o[QStringLiteral("seq")] = 1;
  QVERIFY(s.sign(o));
  o[QStringLiteral("hmac")] = QStringLiteral("00");  // one byte vs 32-byte digest hex
  QString reason;
  QVERIFY(!s.verify(o, &reason));
  QVERIFY(reason.contains(QStringLiteral("length")) || !reason.isEmpty());
}

QTEST_MAIN(TestCommandSigner)
#include "test_commandsigner.moc"
