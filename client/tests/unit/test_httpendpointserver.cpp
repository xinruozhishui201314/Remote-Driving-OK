#include "core/httpendpointserver.h"
#include "core/metricscollector.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QtTest/QtTest>

namespace {

static bool httpGet(quint16 port, const QByteArray &path, int *statusCode, QByteArray *body,
                    int timeoutMs = 8000) {
  QTcpSocket s;
  s.connectToHost(QHostAddress::LocalHost, port);
  if (!s.waitForConnected(3000))
    return false;

  const QByteArray req =
      QByteArrayLiteral("GET ") + path +
      QByteArrayLiteral(" HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
  s.write(req);
  if (!s.waitForBytesWritten(3000))
    return false;

  QByteArray acc;
  QElapsedTimer et;
  et.start();
  while (et.elapsed() < timeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (s.waitForReadyRead(80))
      acc += s.readAll();
    if (s.state() == QAbstractSocket::UnconnectedState && !acc.isEmpty())
      break;
  }
  acc += s.readAll();

  const int sep = acc.indexOf("\r\n\r\n");
  if (sep < 0)
    return false;
  const QByteArray statusLine = acc.left(sep).split('\r').value(0);
  *body = acc.mid(sep + 4);
  const QList<QByteArray> parts = statusLine.split(' ');
  if (parts.size() < 2)
    return false;
  *statusCode = parts.at(1).toInt();
  return *statusCode > 0;
}

static QJsonObject parseJsonObject(const QByteArray &utf8) {
  QJsonParseError err;
  const QJsonDocument d = QJsonDocument::fromJson(utf8, &err);
  if (err.error != QJsonParseError::NoError || !d.isObject())
    return {};
  return d.object();
}

}  // namespace

class TestHttpEndpointServer : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestHttpEndpointServer)
 public:
  explicit TestHttpEndpointServer(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void init();
  void cleanup();
  void start_stop_ephemeral_port();
  void get_health_ok_json();
  void get_ready_reflects_setReadyStatus();
  void get_metrics_includes_http_counter();
  void get_metrics_json_merges_collector();
  void custom_handler_and_unregister();
  void custom_json_handler();
  void not_found();
  void bad_request_line();
  void setHealthStatus_degraded();
  void requestReceived_signal();

 private:
  HttpEndpointServer &srv() { return HttpEndpointServer::instance(); }
};

void TestHttpEndpointServer::init() { srv().stop(); }

void TestHttpEndpointServer::cleanup() {
  srv().unregisterHandler(QStringLiteral("/ut_custom"));
  srv().unregisterHandler(QStringLiteral("/ut_json"));
  srv().stop();
}

void TestHttpEndpointServer::start_stop_ephemeral_port() {
  QVERIFY(!srv().isRunning());
  QVERIFY(srv().start(0));
  QVERIFY(srv().isRunning());
  QVERIFY(srv().port() > 0);
  srv().stop();
  QVERIFY(!srv().isRunning());
}

void TestHttpEndpointServer::get_health_ok_json() {
  QVERIFY(srv().start(0));
  const quint16 p = static_cast<quint16>(srv().port());
  int st = 0;
  QByteArray body;
  QVERIFY(httpGet(p, QByteArrayLiteral("/health"), &st, &body));
  QCOMPARE(st, 200);
  const QJsonObject o = parseJsonObject(body);
  QCOMPARE(o.value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
  QVERIFY(o.contains(QStringLiteral("components")));
  srv().stop();
}

void TestHttpEndpointServer::get_ready_reflects_setReadyStatus() {
  QVERIFY(srv().start(0));
  const quint16 p = static_cast<quint16>(srv().port());
  srv().setReadyStatus(false, QStringLiteral("warming"));
  int st = 0;
  QByteArray body;
  QVERIFY(httpGet(p, QByteArrayLiteral("/ready"), &st, &body));
  QCOMPARE(st, 200);
  QJsonObject o = parseJsonObject(body);
  QCOMPARE(o.value(QStringLiteral("ready")).toBool(), false);
  QCOMPARE(o.value(QStringLiteral("reason")).toString(), QStringLiteral("warming"));

  srv().setReadyStatus(true, QString());
  QVERIFY(httpGet(p, QByteArrayLiteral("/ready"), &st, &body));
  o = parseJsonObject(body);
  QCOMPARE(o.value(QStringLiteral("ready")).toBool(), true);
  srv().stop();
}

void TestHttpEndpointServer::get_metrics_includes_http_counter() {
  QVERIFY(srv().start(0));
  const quint16 p = static_cast<quint16>(srv().port());
  srv().setMetric(QStringLiteral("ut_gauge"), 3.5);
  int st = 0;
  QByteArray body;
  QVERIFY(httpGet(p, QByteArrayLiteral("/metrics"), &st, &body));
  QCOMPARE(st, 200);
  QVERIFY(body.contains(QByteArrayLiteral("http_requests_total")));
  QVERIFY(body.contains(QByteArrayLiteral("ut_gauge")));
  QVERIFY(body.contains(QByteArrayLiteral("http_endpoint_up")));
  srv().stop();
}

void TestHttpEndpointServer::get_metrics_json_merges_collector() {
  MetricsCollector::instance().set(QStringLiteral("ut_mc"), 7.0);
  QVERIFY(srv().start(0));
  const quint16 p = static_cast<quint16>(srv().port());
  int st = 0;
  QByteArray body;
  QVERIFY(httpGet(p, QByteArrayLiteral("/metrics/json"), &st, &body));
  QCOMPARE(st, 200);
  const QJsonObject root = parseJsonObject(body);
  QVERIFY(root.value(QStringLiteral("up")).toBool());
  QVERIFY(root.contains(QStringLiteral("metricsCollector")));
  srv().stop();
}

void TestHttpEndpointServer::custom_handler_and_unregister() {
  QVERIFY(srv().start(0));
  const quint16 p = static_cast<quint16>(srv().port());
  srv().registerHandler(QStringLiteral("/ut_custom"), []() { return QStringLiteral("hello_ut"); });
  int st = 0;
  QByteArray body;
  QVERIFY(httpGet(p, QByteArrayLiteral("/ut_custom"), &st, &body));
  QCOMPARE(st, 200);
  QCOMPARE(body, QByteArrayLiteral("hello_ut"));

  srv().unregisterHandler(QStringLiteral("/ut_custom"));
  QVERIFY(httpGet(p, QByteArrayLiteral("/ut_custom"), &st, &body));
  QCOMPARE(st, 404);
  srv().stop();
}

void TestHttpEndpointServer::custom_json_handler() {
  QVERIFY(srv().start(0));
  const quint16 p = static_cast<quint16>(srv().port());
  srv().registerJsonHandler(QStringLiteral("/ut_json"), []() {
    QJsonObject o;
    o[QStringLiteral("k")] = 42;
    return o;
  });
  int st = 0;
  QByteArray body;
  QVERIFY(httpGet(p, QByteArrayLiteral("/ut_json"), &st, &body));
  QCOMPARE(st, 200);
  const QJsonObject o = parseJsonObject(body);
  QCOMPARE(o.value(QStringLiteral("k")).toInt(), 42);
  srv().stop();
}

void TestHttpEndpointServer::not_found() {
  QVERIFY(srv().start(0));
  const quint16 p = static_cast<quint16>(srv().port());
  int st = 0;
  QByteArray body;
  QVERIFY(httpGet(p, QByteArrayLiteral("/no_such_path_xyz"), &st, &body));
  QCOMPARE(st, 404);
  srv().stop();
}

void TestHttpEndpointServer::bad_request_line() {
  QVERIFY(srv().start(0));
  const quint16 p = static_cast<quint16>(srv().port());
  QTcpSocket s;
  s.connectToHost(QHostAddress::LocalHost, p);
  QVERIFY(s.waitForConnected(3000));
  s.write("INVALID\r\n\r\n");
  QVERIFY(s.waitForBytesWritten(3000));

  QByteArray acc;
  QElapsedTimer et;
  et.start();
  while (et.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (s.waitForReadyRead(100))
      acc += s.readAll();
    if (s.state() == QAbstractSocket::UnconnectedState)
      break;
  }
  acc += s.readAll();
  QVERIFY(acc.contains(QByteArrayLiteral("400")));
  srv().stop();
}

void TestHttpEndpointServer::setHealthStatus_degraded() {
  QVERIFY(srv().start(0));
  const quint16 p = static_cast<quint16>(srv().port());
  srv().setHealthStatus(QStringLiteral("video"), false, QStringLiteral("down"));
  int st = 0;
  QByteArray body;
  QVERIFY(httpGet(p, QByteArrayLiteral("/health"), &st, &body));
  QCOMPARE(st, 200);
  const QJsonObject o = parseJsonObject(body);
  QCOMPARE(o.value(QStringLiteral("status")).toString(), QStringLiteral("degraded"));
  QCOMPARE(o.value(QStringLiteral("all_healthy")).toBool(), false);
  srv().setHealthStatus(QStringLiteral("video"), true, QString());
  srv().stop();
}

void TestHttpEndpointServer::requestReceived_signal() {
  QVERIFY(srv().start(0));
  const quint16 p = static_cast<quint16>(srv().port());
  QSignalSpy spy(&srv(), &HttpEndpointServer::requestReceived);
  int st = 0;
  QByteArray body;
  QVERIFY(httpGet(p, QByteArrayLiteral("/health"), &st, &body));
  QCOMPARE(st, 200);
  QVERIFY(spy.count() >= 1);
  srv().stop();
}

QTEST_MAIN(TestHttpEndpointServer)
#include "test_httpendpointserver.moc"
