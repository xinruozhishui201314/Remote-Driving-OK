#include "media/ClientMediaBudget.h"
#include "media/RtpPacketSpscQueue.h"

#include <QByteArray>
#include <QtTest/QtTest>

class TestRtpPacketSpscQueue : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestRtpPacketSpscQueue)
 public:
  explicit TestRtpPacketSpscQueue(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void initTestCase();
  void cleanup();
  void push_pop_roundtrip_releases_budget();
  void push_fails_when_budget_exhausted();
  void discardAll_empties();

 private:
  void resetBudget() {
    qputenv("CLIENT_MEDIA_RTP_RING_GLOBAL_BYTES", QByteArrayLiteral("200000"));
    qputenv("CLIENT_MEDIA_RTP_RING_PER_SLOT_BYTES", QByteArrayLiteral("50000"));
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
};

void TestRtpPacketSpscQueue::initTestCase() { resetBudget(); }

void TestRtpPacketSpscQueue::cleanup() { resetBudget(); }

void TestRtpPacketSpscQueue::push_pop_roundtrip_releases_budget() {
  resetBudget();
  ClientMediaBudget::instance().setSlotEnabled(0, true);
  RtpPacketSpscQueue q(QStringLiteral("ut-front"), 0);
  RtpIngressPacket p;
  p.bytes = QByteArray(100, 'x');
  QVERIFY(q.tryPush(std::move(p)));
  QCOMPARE(q.packetCount(), 1u);
  QCOMPARE(ClientMediaBudget::instance().totalBytes(), 100LL);
  RtpIngressPacket out;
  QVERIFY(q.pop(out));
  QCOMPARE(out.bytes.size(), 100);
  QCOMPARE(ClientMediaBudget::instance().totalBytes(), 0LL);
  QVERIFY(q.empty());
}

void TestRtpPacketSpscQueue::push_fails_when_budget_exhausted() {
  resetBudget();
  ClientMediaBudget::instance().setSlotEnabled(1, true);
  RtpPacketSpscQueue q(QStringLiteral("ut-slot1"), 1);
  RtpIngressPacket p;
  p.bytes = QByteArray(60000, 'y');  // > per-slot 50000 from env
  QVERIFY(!q.tryPush(std::move(p)));
  QVERIFY(q.empty());
}

void TestRtpPacketSpscQueue::discardAll_empties() {
  resetBudget();
  ClientMediaBudget::instance().setSlotEnabled(2, true);
  RtpPacketSpscQueue q(QStringLiteral("ut-discard"), 2);
  for (int i = 0; i < 3; ++i) {
    RtpIngressPacket p;
    p.bytes = QByteArray(50, char('0' + i));
    QVERIFY(q.tryPush(std::move(p)));
  }
  q.discardAll();
  QVERIFY(q.empty());
  QCOMPARE(ClientMediaBudget::instance().totalBytes(), 0LL);
}

QTEST_MAIN(TestRtpPacketSpscQueue)
#include "test_rtppacketspscqueue.moc"
