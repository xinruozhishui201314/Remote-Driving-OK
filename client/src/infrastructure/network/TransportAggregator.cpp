#include "TransportAggregator.h"

#include <QDebug>

TransportAggregator::TransportAggregator(QObject* parent) : ITransportManager(parent) {}

TransportAggregator::~TransportAggregator() { shutdown(); }

void TransportAggregator::addTransport(const QString& name, ITransportManager* transport,
                                       bool isPrimary) {
  if (!transport) return;

  TransportEntry entry;
  entry.name = name;
  entry.transport = transport;
  entry.isPrimary = isPrimary;
  entry.state = transport->connectionState();
  entry.lastQuality = transport->getNetworkQuality();

  m_transports[name] = entry;

  connect(transport, &ITransportManager::networkQualityChanged, this,
          &TransportAggregator::onSubTransportQualityChanged);
  connect(transport, &ITransportManager::connectionStateChanged, this,
          &TransportAggregator::onSubTransportStateChanged);
  
  qInfo() << "[Client][TransportAggregator] added transport=" << name << "primary=" << isPrimary;
}

bool TransportAggregator::initialize(const TransportConfig& config) {
  m_config = config;
  bool ok = true;
  for (auto& entry : m_transports) {
    if (!entry.transport->initialize(config)) {
      qWarning() << "[Client][TransportAggregator] failed to initialize transport=" << entry.name;
      ok = false;
    }
  }
  return ok;
}

void TransportAggregator::shutdown() {
  for (auto& entry : m_transports) {
    entry.transport->shutdown();
  }
  qInfo() << "[Client][TransportAggregator] shutdown";
}

void TransportAggregator::connectAsync(const EndpointInfo& endpoint) {
  for (auto& entry : m_transports) {
    entry.transport->connectAsync(endpoint);
  }
}

void TransportAggregator::disconnect() {
  for (auto& entry : m_transports) {
    entry.transport->disconnect();
  }
}

SendResult TransportAggregator::send(TransportChannel channel, const uint8_t* data, size_t len,
                                     SendFlags flags) {
  // ─── 自适应双链路冗余逻辑 (Adaptive Dual-Send) ───
  // 基于 Google SRE 网络规范：在抖动/高延迟环境下开启全量冗余，在健康环境下优先主链路。
  if (channel == TransportChannel::CONTROL_CRITICAL) {
    SendResult lastRes = {false, "No transport available"};
    int sentCount = 0;

    // 获取主链路质量
    NetworkQuality primaryQuality;
    bool primaryHealthy = false;
    for (const auto& entry : m_transports) {
        if (entry.isPrimary && entry.state == ConnectionState::CONNECTED) {
            primaryQuality = entry.lastQuality;
            // 健康标准：RTT < 100ms 且 Jitter < 30ms 且 丢包 < 2%
            if (primaryQuality.rttMs < 100.0 && primaryQuality.jitterMs < 30.0 && 
                primaryQuality.packetLossRate < 0.02) {
                primaryHealthy = true;
            }
            break;
        }
    }

    // 冗余决策：
    // 1. 如果主链路不健康，或者抖动剧烈 -> 开启双发 (Full Redundancy)
    // 2. 如果主链路健康 -> 仅主发 (Primary Only) 以节省带宽并减少乱序风险
    bool enableFullRedundancy = !primaryHealthy;

    for (auto& entry : m_transports) {
      if (entry.state == ConnectionState::CONNECTED) {
        if (enableFullRedundancy || entry.isPrimary) {
            auto res = entry.transport->send(channel, data, len, flags);
            if (res.success) {
                lastRes = res;
                sentCount++;
            }
        }
      }
    }
    
    if (sentCount > 0) return {true, ""};
    return lastRes;
  }

  // 视频通道仅走 WebRTC (Primary)
  if (channel == TransportChannel::VIDEO_PRIMARY || channel == TransportChannel::VIDEO_SECONDARY) {
    for (auto& entry : m_transports) {
      if (entry.isPrimary && entry.state == ConnectionState::CONNECTED) {
        return entry.transport->send(channel, data, len, flags);
      }
    }
    return {false, "Primary transport (WebRTC) not connected for video"};
  }

  // 其他通道默认尝试所有可用传输
  for (auto& entry : m_transports) {
    if (entry.state == ConnectionState::CONNECTED) {
      return entry.transport->send(channel, data, len, flags);
    }
  }

  return {false, "No connected transport"};
}

void TransportAggregator::registerReceiver(
    TransportChannel channel,
    std::function<void(const uint8_t*, size_t, const PacketMetadata&)> callback) {
  m_receivers[channel] = callback;
  for (auto& entry : m_transports) {
    entry.transport->registerReceiver(channel, callback);
  }
}

NetworkQuality TransportAggregator::getNetworkQuality() const {
  // 返回最优链路的质量
  NetworkQuality best;
  best.score = 0;
  for (const auto& entry : m_transports) {
    if (entry.state == ConnectionState::CONNECTED && entry.lastQuality.score > best.score) {
      best = entry.lastQuality;
    }
  }
  return best;
}

ChannelStats TransportAggregator::getChannelStats(TransportChannel ch) const {
  ChannelStats total(ch);
  for (const auto& entry : m_transports) {
    auto s = entry.transport->getChannelStats(ch);
    total.bytesSent += s.bytesSent;
    total.bytesReceived += s.bytesReceived;
    total.packetsSent += s.packetsSent;
    total.packetsReceived += s.packetsReceived;
    total.packetsLost += s.packetsLost;
    if (total.latencyMs == 0 || (s.latencyMs > 0 && s.latencyMs < total.latencyMs)) {
      total.latencyMs = s.latencyMs;
    }
  }
  return total;
}

ConnectionState TransportAggregator::connectionState() const {
  bool anyConnected = false;
  bool anyConnecting = false;
  for (const auto& entry : m_transports) {
    if (entry.state == ConnectionState::CONNECTED) anyConnected = true;
    if (entry.state == ConnectionState::CONNECTING) anyConnecting = true;
  }
  if (anyConnected) return ConnectionState::CONNECTED;
  if (anyConnecting) return ConnectionState::CONNECTING;
  return ConnectionState::DISCONNECTED;
}

void TransportAggregator::onSubTransportQualityChanged(const NetworkQuality& quality) {
  auto* t = qobject_cast<ITransportManager*>(sender());
  if (!t) return;

  for (auto& entry : m_transports) {
    if (entry.transport == t) {
      entry.lastQuality = quality;
      break;
    }
  }
  emit networkQualityChanged(getNetworkQuality());
}

void TransportAggregator::onSubTransportStateChanged(ConnectionState state) {
  auto* t = qobject_cast<ITransportManager*>(sender());
  if (!t) return;

  for (auto& entry : m_transports) {
    if (entry.transport == t) {
      entry.state = state;
      break;
    }
  }
  emit connectionStateChanged(connectionState());
}
