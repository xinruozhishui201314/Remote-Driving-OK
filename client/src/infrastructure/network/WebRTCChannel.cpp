#include "WebRTCChannel.h"

#include "../../utils/WebRtcTransportDispatch.h"
#include "../../webrtcclient.h"
#include "../../webrtcstreammanager.h"

#include <QDebug>
#include <QProcessEnvironment>

WebRTCChannel::WebRTCChannel(QObject* parent) : ITransportManager(parent) {}

WebRTCChannel::~WebRTCChannel() { shutdown(); }

bool WebRTCChannel::initialize(const TransportConfig& config) {
  m_config = config;
  m_streamManager = std::make_unique<WebRtcStreamManager>();
  m_primaryClient = std::make_unique<WebRtcClient>();

  connect(m_primaryClient.get(), &WebRtcClient::connectionStatusChanged, this,
          &WebRTCChannel::onPrimaryConnectionChanged);

  qInfo() << "[Client][WebRTCChannel] initialized";
  return true;
}

void WebRTCChannel::shutdown() {
  if (m_streamManager) {
    m_streamManager->disconnectAll();
  }
  m_state = ConnectionState::DISCONNECTED;
  qInfo() << "[Client][WebRTCChannel] shutdown";
}

void WebRTCChannel::connectAsync(const EndpointInfo& endpoint) {
  m_endpoint = endpoint;
  m_state = ConnectionState::CONNECTING;
  emit connectionStateChanged(m_state);

  qInfo() << "[Client][WebRTCChannel] connecting to" << endpoint.host << "vin=" << endpoint.vin;

  // 连接四路摄像头流（whepUrl 由 host 构造；先设置 VIN 以构造 VIN-prefixed 流名）
  if (m_streamManager) {
    const QString zlm =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("ZLM_VIDEO_URL"));
    if (!endpoint.vin.isEmpty())
      m_streamManager->setCurrentVin(endpoint.vin);
    const QString whepUrl =
        WebRtcTransportDispatch::synthesizeWhepUrl(endpoint.host, endpoint.vin, endpoint.sessionId);
    qInfo().noquote() << QStringLiteral("[Client][StreamE2E][WebRTCChannel] connectAsync host=")
                      << endpoint.host << "vin=" << endpoint.vin
                      << "sessionId=" << endpoint.sessionId << "synthWhepLen=" << whepUrl.size()
                      << "ZLM_VIDEO_URL_set=" << (!zlm.isEmpty() ? 1 : 0)
                      << "★ 非主 UI 路径；即将 connectFourStreams(synthWhep)";
    m_streamManager->connectFourStreams(whepUrl);
  }
}

void WebRTCChannel::disconnect() {
  if (m_streamManager) {
    m_streamManager->disconnectAll();
  }
  m_state = ConnectionState::DISCONNECTED;
  emit connectionStateChanged(m_state);
  emit disconnected();
  qInfo() << "[Client][WebRTCChannel] disconnected";
}

SendResult WebRTCChannel::send(TransportChannel channel, const uint8_t* data, size_t len,
                               SendFlags /*flags*/) {
  return WebRtcTransportDispatch::sendPayload(
      channel, static_cast<bool>(m_primaryClient.get()),
      [this](const QByteArray& payload) {
        return m_primaryClient && m_primaryClient->trySendDataChannelMessage(payload);
      },
      data, len, &m_stats);
}

void WebRTCChannel::registerReceiver(
    TransportChannel channel,
    std::function<void(const uint8_t*, size_t, const PacketMetadata&)> callback) {
  m_receivers[channel] = std::move(callback);
}

NetworkQuality WebRTCChannel::getNetworkQuality() const { return m_quality; }

ChannelStats WebRTCChannel::getChannelStats(TransportChannel ch) const {
  return m_stats.value(ch, ChannelStats{ch});
}

ConnectionState WebRTCChannel::connectionState() const { return m_state; }

void WebRTCChannel::onPrimaryConnectionChanged(bool connected) {
  m_state = connected ? ConnectionState::CONNECTED : ConnectionState::DISCONNECTED;
  emit connectionStateChanged(m_state);
  if (connected) {
    emit this->connected();
    qInfo() << "[Client][WebRTCChannel] primary stream connected";
  } else {
    emit disconnected();
    qWarning() << "[Client][WebRTCChannel] primary stream disconnected";
  }
}

void WebRTCChannel::onVideoFrameReceived(const QByteArray& frame, uint32_t cameraId) {
  const TransportChannel ch = WebRtcTransportDispatch::videoChannelForCameraId(cameraId);
  auto it = m_receivers.constFind(ch);
  if (it != m_receivers.constEnd() && it.value()) {
    PacketMetadata meta;
    meta.channel = ch;
    it.value()(reinterpret_cast<const uint8_t*>(frame.constData()),
               static_cast<size_t>(frame.size()), meta);
  }
  m_stats[ch].bytesReceived += frame.size();
  m_stats[ch].packetsReceived++;
}
