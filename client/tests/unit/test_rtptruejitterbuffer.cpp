#include "media/RtpStreamClockContext.h"
#include "media/RtpTrueJitterBuffer.h"

#include <QByteArray>
#include <QDateTime>
#include <QtGlobal>
#include <QtTest/QtTest>

namespace {

static void putRtpHeader(QByteArray &buf, quint16 seq, quint32 rtpTs, quint32 ssrc) {
  buf.resize(12);
  buf[0] = char(0x80);
  buf[1] = char(96);
  buf[2] = char((seq >> 8) & 0xff);
  buf[3] = char(seq & 0xff);
  buf[4] = char((rtpTs >> 24) & 0xff);
  buf[5] = char((rtpTs >> 16) & 0xff);
  buf[6] = char((rtpTs >> 8) & 0xff);
  buf[7] = char(rtpTs & 0xff);
  buf[8] = char((ssrc >> 24) & 0xff);
  buf[9] = char((ssrc >> 16) & 0xff);
  buf[10] = char((ssrc >> 8) & 0xff);
  buf[11] = char(ssrc & 0xff);
}

static RtpIngressPacket makePkt(const QByteArray &rtpHdr) {
  RtpIngressPacket p;
  p.bytes = rtpHdr;
  p.lifecycleId = 1;
  return p;
}

}  // namespace

class TestRtpTrueJitterBuffer : public QObject {
  Q_OBJECT
 private slots:
  void init();
  void mode_off_enqueue_no_buffer();
  void mode_fixed_deadline_and_pop();
  void fifo_mode_pops_by_deadline_order();
  void overflow_clears_and_returns_drop();
  void late_seq_dropped();
  void hole_timeout_requests_keyframe();
  void millisUntilNextDue();
  void clear_resets_state();
  void short_rtp_header_rejected();
  void adaptive_sets_deadline();
  void ntp_mode_uses_sender_report_mapping();
  void hybrid_falls_back_when_sr_stale();
  void logMetricsIfDue_no_crash();

 private:
  void resetEnv();
};

void TestRtpTrueJitterBuffer::resetEnv() {
  qunsetenv("CLIENT_RTP_PLAYOUT_MODE");
  qunsetenv("CLIENT_RTP_PLAYOUT_DELAY_MS");
  qunsetenv("CLIENT_RTP_PLAYOUT_MAX_PACKETS");
  qunsetenv("CLIENT_RTP_PLAYOUT_SEQ_ORDER");
  qunsetenv("CLIENT_RTP_PLAYOUT_HOLE_MS");
  qunsetenv("CLIENT_RTP_SR_STALE_MS");
  qunsetenv("CLIENT_RTP_CLOCK_HZ");
  qunsetenv("CLIENT_RTP_NTP_MARGIN_MS");
  qunsetenv("CLIENT_RTP_JITTER_MIN_MS");
  qunsetenv("CLIENT_RTP_JITTER_MAX_MS");
  qunsetenv("CLIENT_RTP_JITTER_GAIN");
  qunsetenv("CLIENT_RTP_JITTER_METRICS_MS");
}

void TestRtpTrueJitterBuffer::init() { resetEnv(); }

void TestRtpTrueJitterBuffer::mode_off_enqueue_no_buffer() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("off"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();
  QCOMPARE(int(buf.mode()), int(RtpPlayoutMode::Off));
  QVERIFY(!buf.isActive());

  QByteArray hdr;
  putRtpHeader(hdr, 10, 1000, 0x11111111u);
  RtpIngressPacket p = makePkt(hdr);
  QCOMPARE(buf.enqueue(std::move(p), 1'000'000, reinterpret_cast<const uint8_t *>(hdr.constData()),
                       hdr.size()),
           0);
  QVERIFY(buf.empty());

  RtpIngressPacket out;
  QVERIFY(!buf.tryPopDue(1'000'100, out));
}

void TestRtpTrueJitterBuffer::mode_fixed_deadline_and_pop() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("fixed"));
  qputenv("CLIENT_RTP_PLAYOUT_DELAY_MS", QByteArrayLiteral("50"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();
  QCOMPARE(int(buf.mode()), int(RtpPlayoutMode::Fixed));

  QByteArray hdr;
  putRtpHeader(hdr, 100, 90'000, 0x22222222u);
  const qint64 recv = 1'000'000;
  RtpIngressPacket p = makePkt(hdr);
  QCOMPARE(buf.enqueue(std::move(p), recv, reinterpret_cast<const uint8_t *>(hdr.constData()),
                       hdr.size()),
           0);

  RtpIngressPacket out;
  QVERIFY(!buf.tryPopDue(recv + 49, out));
  QVERIFY(buf.tryPopDue(recv + 50, out));
  QCOMPARE(out.bytes.size(), 12);
}

void TestRtpTrueJitterBuffer::fifo_mode_pops_by_deadline_order() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("fixed"));
  qputenv("CLIENT_RTP_PLAYOUT_DELAY_MS", QByteArrayLiteral("10"));
  qputenv("CLIENT_RTP_PLAYOUT_SEQ_ORDER", QByteArrayLiteral("0"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();

  QByteArray h1, h2;
  putRtpHeader(h1, 1, 1, 1);
  putRtpHeader(h2, 2, 2, 1);
  const qint64 t0 = 2'000'000;
  {
    RtpIngressPacket p = makePkt(h1);
    buf.enqueue(std::move(p), t0, reinterpret_cast<const uint8_t *>(h1.constData()), h1.size());
  }
  {
    RtpIngressPacket p = makePkt(h2);
    buf.enqueue(std::move(p), t0 + 5, reinterpret_cast<const uint8_t *>(h2.constData()), h2.size());
  }

  RtpIngressPacket out;
  QVERIFY(!buf.tryPopDue(t0 + 9, out));
  QVERIFY(buf.tryPopDue(t0 + 10, out));
  QCOMPARE(static_cast<unsigned char>(out.bytes[3]), 1u);  // seq low byte of first enqueued
  QVERIFY(buf.tryPopDue(t0 + 15, out));
  QCOMPARE(static_cast<unsigned char>(out.bytes[3]), 2u);
}

void TestRtpTrueJitterBuffer::overflow_clears_and_returns_drop() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("fixed"));
  qputenv("CLIENT_RTP_PLAYOUT_DELAY_MS", QByteArrayLiteral("5"));
  qputenv("CLIENT_RTP_PLAYOUT_MAX_PACKETS", QByteArrayLiteral("64"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();

  for (int i = 0; i < 64; ++i) {
    QByteArray hdr;
    putRtpHeader(hdr, quint16(i), quint32(i * 1000), 0x33333333u);
    RtpIngressPacket p = makePkt(hdr);
    QCOMPARE(buf.enqueue(std::move(p), 3'000'000,
                         reinterpret_cast<const uint8_t *>(hdr.constData()), hdr.size()),
             0);
  }
  QByteArray hdr65;
  putRtpHeader(hdr65, 99, 99, 0x33333333u);
  RtpIngressPacket p65 = makePkt(hdr65);
  QCOMPARE(buf.enqueue(std::move(p65), 3'000'000,
                       reinterpret_cast<const uint8_t *>(hdr65.constData()), hdr65.size()),
           1);
  QVERIFY(buf.empty());
}

void TestRtpTrueJitterBuffer::late_seq_dropped() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("fixed"));
  qputenv("CLIENT_RTP_PLAYOUT_DELAY_MS", QByteArrayLiteral("5"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();

  QByteArray h100, h101, h100late;
  putRtpHeader(h100, 100, 1, 1);
  putRtpHeader(h101, 101, 2, 1);
  putRtpHeader(h100late, 100, 3, 1);

  {
    RtpIngressPacket p = makePkt(h100);
    QCOMPARE(buf.enqueue(std::move(p), 4'000'000,
                         reinterpret_cast<const uint8_t *>(h100.constData()), h100.size()),
             0);
  }
  RtpIngressPacket out;
  QVERIFY(buf.tryPopDue(4'000'010, out));

  {
    RtpIngressPacket p = makePkt(h101);
    QCOMPARE(buf.enqueue(std::move(p), 4'000'020,
                         reinterpret_cast<const uint8_t *>(h101.constData()), h101.size()),
             0);
  }
  {
    RtpIngressPacket p = makePkt(h100late);
    QCOMPARE(buf.enqueue(std::move(p), 4'000'030,
                         reinterpret_cast<const uint8_t *>(h100late.constData()), h100late.size()),
             1);
  }
}

void TestRtpTrueJitterBuffer::hole_timeout_requests_keyframe() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("fixed"));
  qputenv("CLIENT_RTP_PLAYOUT_DELAY_MS", QByteArrayLiteral("1"));
  qputenv("CLIENT_RTP_PLAYOUT_HOLE_MS", QByteArrayLiteral("30"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();

  QByteArray h100, h102;
  putRtpHeader(h100, 100, 1, 1);
  putRtpHeader(h102, 102, 2, 1);
  {
    RtpIngressPacket p = makePkt(h100);
    buf.enqueue(std::move(p), 5'000'000, reinterpret_cast<const uint8_t *>(h100.constData()),
                h100.size());
  }
  RtpIngressPacket out;
  QVERIFY(buf.tryPopDue(5'000'010, out));

  {
    RtpIngressPacket p = makePkt(h102);
    QCOMPARE(buf.enqueue(std::move(p), 5'000'020,
                         reinterpret_cast<const uint8_t *>(h102.constData()), h102.size()),
             0);
  }

  QVERIFY(!buf.consumeHoleKeyframeRequest());
  RtpIngressPacket ignored;
  QVERIFY(!buf.tryPopDue(5'000'035, ignored));
  QVERIFY(!buf.tryPopDue(5'000'070, ignored));
  QVERIFY(buf.consumeHoleKeyframeRequest());
  QVERIFY(!buf.consumeHoleKeyframeRequest());
  QVERIFY(buf.empty());
}

void TestRtpTrueJitterBuffer::millisUntilNextDue() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("fixed"));
  qputenv("CLIENT_RTP_PLAYOUT_DELAY_MS", QByteArrayLiteral("100"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();

  QByteArray hdr;
  putRtpHeader(hdr, 200, 1, 1);
  RtpIngressPacket p = makePkt(hdr);
  const qint64 recv = 6'000'000;
  buf.enqueue(std::move(p), recv, reinterpret_cast<const uint8_t *>(hdr.constData()), hdr.size());

  int w = buf.millisUntilNextDue(recv);
  QVERIFY(w > 0 && w <= 100);
  QCOMPARE(buf.millisUntilNextDue(recv + 100), 0);
}

void TestRtpTrueJitterBuffer::clear_resets_state() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("fixed"));
  qputenv("CLIENT_RTP_PLAYOUT_DELAY_MS", QByteArrayLiteral("50"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();

  QByteArray hdr;
  putRtpHeader(hdr, 300, 1, 1);
  RtpIngressPacket p = makePkt(hdr);
  buf.enqueue(std::move(p), 7'000'000, reinterpret_cast<const uint8_t *>(hdr.constData()),
              hdr.size());
  buf.clear();
  QVERIFY(buf.empty());
  RtpIngressPacket out;
  QVERIFY(!buf.tryPopDue(7'000'200, out));
}

void TestRtpTrueJitterBuffer::short_rtp_header_rejected() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("fixed"));
  qputenv("CLIENT_RTP_PLAYOUT_DELAY_MS", QByteArrayLiteral("10"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();

  QByteArray shortHdr(8, 0);
  RtpIngressPacket p = makePkt(shortHdr);
  QCOMPARE(buf.enqueue(std::move(p), 8'000'000,
                       reinterpret_cast<const uint8_t *>(shortHdr.constData()), shortHdr.size()),
           0);
}

void TestRtpTrueJitterBuffer::adaptive_sets_deadline() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("adaptive"));
  qputenv("CLIENT_RTP_PLAYOUT_DELAY_MS", QByteArrayLiteral("40"));
  qputenv("CLIENT_RTP_JITTER_MIN_MS", QByteArrayLiteral("20"));
  qputenv("CLIENT_RTP_JITTER_MAX_MS", QByteArrayLiteral("180"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();
  QCOMPARE(int(buf.mode()), int(RtpPlayoutMode::Adaptive));

  const qint64 base = QDateTime::currentMSecsSinceEpoch();
  QByteArray h1, h2;
  putRtpHeader(h1, 400, 90'000, 1);
  putRtpHeader(h2, 401, 90'090, 1);  // 1ms @ 90kHz
  buf.enqueue(makePkt(h1), base, reinterpret_cast<const uint8_t *>(h1.constData()), h1.size());
  buf.enqueue(makePkt(h2), base + 50, reinterpret_cast<const uint8_t *>(h2.constData()), h2.size());

  RtpIngressPacket out;
  QVERIFY(!buf.tryPopDue(base, out));
  QVERIFY(buf.tryPopDue(base + 300, out));
}

void TestRtpTrueJitterBuffer::ntp_mode_uses_sender_report_mapping() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("ntp"));
  qputenv("CLIENT_RTP_PLAYOUT_DELAY_MS", QByteArrayLiteral("40"));
  qputenv("CLIENT_RTP_SR_STALE_MS", QByteArrayLiteral("2000"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();

  const qint64 base = QDateTime::currentMSecsSinceEpoch();
  RtpStreamClockContext ctx;
  const quint32 ssrc = 0xABCDEF01u;
  ctx.updateFromSenderReport(ssrc, 300'000, base - 50);
  buf.setClockContext(&ctx);

  QByteArray hdr;
  // +9000 ticks ≈ 100ms @ 90kHz → play wall ≈ (base-50)+100ms = base+50
  putRtpHeader(hdr, 500, 309'000, ssrc);
  const qint64 recv = base + 50;
  buf.enqueue(makePkt(hdr), recv, reinterpret_cast<const uint8_t *>(hdr.constData()), hdr.size());

  RtpIngressPacket out;
  QVERIFY(!buf.tryPopDue(recv - 1, out));
  QVERIFY(buf.tryPopDue(recv, out));
}

void TestRtpTrueJitterBuffer::hybrid_falls_back_when_sr_stale() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("hybrid"));
  qputenv("CLIENT_RTP_PLAYOUT_DELAY_MS", QByteArrayLiteral("40"));
  qputenv("CLIENT_RTP_JITTER_MIN_MS", QByteArrayLiteral("20"));
  qputenv("CLIENT_RTP_JITTER_MAX_MS", QByteArrayLiteral("180"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();
  QCOMPARE(int(buf.mode()), int(RtpPlayoutMode::Hybrid));
  buf.setClockContext(nullptr);

  const qint64 base = QDateTime::currentMSecsSinceEpoch();
  QByteArray h1, h2;
  putRtpHeader(h1, 400, 90'000, 1);
  putRtpHeader(h2, 401, 90'090, 1);
  QCOMPARE(
      buf.enqueue(makePkt(h1), base, reinterpret_cast<const uint8_t *>(h1.constData()), h1.size()),
      0);
  QCOMPARE(buf.enqueue(makePkt(h2), base + 50, reinterpret_cast<const uint8_t *>(h2.constData()),
                       h2.size()),
           0);

  RtpIngressPacket out;
  QVERIFY(!buf.tryPopDue(base + 5, out));
  QVERIFY(buf.tryPopDue(base + 300, out));
}

void TestRtpTrueJitterBuffer::logMetricsIfDue_no_crash() {
  resetEnv();
  qputenv("CLIENT_RTP_PLAYOUT_MODE", QByteArrayLiteral("fixed"));
  qputenv("CLIENT_RTP_PLAYOUT_DELAY_MS", QByteArrayLiteral("10"));
  qputenv("CLIENT_RTP_JITTER_METRICS_MS", QByteArrayLiteral("5"));
  RtpTrueJitterBuffer buf;
  buf.reloadEnv();

  QByteArray hdr;
  putRtpHeader(hdr, 700, 1, 1);
  buf.enqueue(makePkt(hdr), 12'000'000, reinterpret_cast<const uint8_t *>(hdr.constData()),
              hdr.size());
  buf.logMetricsIfDue(12'000'000, QStringLiteral("ut"));
  buf.logMetricsIfDue(12'000'100, QStringLiteral("ut"));
}

QTEST_MAIN(TestRtpTrueJitterBuffer)
#include "test_rtptruejitterbuffer.moc"
