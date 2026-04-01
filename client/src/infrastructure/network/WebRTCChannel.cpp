#include "WebRTCChannel.h"
#include "../../webrtcclient.h"
#include "../../webrtcstreammanager.h"
#include <QDebug>

WebRTCChannel::WebRTCChannel(QObject* parent)
    : ITransportManager(parent)
{
}

WebRTCChannel::~WebRTCChannel()
{
    shutdown();
}

bool WebRTCChannel::initialize(const TransportConfig& config)
{
    m_config = config;
    m_streamManager = std::make_unique<WebRtcStreamManager>();
    m_primaryClient = std::make_unique<WebRtcClient>();

    connect(m_primaryClient.get(), &WebRtcClient::connectionStatusChanged,
            this, &WebRTCChannel::onPrimaryConnectionChanged);

    qInfo() << "[Client][WebRTCChannel] initialized";
    return true;
}

void WebRTCChannel::shutdown()
{
    if (m_streamManager) {
        m_streamManager->disconnectAll();
    }
    m_state = ConnectionState::DISCONNECTED;
    qInfo() << "[Client][WebRTCChannel] shutdown";
}

void WebRTCChannel::connectAsync(const EndpointInfo& endpoint)
{
    m_endpoint = endpoint;
    m_state = ConnectionState::CONNECTING;
    emit connectionStateChanged(m_state);

    qInfo() << "[Client][WebRTCChannel] connecting to" << endpoint.host
            << "vin=" << endpoint.vin;

    // 连接四路摄像头流（whepUrl 由 host 构造；先设置 VIN 以构造 VIN-prefixed 流名）
    if (m_streamManager) {
        if (!endpoint.vin.isEmpty())
            m_streamManager->setCurrentVin(endpoint.vin);
        const QString whepUrl = QString("http://%1/whep/%2/%3")
                                    .arg(endpoint.host, endpoint.vin, endpoint.sessionId);
        m_streamManager->connectFourStreams(whepUrl);
    }
}

void WebRTCChannel::disconnect()
{
    if (m_streamManager) {
        m_streamManager->disconnectAll();
    }
    m_state = ConnectionState::DISCONNECTED;
    emit connectionStateChanged(m_state);
    emit disconnected();
    qInfo() << "[Client][WebRTCChannel] disconnected";
}

SendResult WebRTCChannel::send(TransportChannel channel, const uint8_t* data,
                                size_t len, SendFlags /*flags*/)
{
    if (channel == TransportChannel::CONTROL_CRITICAL ||
        channel == TransportChannel::SIGNALING) {
        // DataChannel 发送控制命令
        if (m_primaryClient) {
            QByteArray payload(reinterpret_cast<const char*>(data),
                               static_cast<int>(len));
            m_primaryClient->sendDataChannelMessage(payload);
            m_stats[channel].bytesSent += len;
            m_stats[channel].packetsSent++;
            return SendResult{true, {}};
        }
        return SendResult{false, "Primary WebRTC client not available"};
    }
    return SendResult{false, "Channel not supported by WebRTCChannel"};
}

void WebRTCChannel::registerReceiver(
    TransportChannel channel,
    std::function<void(const uint8_t*, size_t, const PacketMetadata&)> callback)
{
    m_receivers[channel] = std::move(callback);
}

NetworkQuality WebRTCChannel::getNetworkQuality() const
{
    return m_quality;
}

ChannelStats WebRTCChannel::getChannelStats(TransportChannel ch) const
{
    return m_stats.value(ch, ChannelStats{ch});
}

ConnectionState WebRTCChannel::connectionState() const
{
    return m_state;
}

void WebRTCChannel::onPrimaryConnectionChanged(bool connected)
{
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

void WebRTCChannel::onVideoFrameReceived(const QByteArray& frame, uint32_t cameraId)
{
    TransportChannel ch = (cameraId == 0) ? TransportChannel::VIDEO_PRIMARY
                                           : TransportChannel::VIDEO_SECONDARY;
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
