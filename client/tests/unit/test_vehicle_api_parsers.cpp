#include "services/vehicle_api_parsers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

class VehicleApiParsersTest : public QObject {
  Q_OBJECT
 private slots:
  void list_vinsStringArray_flattensToObjects();
  void list_dataArray_passThrough();
  void list_invalidTopLevel_fails();
  void session_ok_parsesMediaAndControl();
  void session_vinMismatch_fails();
  void session_missingSessionId_fails();
  void session_emptyResponseVin_fallsBackToRequestVin();
};

void VehicleApiParsersTest::list_vinsStringArray_flattensToObjects() {
  QJsonObject root;
  root[QStringLiteral("vins")] = QJsonArray{QStringLiteral("VIN1"), QStringLiteral("VIN2")};
  QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);
  QJsonArray out;
  QString err;
  QVERIFY(rd_client_api::parseVehicleListHttpBody(body, &out, &err));
  QVERIFY(err.isEmpty());
  QCOMPARE(out.size(), 2);
  QCOMPARE(out.at(0).toObject().value(QStringLiteral("vin")).toString(), QStringLiteral("VIN1"));
  QCOMPARE(out.at(1).toObject().value(QStringLiteral("vin")).toString(), QStringLiteral("VIN2"));
}

void VehicleApiParsersTest::list_dataArray_passThrough() {
  QJsonObject v;
  v[QStringLiteral("vin")] = QStringLiteral("X");
  v[QStringLiteral("name")] = QStringLiteral("Car");
  QJsonObject root;
  root[QStringLiteral("data")] = QJsonArray{v};
  QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);
  QJsonArray out;
  QString err;
  QVERIFY(rd_client_api::parseVehicleListHttpBody(body, &out, &err));
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Car"));
}

void VehicleApiParsersTest::list_invalidTopLevel_fails() {
  QJsonObject root;
  root[QStringLiteral("nope")] = 1;
  QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);
  QJsonArray out;
  QString err;
  QVERIFY(!rd_client_api::parseVehicleListHttpBody(body, &out, &err));
  QVERIFY(!err.isEmpty());
}

void VehicleApiParsersTest::session_ok_parsesMediaAndControl() {
  QJsonObject control;
  control[QStringLiteral("mqtt_broker_url")] = QStringLiteral("tcp://broker:1883");
  QJsonObject media;
  media[QStringLiteral("whip")] = QStringLiteral("w://a");
  media[QStringLiteral("whep")] = QStringLiteral("w://b");
  QJsonObject root;
  root[QStringLiteral("sessionId")] = QStringLiteral("sess-1");
  root[QStringLiteral("vin")] = QStringLiteral("REQVIN");
  root[QStringLiteral("media")] = media;
  root[QStringLiteral("control")] = control;
  QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);

  auto out = rd_client_api::parseSessionCreateHttpBody(body, QStringLiteral("REQVIN"));
  QVERIFY(out.ok);
  QCOMPARE(out.sessionId, QStringLiteral("sess-1"));
  QCOMPARE(out.canonicalVin, QStringLiteral("REQVIN"));
  QCOMPARE(out.whipUrl, QStringLiteral("w://a"));
  QCOMPARE(out.whepUrl, QStringLiteral("w://b"));
  QCOMPARE(out.controlConfig.value(QStringLiteral("mqtt_broker_url")).toString(),
           QStringLiteral("tcp://broker:1883"));
}

void VehicleApiParsersTest::session_vinMismatch_fails() {
  QJsonObject root;
  root[QStringLiteral("sessionId")] = QStringLiteral("sess-1");
  root[QStringLiteral("vin")] = QStringLiteral("OTHER");
  QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);
  auto out = rd_client_api::parseSessionCreateHttpBody(body, QStringLiteral("REQVIN"));
  QVERIFY(!out.ok);
  QVERIFY(!out.error.isEmpty());
}

void VehicleApiParsersTest::session_missingSessionId_fails() {
  QJsonObject root;
  root[QStringLiteral("vin")] = QStringLiteral("REQVIN");
  QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);
  auto out = rd_client_api::parseSessionCreateHttpBody(body, QStringLiteral("REQVIN"));
  QVERIFY(!out.ok);
}

void VehicleApiParsersTest::session_emptyResponseVin_fallsBackToRequestVin() {
  QJsonObject root;
  root[QStringLiteral("sessionId")] = QStringLiteral("s2");
  QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);
  auto out = rd_client_api::parseSessionCreateHttpBody(body, QStringLiteral("REQVIN"));
  QVERIFY(out.ok);
  QCOMPARE(out.canonicalVin, QStringLiteral("REQVIN"));
}

QTEST_MAIN(VehicleApiParsersTest)
#include "test_vehicle_api_parsers.moc"
