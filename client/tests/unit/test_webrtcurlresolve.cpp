#include "utils/WebRtcUrlResolve.h"

#include <QRandomGenerator>
#include <QString>
#include <QtTest/QtTest>

class TestWebRtcUrlResolve : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestWebRtcUrlResolve)
 public:
  explicit TestWebRtcUrlResolve(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void baseUrlFromWhep_empty() { QVERIFY(WebRtcUrlResolve::baseUrlFromWhep({}).isEmpty()); }

  void baseUrlFromWhep_whepScheme_toHttp() {
    const QString u =
        QStringLiteral("whep://media.example:8080/index/api/webrtc?app=live&stream=x");
    QCOMPARE(WebRtcUrlResolve::baseUrlFromWhep(u), QStringLiteral("http://media.example:8080"));
  }

  void baseUrlFromWhep_http_keepsScheme() {
    const QString u = QStringLiteral("https://zlm.internal/webrtc?type=play");
    QCOMPARE(WebRtcUrlResolve::baseUrlFromWhep(u), QStringLiteral("https://zlm.internal"));
  }

  void baseUrlFromWhep_invalid_returnsEmpty() {
    QVERIFY(WebRtcUrlResolve::baseUrlFromWhep(QStringLiteral("not-a-url")).isEmpty());
  }

  void resolveBaseUrl_prefersWhepOverEnv() {
    const QString whep = QStringLiteral("whep://a:1/x");
    QCOMPARE(WebRtcUrlResolve::resolveBaseUrl(whep, QStringLiteral("http://ignored")),
             QStringLiteral("http://a:1"));
  }

  void resolveBaseUrl_emptyWhep_usesEnv() {
    QCOMPARE(WebRtcUrlResolve::resolveBaseUrl({}, QStringLiteral("http://env-only")),
             QStringLiteral("http://env-only"));
  }

  void appFromWhepQuery_usesQueryOrDefault() {
    const QString whep = QStringLiteral("whep://h/p?app=teleop&stream=1");
    QCOMPARE(WebRtcUrlResolve::appFromWhepQuery(whep, QStringLiteral("live")),
             QStringLiteral("teleop"));
    QCOMPARE(WebRtcUrlResolve::appFromWhepQuery({}, QStringLiteral("live")),
             QStringLiteral("live"));
    QCOMPARE(WebRtcUrlResolve::appFromWhepQuery(QStringLiteral("whep://h/p?stream=1"),
                                                QStringLiteral("live")),
             QStringLiteral("live"));
  }

  /** 不变式：任意 QString 输入下 URL 解析不崩溃；有结果时多为 http(s) 或空。 */
  void property_random_strings_no_crash() {
    QRandomGenerator *rng = QRandomGenerator::global();
    constexpr int kIterations = 500;
    const QString alphabet =
        QStringLiteral("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/.?&=%-_");
    for (int i = 0; i < kIterations; ++i) {
      QString s;
      const int len = rng->bounded(120);
      for (int c = 0; c < len; ++c)
        s += alphabet.at(rng->bounded(alphabet.size()));
      const QString base = WebRtcUrlResolve::baseUrlFromWhep(s);
      QVERIFY(base.isEmpty() || base.startsWith(QStringLiteral("http://")) ||
              base.startsWith(QStringLiteral("https://")));
      (void)WebRtcUrlResolve::resolveBaseUrl(s, QStringLiteral("http://fallback"));
      (void)WebRtcUrlResolve::appFromWhepQuery(s, QStringLiteral("live"));
    }
  }
};

QTEST_MAIN(TestWebRtcUrlResolve)
#include "test_webrtcurlresolve.moc"
