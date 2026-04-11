#include "h264decoder.h"
#include "media/ClientVideoStreamHealth.h"

#include <QByteArray>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <cstring>

namespace {

QByteArray makeRtp(quint16 seq, quint32 ts, quint32 ssrc, uint8_t byte0, uint8_t byte1,
                   const QByteArray &payload = {}) {
  QByteArray p(12 + payload.size(), Qt::Uninitialized);
  p[0] = static_cast<char>(byte0);
  p[1] = static_cast<char>(byte1);
  p[2] = static_cast<char>((seq >> 8) & 0xff);
  p[3] = static_cast<char>(seq & 0xff);
  p[4] = static_cast<char>((ts >> 24) & 0xff);
  p[5] = static_cast<char>((ts >> 16) & 0xff);
  p[6] = static_cast<char>((ts >> 8) & 0xff);
  p[7] = static_cast<char>(ts & 0xff);
  p[8] = static_cast<char>((ssrc >> 24) & 0xff);
  p[9] = static_cast<char>((ssrc >> 16) & 0xff);
  p[10] = static_cast<char>((ssrc >> 8) & 0xff);
  p[11] = static_cast<char>(ssrc & 0xff);
  if (!payload.isEmpty())
    memcpy(p.data() + 12, payload.constData(), static_cast<size_t>(payload.size()));
  return p;
}

/** V=2, PT=96, minimal 12-byte header */
QByteArray makeH264Rtp(quint16 seq, quint32 ts, quint32 ssrc, const QByteArray &payload = {}) {
  return makeRtp(seq, ts, ssrc, 0x80, 0x60, payload);
}

}  // namespace

class TestH264Decoder : public QObject {
  Q_OBJECT
 private slots:
  void init();
  void cleanup();

  void takeAndReset_emit_diag_starts_zero();
  void feedRtp_updates_lifecycle_even_on_short_packet();
  void feedRtp_rejects_too_short();
  void feedRtp_rejects_bad_version();
  void feedRtp_rejects_wrong_payload_type();
  void reset_clears_decoder_state();
  void drainIngress_without_queue_emits_finished();
  void rtp_with_extension_header_no_crash();
  void video_stream_health_dma_effective_respects_software_gl();
  void video_stream_health_force_single_thread_under_software_gl();

 private:
  QByteArray m_savedPt;
};

void TestH264Decoder::init() { m_savedPt = qgetenv("CLIENT_H264_RTP_PAYLOAD_TYPE"); }

void TestH264Decoder::cleanup() {
  if (m_savedPt.isNull())
    qunsetenv("CLIENT_H264_RTP_PAYLOAD_TYPE");
  else
    qputenv("CLIENT_H264_RTP_PAYLOAD_TYPE", m_savedPt);
}

void TestH264Decoder::takeAndReset_emit_diag_starts_zero() {
  H264Decoder dec(QStringLiteral("unit"));
  QCOMPARE(dec.takeAndResetFrameReadyEmitDiagCount(), 0);
}

void TestH264Decoder::feedRtp_updates_lifecycle_even_on_short_packet() {
  H264Decoder dec(QStringLiteral("unit"));
  const quint8 buf[8] = {0};
  dec.feedRtp(buf, sizeof buf, 12345ULL);
  QCOMPARE(dec.currentLifecycleId(), 12345ULL);
}

void TestH264Decoder::feedRtp_rejects_too_short() {
  H264Decoder dec(QStringLiteral("unit"));
  const quint8 buf[11] = {0};
  dec.feedRtp(buf, sizeof buf, 0);
  QCOMPARE(dec.receiverCountFrameReady(), 0);
}

void TestH264Decoder::feedRtp_rejects_bad_version() {
  H264Decoder dec(QStringLiteral("unit"));
  QByteArray p = makeH264Rtp(1, 1000, 0x11223344);
  p[0] = static_cast<char>(0x00);
  dec.feedRtp(reinterpret_cast<const uint8_t *>(p.constData()), static_cast<size_t>(p.size()), 0);
  QCOMPARE(dec.receiverCountFrameReady(), 0);
}

void TestH264Decoder::feedRtp_rejects_wrong_payload_type() {
  H264Decoder dec(QStringLiteral("unit"));
  QByteArray p = makeRtp(1, 1000, 0xAABBCCDD, 0x80, 0xC8);
  dec.feedRtp(reinterpret_cast<const uint8_t *>(p.constData()), static_cast<size_t>(p.size()), 0);
  QCOMPARE(dec.receiverCountFrameReady(), 0);
}

void TestH264Decoder::reset_clears_decoder_state() {
  H264Decoder dec(QStringLiteral("unit"));
  dec.reset();
  QCOMPARE(dec.takeAndResetFrameReadyEmitDiagCount(), 0);
  QCOMPARE(dec.receiverCountFrameReady(), 0);
}

void TestH264Decoder::drainIngress_without_queue_emits_finished() {
  H264Decoder dec(QStringLiteral("unit"));
  QSignalSpy spy(&dec, &H264Decoder::ingressDrainFinished);
  dec.drainRtpIngressQueue();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toBool(), false);
}

void TestH264Decoder::rtp_with_extension_header_no_crash() {
  H264Decoder dec(QStringLiteral("unit-ext"));
  QByteArray p(18, '\0');
  p[0] = static_cast<char>(0x90);
  p[1] = static_cast<char>(0x60);
  const quint16 seq = 3;
  const quint32 ts = 4000, ssrc = 0x99AABBCC;
  p[2] = static_cast<char>((seq >> 8) & 0xff);
  p[3] = static_cast<char>(seq & 0xff);
  p[4] = static_cast<char>((ts >> 24) & 0xff);
  p[5] = static_cast<char>((ts >> 16) & 0xff);
  p[6] = static_cast<char>((ts >> 8) & 0xff);
  p[7] = static_cast<char>(ts & 0xff);
  p[8] = static_cast<char>((ssrc >> 24) & 0xff);
  p[9] = static_cast<char>((ssrc >> 16) & 0xff);
  p[10] = static_cast<char>((ssrc >> 8) & 0xff);
  p[11] = static_cast<char>(ssrc & 0xff);
  p[12] = 0;
  p[13] = 0;
  p[14] = 0;
  p[15] = 0;
  p[16] = static_cast<char>(0x09);
  p[17] = static_cast<char>(0xF0);
  dec.feedRtp(reinterpret_cast<const uint8_t *>(p.constData()), static_cast<size_t>(p.size()), 0);
}

namespace {

void restoreEnvVar(const char *key, const QByteArray &saved) {
  if (saved.isNull())
    qunsetenv(key);
  else
    qputenv(key, saved);
}

}  // namespace

void TestH264Decoder::video_stream_health_dma_effective_respects_software_gl() {
  const QByteArray lib = qgetenv("LIBGL_ALWAYS_SOFTWARE");
  const QByteArray allow = qgetenv("CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL");
  const QByteArray exp = qgetenv("CLIENT_WEBRTC_HW_EXPORT_DMABUF");
  qputenv("LIBGL_ALWAYS_SOFTWARE", "1");
  qputenv("CLIENT_WEBRTC_HW_EXPORT_DMABUF", "1");
  qunsetenv("CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL");
  ClientVideoStreamHealth::debugResetPolicyCacheForTest();
  QVERIFY(!ClientVideoStreamHealth::effectivePreferDmaBufExport());
  qputenv("CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL", "1");
  QVERIFY(ClientVideoStreamHealth::effectivePreferDmaBufExport());
  restoreEnvVar("LIBGL_ALWAYS_SOFTWARE", lib);
  restoreEnvVar("CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL", allow);
  restoreEnvVar("CLIENT_WEBRTC_HW_EXPORT_DMABUF", exp);
  ClientVideoStreamHealth::debugResetPolicyCacheForTest();
}

void TestH264Decoder::video_stream_health_force_single_thread_under_software_gl() {
  const QByteArray lib = qgetenv("LIBGL_ALWAYS_SOFTWARE");
  const QByteArray mult = qgetenv("CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL");
  qputenv("LIBGL_ALWAYS_SOFTWARE", "1");
  qunsetenv("CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL");
  QVERIFY(ClientVideoStreamHealth::shouldForceSingleThreadDecodeUnderSoftwareRaster(4));
  QVERIFY(!ClientVideoStreamHealth::shouldForceSingleThreadDecodeUnderSoftwareRaster(1));
  qputenv("CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL", "1");
  QVERIFY(!ClientVideoStreamHealth::shouldForceSingleThreadDecodeUnderSoftwareRaster(4));
  restoreEnvVar("LIBGL_ALWAYS_SOFTWARE", lib);
  restoreEnvVar("CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL", mult);
  ClientVideoStreamHealth::debugResetPolicyCacheForTest();
}

QTEST_MAIN(TestH264Decoder)
#include "test_h264decoder.moc"
