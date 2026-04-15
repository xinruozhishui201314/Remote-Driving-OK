#include "core/metricscollector.h"
#include "infrastructure/network/ratelimiter.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QtTest/QtTest>

class TestRateLimiter : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestRateLimiter)
 public:
  explicit TestRateLimiter(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void tryAcquire_respects_burst();
  void tryAcquire_emits_exceeded_and_rejectedCount();
  void refill_restores_tokens_and_released_signal();
  void acquire_zero_same_as_tryAcquire();
  void acquire_waits_until_refill();
  void acquire_times_out();
  void setRate_rejects_non_positive();
  void setBurst_rejects_non_positive();
  void setBurst_caps_tokens();
  void resetStats_clears_rejected();

 private:
  void waitForRefillMs(int ms) { QTest::qWait(ms); }
};

void TestRateLimiter::tryAcquire_respects_burst() {
  RateLimiter lim(100.0, 3, nullptr);
  QVERIFY(lim.tryAcquire());
  QVERIFY(lim.tryAcquire());
  QVERIFY(lim.tryAcquire());
  QVERIFY(!lim.tryAcquire());
}

void TestRateLimiter::tryAcquire_emits_exceeded_and_rejectedCount() {
  RateLimiter lim(50.0, 1, nullptr);
  QSignalSpy spy(&lim, &RateLimiter::rateLimitExceeded);
  QVERIFY(lim.tryAcquire());
  QVERIFY(!lim.tryAcquire());
  QCOMPARE(spy.count(), 1);
  QCOMPARE(lim.rejectedCount(), 1LL);
}

void TestRateLimiter::refill_restores_tokens_and_released_signal() {
  RateLimiter lim(100.0, 1, nullptr);
  QSignalSpy released(&lim, &RateLimiter::rateLimitReleased);
  QVERIFY(lim.tryAcquire());
  QVERIFY(!lim.tryAcquire());
  waitForRefillMs(250);
  QVERIFY(lim.tryAcquire());
  QVERIFY(released.count() >= 1);
}

void TestRateLimiter::acquire_zero_same_as_tryAcquire() {
  RateLimiter lim(200.0, 1, nullptr);
  QVERIFY(lim.acquire(0));
  QVERIFY(!lim.acquire(0));
}

void TestRateLimiter::acquire_waits_until_refill() {
  RateLimiter lim(200.0, 1, nullptr);
  QVERIFY(lim.tryAcquire());
  QVERIFY(lim.acquire(800));
}

void TestRateLimiter::acquire_times_out() {
  RateLimiter lim(0.1, 1, nullptr);
  QVERIFY(lim.tryAcquire());
  const qint64 before = lim.rejectedCount();
  QVERIFY(!lim.acquire(50));
  QVERIFY(lim.rejectedCount() > before);
}

void TestRateLimiter::setRate_rejects_non_positive() {
  RateLimiter lim(80.0, 5, nullptr);
  const double r0 = lim.rate();
  lim.setRate(0.0);
  lim.setRate(-1.0);
  QCOMPARE(lim.rate(), r0);
  lim.setRate(120.0);
  QCOMPARE(lim.rate(), 120.0);
}

void TestRateLimiter::setBurst_rejects_non_positive() {
  RateLimiter lim(50.0, 4, nullptr);
  const int b0 = lim.burst();
  lim.setBurst(0);
  lim.setBurst(-3);
  QCOMPARE(lim.burst(), b0);
  lim.setBurst(6);
  QCOMPARE(lim.burst(), 6);
}

void TestRateLimiter::setBurst_caps_tokens() {
  RateLimiter lim(1000.0, 10, nullptr);
  waitForRefillMs(50);
  QVERIFY(lim.availableTokens() > 5);
  lim.setBurst(2);
  QCOMPARE(lim.availableTokens(), 2.0);
}

void TestRateLimiter::resetStats_clears_rejected() {
  RateLimiter lim(30.0, 1, nullptr);
  QVERIFY(lim.tryAcquire());
  QVERIFY(!lim.tryAcquire());
  QVERIFY(lim.rejectedCount() >= 1);
  lim.resetStats();
  QCOMPARE(lim.rejectedCount(), 0LL);
}

QTEST_MAIN(TestRateLimiter)
#include "test_ratelimiter.moc"
