#include "core/antireplayguard.h"

#include <QtTest/QtTest>

class TestAntiReplayGuard : public QObject {
  Q_OBJECT
 private slots:
  void first_packet_initializes();
  void duplicate_seq_rejected();
  void monotonic_seq_accepted();
  void out_of_window_rejected();
  void in_window_late_packet_accepted_once();
  void timestamp_too_far_future_rejected();
  void timestamp_too_old_rejected();
  void reset_allows_fresh_start();
  void checkTimestampDrift_static_boundaries();
  void lastResetReason_afterConstruction();
  void large_forward_gap_advances_without_duplicate();
  void isOverflowing_true_when_highest_seq_past_three_quarters_threshold();
};

void TestAntiReplayGuard::first_packet_initializes() {
  AntiReplayGuard g;
  QString r;
  const qint64 now = 1'700'000'000'000LL;
  QVERIFY(g.checkAndRecord(100u, now, now, &r));
  QVERIFY2(r.isEmpty(), qPrintable(r));
}

void TestAntiReplayGuard::duplicate_seq_rejected() {
  AntiReplayGuard g;
  const qint64 now = 1'700'000'000'000LL;
  QVERIFY(g.checkAndRecord(1u, now, now));
  QString r;
  QVERIFY(!g.checkAndRecord(1u, now, now, &r));
  QVERIFY(r.contains(QStringLiteral("duplicate")));
}

void TestAntiReplayGuard::monotonic_seq_accepted() {
  AntiReplayGuard g;
  const qint64 now = 1'700'000'000'000LL;
  QVERIFY(g.checkAndRecord(10u, now, now));
  QVERIFY(g.checkAndRecord(11u, now, now));
  QVERIFY(g.checkAndRecord(12u, now, now));
}

void TestAntiReplayGuard::out_of_window_rejected() {
  AntiReplayGuard g;
  const qint64 now = 1'700'000'000'000LL;
  QVERIFY(g.checkAndRecord(10000u, now, now));
  QString r;
  // behind = 2000 >= WINDOW_SIZE(1024) → too old
  QVERIFY(!g.checkAndRecord(8000u, now, now, &r));
  QVERIFY(r.contains(QStringLiteral("too old")) || r.contains(QStringLiteral("old")));
}

void TestAntiReplayGuard::in_window_late_packet_accepted_once() {
  AntiReplayGuard g;
  const qint64 now = 1'700'000'000'000LL;
  QVERIFY(g.checkAndRecord(50u, now, now));
  QVERIFY(g.checkAndRecord(55u, now, now));
  QString r;
  QVERIFY(g.checkAndRecord(52u, now, now, &r));
  QVERIFY(r.isEmpty());
  QVERIFY(!g.checkAndRecord(52u, now, now, &r));
  QVERIFY(r.contains(QStringLiteral("replay")) || r.contains(QStringLiteral("duplicate")));
}

void TestAntiReplayGuard::timestamp_too_far_future_rejected() {
  AntiReplayGuard g;
  QString r;
  const qint64 local = 1'700'000'000'000LL;
  const qint64 msg = local + AntiReplayGuard::MAX_TIMESTAMP_DRIFT_MS + 1000;
  QVERIFY(!g.checkAndRecord(1u, msg, local, &r));
  QVERIFY(!r.isEmpty());
}

void TestAntiReplayGuard::timestamp_too_old_rejected() {
  AntiReplayGuard g;
  QString r;
  const qint64 local = 1'700'000'000'000LL;
  const qint64 msg = local + AntiReplayGuard::MIN_TIMESTAMP_DRIFT_MS - 1;
  QVERIFY(!g.checkAndRecord(1u, msg, local, &r));
  QVERIFY(!r.isEmpty());
}

void TestAntiReplayGuard::reset_allows_fresh_start() {
  AntiReplayGuard g;
  const qint64 now = 1'700'000'000'000LL;
  QVERIFY(g.checkAndRecord(5u, now, now));
  QVERIFY(!g.checkAndRecord(5u, now, now));
  g.reset();
  QVERIFY(g.checkAndRecord(5u, now, now));
}

void TestAntiReplayGuard::checkTimestampDrift_static_boundaries() {
  QString r;
  const qint64 local = 1'000'000LL;
  QVERIFY(AntiReplayGuard::checkTimestampDrift(local, local, &r));
  QVERIFY(AntiReplayGuard::checkTimestampDrift(local + AntiReplayGuard::MAX_TIMESTAMP_DRIFT_MS,
                                               local, &r));
  QVERIFY(!AntiReplayGuard::checkTimestampDrift(local + AntiReplayGuard::MAX_TIMESTAMP_DRIFT_MS + 1,
                                                local, &r));
  QVERIFY(AntiReplayGuard::checkTimestampDrift(local + AntiReplayGuard::MIN_TIMESTAMP_DRIFT_MS,
                                               local, &r));
  QVERIFY(!AntiReplayGuard::checkTimestampDrift(local + AntiReplayGuard::MIN_TIMESTAMP_DRIFT_MS - 1,
                                                local, &r));
}

void TestAntiReplayGuard::lastResetReason_afterConstruction() {
  AntiReplayGuard g;
  QVERIFY(!g.lastResetReason().isEmpty());
}

void TestAntiReplayGuard::large_forward_gap_advances_without_duplicate() {
  AntiReplayGuard g;
  const qint64 now = 1'700'000'000'000LL;
  QVERIFY(g.checkAndRecord(100u, now, now));
  const uint32_t far = 100u + static_cast<uint32_t>(AntiReplayGuard::WINDOW_SIZE) + 500u;
  QVERIFY(g.checkAndRecord(far, now, now));
  QVERIFY(g.checkAndRecord(far + 1u, now, now));
}

void TestAntiReplayGuard::isOverflowing_true_when_highest_seq_past_three_quarters_threshold() {
  AntiReplayGuard g;
  const qint64 now = 1'700'000'000'000LL;
  const uint32_t hi = (AntiReplayGuard::OVERFLOW_THRESHOLD * 3 / 4) + 1u;
  QVERIFY(g.checkAndRecord(hi, now, now));
  QVERIFY(g.isOverflowing());
}

QTEST_MAIN(TestAntiReplayGuard)
#include "test_antireplayguard.moc"
