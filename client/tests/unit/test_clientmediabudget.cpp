#include "media/ClientMediaBudget.h"

#include <QByteArray>
#include <QtTest/QtTest>

class TestClientMediaBudget : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void cleanup();
  void disabled_slot_no_ops();
  void tryAcquire_release_balances();
  void global_cap_blocks();
  void invalid_slot_ignored();
  void zero_byte_noop_acquire();

 private:
  void resetEnvAndInstance();
};

void TestClientMediaBudget::initTestCase() {
  // 与 ClientMediaBudget 构造函数一致：仅当 env 大于内置阈值时才覆盖默认 12MiB/4MiB
  qputenv("CLIENT_MEDIA_RTP_RING_GLOBAL_BYTES", QByteArrayLiteral("80000"));
  qputenv("CLIENT_MEDIA_RTP_RING_PER_SLOT_BYTES", QByteArrayLiteral("50000"));
}

void TestClientMediaBudget::resetEnvAndInstance() {
  ClientMediaBudget &b = ClientMediaBudget::instance();
  for (int i = 0; i < ClientMediaBudget::kMaxSlots; ++i) {
    const qint64 n = b.slotBytes(i);
    if (n > 0) {
      b.setSlotEnabled(i, true);
      b.release(i, n);
    }
    b.setSlotEnabled(i, false);
  }
}

void TestClientMediaBudget::cleanup() { resetEnvAndInstance(); }

void TestClientMediaBudget::disabled_slot_no_ops() {
  resetEnvAndInstance();
  ClientMediaBudget &b = ClientMediaBudget::instance();
  QVERIFY(b.tryAcquire(0, 1000));
  QCOMPARE(b.totalBytes(), 0LL);
  b.release(0, 1000);
  QCOMPARE(b.totalBytes(), 0LL);
}

void TestClientMediaBudget::tryAcquire_release_balances() {
  resetEnvAndInstance();
  ClientMediaBudget &b = ClientMediaBudget::instance();
  b.setSlotEnabled(0, true);
  QVERIFY(b.tryAcquire(0, 5000));
  QCOMPARE(b.totalBytes(), 5000LL);
  QCOMPARE(b.slotBytes(0), 5000LL);
  b.release(0, 5000);
  QCOMPARE(b.totalBytes(), 0LL);
  QCOMPARE(b.slotBytes(0), 0LL);
}

void TestClientMediaBudget::global_cap_blocks() {
  resetEnvAndInstance();
  ClientMediaBudget &b = ClientMediaBudget::instance();
  b.setSlotEnabled(0, true);
  b.setSlotEnabled(1, true);
  QVERIFY(b.tryAcquire(0, 40000));
  const bool secondOk = b.tryAcquire(1, 40001);  // 80001 > 80000（见 initTestCase）
  b.release(0, 40000);
  if (secondOk)
    b.release(1, 40001);
  QVERIFY(!secondOk);
}

void TestClientMediaBudget::invalid_slot_ignored() {
  resetEnvAndInstance();
  ClientMediaBudget &b = ClientMediaBudget::instance();
  QVERIFY(b.tryAcquire(-1, 100000));
  QVERIFY(b.tryAcquire(99, 100000));
}

void TestClientMediaBudget::zero_byte_noop_acquire() {
  resetEnvAndInstance();
  ClientMediaBudget &b = ClientMediaBudget::instance();
  b.setSlotEnabled(2, true);
  QVERIFY(b.tryAcquire(2, 0));
  QCOMPARE(b.totalBytes(), 0LL);
}

QTEST_MAIN(TestClientMediaBudget)
#include "test_clientmediabudget.moc"
