#include "WebRtcTransportDispatch.h"

using namespace WebRtcTransportDispatch;

SendResult WebRtcTransportDispatch::sendPayload(
    TransportChannel channel, bool hasPrimaryClient,
    const std::function<bool(const QByteArray&)>& tryPostToDataChannel, const uint8_t* data,
    size_t len, QMap<TransportChannel, ChannelStats>* stats) {
  if (channel == TransportChannel::CONTROL_CRITICAL || channel == TransportChannel::SIGNALING) {
    if (hasPrimaryClient) {
      const QByteArray payload(reinterpret_cast<const char*>(data), static_cast<int>(len));
      if (!tryPostToDataChannel(payload)) {
        return SendResult{false, QStringLiteral("Data channel not ready or send rejected")};
      }
      if (stats) {
        (*stats)[channel].bytesSent += len;
        (*stats)[channel].packetsSent++;
      }
      return SendResult{true, {}};
    }
    return SendResult{false, QStringLiteral("Primary WebRTC client not available")};
  }
  return SendResult{false, QStringLiteral("Channel not supported by WebRTCChannel")};
}

QString WebRtcTransportDispatch::synthesizeWhepUrl(const QString& host, const QString& vin,
                                                   const QString& sessionId) {
  return QStringLiteral("http://%1/whep/%2/%3").arg(host, vin, sessionId);
}

TransportChannel WebRtcTransportDispatch::videoChannelForCameraId(quint32 cameraId) {
  return (cameraId == 0) ? TransportChannel::VIDEO_PRIMARY : TransportChannel::VIDEO_SECONDARY;
}
