#include "WebRTCChannel.h"

#include <utils/WebRtcTransportDispatch.h>
#include <webrtcclient.h>
#include <webrtcstreammanager.h>

#include <QDebug>
#include <QProcessEnvironment>

WebRTCChannel::WebRTCChannel(QObject* parent) : ITransportManager(parent) {}

WebRTCChannel::~WebRTCChannel() { shutdown(); }

bool WebRTCChannel::initialize(const TransportConfig& config) {
  m_config = config;
  // 核心修复：不再内部创建实例，由外部 injectInstances 注入或手动 check
  qInfo() << "[Client][WebRTCChannel] initialized";
  return true;
}

void WebRTCChannel::injectInstances(WebRtcStreamManager* wsm, WebRtcClient* primary) {
  m_streamManager = wsm;
  m_primaryClient = primary;

  if (m_primaryClient) {
    connect(m_primaryClient, &WebRtcClient::connectionStatusChanged, this,
            &WebRTCChannel::onPrimaryConnectionChanged, Qt::UniqueConnection);
  }
  qInfo() << "[Client][WebRTCChannel] instances injected wsm=" << (void*)wsm << " primary=" << (void*)primary;
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
      channel, static_cast<bool>(m_primaryClient),
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
    meta.receiveTimestampMs = QDateTime::currentMSecsSinceEpoch();

    const uint8_t* data = reinterpret_cast<const uint8_t*>(frame.constData());
    size_t size = static_cast<size_t>(frame.size());

    // ─── 自定义扩展头解析 (2025/2026 规范) ───
    // Header Format (24 bytes): 
    //   [0-3]   Magic: 0xDE 0xAD 0xBE 0xEF
    //   [4-11]  CaptureTimestamp (us, Big-Endian)
    //   [12]    FrameType (1=I, 2=P, 3=B)
    //   [13]    DropHint (0=Normal, 1=DropSuggested)
    //   [14-23] Reserved
    if (size >= 24 && data[0] == 0xDE && data[1] == 0xAD && data[2] == 0xBE && data[3] == 0xEF) {
      uint64_t ts = 0;
      for (int i = 0; i < 8; ++i) ts = (ts << 8) | data[4 + i];
      meta.captureTimestampUs = static_cast<int64_t>(ts);
      meta.frameType = data[12];
      meta.dropHint = (data[13] != 0);
      
      // 剥离 Header 传递给下游解码器
      data += 24;
      size -= 24;
    }

    it.value()(data, size, meta);
  }
  m_stats[ch].bytesReceived += frame.size();
  m_stats[ch].packetsReceived++;
}
