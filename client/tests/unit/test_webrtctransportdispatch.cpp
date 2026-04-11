#include "utils/WebRtcTransportDispatch.h"

#include <QtTest/QtTest>

class TestWebRtcTransportDispatch : public QObject {
  Q_OBJECT
 private slots:
  void synthesizeWhepUrl_format();
  void videoChannelForCameraId_primaryAndSecondary();
  void sendPayload_control_updates_stats();
  void sendPayload_no_primary_fails();
  void sendPayload_tryPost_rejected_fails();
  void sendPayload_unsupported_channel();
};

void TestWebRtcTransportDispatch::synthesizeWhepUrl_format() {
  const QString u = WebRtcTransportDispatch::synthesizeWhepUrl(
      QStringLiteral("zlm:8443"), QStringLiteral("VIN_A"), QStringLiteral("sess1"));
  QCOMPARE(u, QStringLiteral("http://zlm:8443/whep/VIN_A/sess1"));
}

void TestWebRtcTransportDispatch::videoChannelForCameraId_primaryAndSecondary() {
  QCOMPARE(WebRtcTransportDispatch::videoChannelForCameraId(0), TransportChannel::VIDEO_PRIMARY);
  QCOMPARE(WebRtcTransportDispatch::videoChannelForCameraId(1), TransportChannel::VIDEO_SECONDARY);
  QCOMPARE(WebRtcTransportDispatch::videoChannelForCameraId(99), TransportChannel::VIDEO_SECONDARY);
}

void TestWebRtcTransportDispatch::sendPayload_control_updates_stats() {
  QMap<TransportChannel, ChannelStats> stats;
  const uint8_t buf[] = {0x01, 0x02, 0x03};
  QByteArray captured;
  const auto r = WebRtcTransportDispatch::sendPayload(
      TransportChannel::CONTROL_CRITICAL, true,
      [&captured](const QByteArray& p) {
        captured = p;
        return true;
      },
      buf, sizeof(buf), &stats);
  QVERIFY(r.success);
  QCOMPARE(captured.size(), 3);
  QCOMPARE(stats[TransportChannel::CONTROL_CRITICAL].bytesSent, 3ull);
  QCOMPARE(stats[TransportChannel::CONTROL_CRITICAL].packetsSent, 1ull);
}

void TestWebRtcTransportDispatch::sendPayload_no_primary_fails() {
  const uint8_t buf[] = {0x00};
  const auto r = WebRtcTransportDispatch::sendPayload(
      TransportChannel::SIGNALING, false, [](const QByteArray&) { return false; }, buf, 1, nullptr);
  QVERIFY(!r.success);
  QVERIFY(!r.errorMessage.isEmpty());
}

void TestWebRtcTransportDispatch::sendPayload_tryPost_rejected_fails() {
  const uint8_t buf[] = {0xAB, 0xCD};
  QMap<TransportChannel, ChannelStats> stats;
  const auto r = WebRtcTransportDispatch::sendPayload(
      TransportChannel::CONTROL_CRITICAL, true, [](const QByteArray&) { return false; }, buf,
      sizeof(buf), &stats);
  QVERIFY(!r.success);
  QVERIFY(r.errorMessage.contains(QStringLiteral("Data channel")));
  QCOMPARE(stats[TransportChannel::CONTROL_CRITICAL].bytesSent, 0ull);
}

void TestWebRtcTransportDispatch::sendPayload_unsupported_channel() {
  const uint8_t buf[] = {0x00};
  const auto r = WebRtcTransportDispatch::sendPayload(
      TransportChannel::VIDEO_PRIMARY, true, [](const QByteArray&) { return true; }, buf, 1, nullptr);
  QVERIFY(!r.success);
}

QTEST_MAIN(TestWebRtcTransportDispatch)
#include "test_webrtctransportdispatch.moc"
