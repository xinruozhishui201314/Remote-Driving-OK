#include "media/RtcpCompoundParser.h"
#include "media/RtpStreamClockContext.h"

#include <QByteArray>
#include <QRandomGenerator>
#include <QtTest/QtTest>

#include <cstddef>

class TestRtcpCompoundParser : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestRtcpCompoundParser)
 public:
  explicit TestRtcpCompoundParser(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void null_context_or_short_buffer_returns_false();
  void non_rtcp_returns_false();
  void sender_report_updates_clock();
  void wrong_ssrc_increments_reject_metric();
  /** 属性式：随机字节输入下解析不崩溃、不越界（QuickCheck 风格轻量版）。 */
  void property_random_inputs_no_crash_bounded();

 private:
  static QByteArray makeSrPacket(quint32 ssrc, quint32 rtpTs);
};

QByteArray TestRtcpCompoundParser::makeSrPacket(quint32 ssrc, quint32 rtpTs) {
  // RTCP SR: 4 byte header + 24 byte SR body (SSRC + NTP + RTP ts + counts)
  QByteArray p(28, '\0');
  auto *d = reinterpret_cast<unsigned char *>(p.data());
  d[0] = 0x80;  // V=2, P=0, RC=0
  d[1] = 200;   // PT SR
  d[2] = 0;
  d[3] = 6;  // length = 7 words - 1
  d[4] = static_cast<unsigned char>((ssrc >> 24) & 0xff);
  d[5] = static_cast<unsigned char>((ssrc >> 16) & 0xff);
  d[6] = static_cast<unsigned char>((ssrc >> 8) & 0xff);
  d[7] = static_cast<unsigned char>(ssrc & 0xff);
  // NTP MW/LW zeros @ 8..15
  d[16] = static_cast<unsigned char>((rtpTs >> 24) & 0xff);
  d[17] = static_cast<unsigned char>((rtpTs >> 16) & 0xff);
  d[18] = static_cast<unsigned char>((rtpTs >> 8) & 0xff);
  d[19] = static_cast<unsigned char>(rtpTs & 0xff);
  return p;
}

void TestRtcpCompoundParser::null_context_or_short_buffer_returns_false() {
  RtpStreamClockContext ctx;
  quint8 one = 1;
  QVERIFY(!rtcpCompoundTryConsumeAndUpdateClock(&one, 1, nullptr, 0, 0, nullptr));
  QVERIFY(!rtcpCompoundTryConsumeAndUpdateClock(&one, 2, &ctx, 0, 0, nullptr));
}

void TestRtcpCompoundParser::non_rtcp_returns_false() {
  RtpStreamClockContext ctx;
  quint8 buf[8] = {0x80, 199, 0, 1, 0, 0, 0, 0};
  QVERIFY(!rtcpCompoundTryConsumeAndUpdateClock(buf, sizeof buf, &ctx, 0, 0, nullptr));
}

void TestRtcpCompoundParser::sender_report_updates_clock() {
  RtpStreamClockContext ctx;
  ctx.resetMapping();
  const quint32 ssrc = 0xAABBCCDD;
  const quint32 rtpTs = 0x11223344;
  QByteArray pkt = makeSrPacket(ssrc, rtpTs);
  QString log;
  const qint64 wall = 1'700'000'000'000LL;
  QVERIFY(rtcpCompoundTryConsumeAndUpdateClock(reinterpret_cast<const uint8_t *>(pkt.constData()),
                                               static_cast<std::size_t>(pkt.size()), &ctx, wall,
                                               ssrc, &log));
  QCOMPARE(ctx.sr_ssrc.load(), ssrc);
  QCOMPARE(ctx.sr_rtp_ts.load(), rtpTs);
  QCOMPARE(ctx.sr_recv_wall_ms.load(), wall);
  QCOMPARE(ctx.sr_valid.load(), 1);
  QVERIFY(log.contains(QStringLiteral("sr_ok")));
  QCOMPARE(ctx.metrics_sr_accepted.load(), 1ULL);
}

void TestRtcpCompoundParser::wrong_ssrc_increments_reject_metric() {
  RtpStreamClockContext ctx;
  ctx.resetMapping();
  QByteArray pkt = makeSrPacket(0x11111111, 1);
  QString log;
  QVERIFY(rtcpCompoundTryConsumeAndUpdateClock(reinterpret_cast<const uint8_t *>(pkt.constData()),
                                               static_cast<std::size_t>(pkt.size()), &ctx, 100,
                                               0x22222222, &log));
  QCOMPARE(ctx.metrics_sr_rejected_ssrc.load(), 1ULL);
  QCOMPARE(ctx.sr_valid.load(), 0);
}

void TestRtcpCompoundParser::property_random_inputs_no_crash_bounded() {
  RtpStreamClockContext ctx;
  QRandomGenerator *rng = QRandomGenerator::global();
  constexpr int kIterations = 800;
  for (int i = 0; i < kIterations; ++i) {
    const int n = rng->bounded(512);
    QByteArray buf(n, Qt::Uninitialized);
    for (int b = 0; b < n; ++b)
      buf[b] = static_cast<char>(rng->bounded(256));
    QString log;
    (void)rtcpCompoundTryConsumeAndUpdateClock(
        reinterpret_cast<const uint8_t *>(buf.constData()), static_cast<std::size_t>(buf.size()),
        &ctx, rng->generate64() % 10'000'000, static_cast<quint32>(rng->generate()), &log);
  }
}

QTEST_MAIN(TestRtcpCompoundParser)
#include "test_rtcpcompoundparser.moc"
