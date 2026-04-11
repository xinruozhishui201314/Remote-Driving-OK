/**
 * L3 集成夹具：验证 Paho async_client 可与 MQTT broker 建立连接（不链接 MqttController）。
 * 未设置 MQTT_TEST_BROKER 时 QSKIP（CTest 仍绿）。
 *
 * 配套：scripts/run-client-mqtt-integration-fixture.sh
 */
#include <QUrl>
#include <QtTest/QtTest>

#ifdef ENABLE_MQTT_PAHO
#include <chrono>
#include <mqtt/async_client.h>
#include <mqtt/connect_options.h>
#endif

class TestMqttPahoBrokerSmoke : public QObject {
  Q_OBJECT

 private slots:
  void connect_to_broker_when_env_set() {
#ifndef ENABLE_MQTT_PAHO
    QSKIP("Built without ENABLE_MQTT_PAHO");
#else
    const QByteArray raw = qgetenv("MQTT_TEST_BROKER");
    if (raw.isEmpty())
      QSKIP("MQTT_TEST_BROKER unset (e.g. tcp://127.0.0.1:18883)");

    const QUrl u(QString::fromUtf8(raw));
    QVERIFY2(u.isValid() && !u.host().isEmpty(), "MQTT_TEST_BROKER must be valid URL with host");
    const std::string server =
        QStringLiteral("%1:%2").arg(u.host()).arg(u.port(1883)).toStdString();

    mqtt::async_client client(server, "client_paho_smoke");
    mqtt::connect_options opts;
    opts.set_clean_session(true);
    opts.set_connect_timeout(5);
    opts.set_keep_alive_interval(20);

    bool connected = false;
    client.set_connected_handler([&](const std::string&) { connected = true; });
    client.connect(opts);

    for (int i = 0; i < 100 && !connected; ++i)
      QTest::qWait(50);

    QVERIFY2(connected, "Paho did not connect within ~5s");
    try {
      auto tok = client.disconnect();
      if (tok)
        tok->wait();
    } catch (...) {
    }
#endif
  }
};

QTEST_MAIN(TestMqttPahoBrokerSmoke)
#include "test_mqtt_paho_broker_smoke.moc"
