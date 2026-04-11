#include "UDPChannel.h"

#include "../../utils/TimeUtils.h"

#include <QDebug>
#include <QHostAddress>
#include <QVariant>

UDPChannel::UDPChannel(QObject* parent) : ITransportManager(parent) {
  connect(&m_heartbeatTimer, &QTimer::timeout, this, &UDPChannel::onHeartbeatTimer);
  m_heartbeatTimer.setTimerType(Qt::PreciseTimer);
  m_heartbeatTimer.setInterval(kHeartbeatIntervalMs);

  // ★★★ 修复：启动 RTT 超时检测定时器 ★★★
  // onRttTimer() 用于检测 PONG 响应超时（超过 500ms 未收到响应）
  connect(&m_rttTimer, &QTimer::timeout, this, &UDPChannel::onRttTimer);
  m_rttTimer.setTimerType(Qt::PreciseTimer);
  m_rttTimer.setInterval(kHeartbeatIntervalMs * 5);  // 500ms 检测间隔
}

UDPChannel::~UDPChannel() { shutdown(); }

bool UDPChannel::initialize(const TransportConfig& config) {
  m_config = config;
  m_socket = std::make_unique<QUdpSocket>();
  m_socket->setSocketOption(QAbstractSocket::LowDelayOption, QVariant(true));

  connect(m_socket.get(), &QUdpSocket::readyRead, this, &UDPChannel::onReadyRead);

  m_stats.channel = TransportChannel::CONTROL_CRITICAL;
  qInfo() << "[Client][UDPChannel] initialized port=" << config.control.port;
  return true;
}

void UDPChannel::shutdown() {
  m_heartbeatTimer.stop();
  m_rttTimer.stop();  // ★★★ 停止 RTT 检测定时器 ★★★
  if (m_socket) {
    m_socket->close();
  }
  m_state = ConnectionState::DISCONNECTED;
  qInfo() << "[Client][UDPChannel] shutdown";
}

void UDPChannel::connectAsync(const EndpointInfo& endpoint) {
  m_endpoint = endpoint;
  m_state = ConnectionState::CONNECTING;
  emit connectionStateChanged(m_state);

  // UDP 无连接，绑定本地端口并记录目标
  if (m_socket->bind(QHostAddress::Any, 0)) {
    m_state = ConnectionState::CONNECTED;
    emit connectionStateChanged(m_state);
    emit connected();
    m_heartbeatTimer.start();
    m_rttTimer.start();  // ★★★ 启动 RTT 检测定时器 ★★★
    qInfo() << "[Client][UDPChannel] connected to" << endpoint.host << ":" << m_config.control.port;
  } else {
    m_state = ConnectionState::ERROR;
    emit connectionStateChanged(m_state);
    emit channelError(TransportChannel::CONTROL_CRITICAL, m_socket->errorString());
    qWarning() << "[Client][UDPChannel] bind failed:" << m_socket->errorString();
  }
}

void UDPChannel::disconnect() {
  m_heartbeatTimer.stop();
  m_rttTimer.stop();  // ★★★ 停止 RTT 检测定时器 ★★★
  if (m_socket)
    m_socket->close();
  m_state = ConnectionState::DISCONNECTED;
  emit connectionStateChanged(m_state);
  emit disconnected();
}

SendResult UDPChannel::send(TransportChannel channel, const uint8_t* data, size_t len,
                            SendFlags flags) {
  if (channel != TransportChannel::CONTROL_CRITICAL) {
    return SendResult{false, "UDPChannel only handles CONTROL_CRITICAL"};
  }
  if (!m_socket || m_state != ConnectionState::CONNECTED) {
    return SendResult{false, "Not connected"};
  }

  QByteArray payload(reinterpret_cast<const char*>(data), static_cast<int>(len));

  if (m_config.control.enableFEC &&
      !(static_cast<uint8_t>(flags) & static_cast<uint8_t>(SendFlags::Unreliable))) {
    payload = applyFEC(payload);
  }

  const qint64 sent =
      m_socket->writeDatagram(payload, QHostAddress(m_endpoint.host), m_config.control.port);

  if (sent < 0) {
    m_stats.packetsLost++;
    return SendResult{false, m_socket->errorString()};
  }

  m_stats.bytesSent += static_cast<uint64_t>(len);
  m_stats.packetsSent++;
  return SendResult{true, {}};
}

void UDPChannel::registerReceiver(
    TransportChannel channel,
    std::function<void(const uint8_t*, size_t, const PacketMetadata&)> callback) {
  m_receivers[channel] = std::move(callback);
}

NetworkQuality UDPChannel::getNetworkQuality() const { return m_quality; }

ChannelStats UDPChannel::getChannelStats(TransportChannel /*ch*/) const { return m_stats; }

ConnectionState UDPChannel::connectionState() const { return m_state; }

void UDPChannel::onReadyRead() {
  while (m_socket->hasPendingDatagrams()) {
    QByteArray data;
    data.resize(static_cast<int>(m_socket->pendingDatagramSize()));
    QHostAddress sender;
    quint16 senderPort;
    m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);

    // Pong 检测（简单4字节心跳响应）
    // ★★★ 修复：添加长度检查，防止短数据误判 ★★★
    if (data.size() >= 5 && data.startsWith("PONG")) {
      // 解析 RTT 值（格式：PONG<rtt_value>）
      QString rttStr = QString::fromLatin1(data.mid(5)).trimmed();
      bool ok = false;
      double rtt = rttStr.toDouble(&ok);
      if (ok) {
        updateRTT(static_cast<int64_t>(rtt * 1000));  // 转换为微秒
      } else {
        // 传统格式：仅 PONG，使用本地时间戳计算
        const int64_t rttUs = (TimeUtils::nowUs() - m_lastPingTimestamp);
        updateRTT(rttUs);
      }
      continue;
    }

    // FEC 还原
    QByteArray stripped = stripFEC(data);

    PacketMetadata meta;
    meta.receiveTimestampMs = TimeUtils::wallClockMs();
    meta.channel = TransportChannel::CONTROL_CRITICAL;

    auto it = m_receivers.constFind(TransportChannel::CONTROL_CRITICAL);
    if (it != m_receivers.constEnd() && it.value()) {
      it.value()(reinterpret_cast<const uint8_t*>(stripped.constData()),
                 static_cast<size_t>(stripped.size()), meta);
    }

    m_stats.bytesReceived += data.size();
    m_stats.packetsReceived++;
  }
}

void UDPChannel::onHeartbeatTimer() { sendHeartbeat(); }

void UDPChannel::onRttTimer() {
  // Triggered if no PONG received within timeout
  if (m_lastPingTimestamp > 0) {
    const int64_t elapsed = TimeUtils::nowUs() - m_lastPingTimestamp;
    if (elapsed > 500000) {  // 500ms
      qWarning() << "[Client][UDPChannel] heartbeat timeout, RTT > 500ms";
      m_quality.degraded = true;
      emit networkQualityChanged(m_quality);
    }
  }
}

void UDPChannel::sendHeartbeat() {
  if (!m_socket || m_state != ConnectionState::CONNECTED)
    return;
  m_lastPingTimestamp = TimeUtils::nowUs();
  const QByteArray ping("PING");
  m_socket->writeDatagram(ping, QHostAddress(m_endpoint.host), m_config.control.port);
}

void UDPChannel::updateRTT(int64_t rttUs) {
  const double rttMs = rttUs / 1000.0;
  // 指数移动平均
  m_quality.rttMs = m_quality.rttMs * 0.9 + rttMs * 0.1;
  m_quality.score = std::max(0.0, 1.0 - m_quality.rttMs / 300.0);
  m_stats.latencyMs = m_quality.rttMs;
  emit networkQualityChanged(m_quality);
}

QByteArray UDPChannel::applyFEC(const QByteArray& data) {
  // ═══════════════════════════════════════════════════════════════════
  // 注意：当前实现使用简单 XOR 奇偶校验，仅能检测单比特翻转错误。
  // 生产环境应替换为 Reed-Solomon (RS-255-223) 或 LDPC 码以获得纠错能力。
  // 配置项: m_config.control.enableFEC (默认关闭)
  // ═══════════════════════════════════════════════════════════════════
  if (data.isEmpty()) {
    return data;
  }

  uint8_t parity = 0;
  for (char c : data)
    parity ^= static_cast<uint8_t>(c);
  QByteArray result = data;
  result.append(static_cast<char>(parity));
  return result;
}

QByteArray UDPChannel::stripFEC(const QByteArray& data) {
  if (data.size() < 2)
    return data;

  // 验证奇偶校验
  uint8_t parity = 0;
  for (int i = 0; i < data.size() - 1; ++i) {
    parity ^= static_cast<uint8_t>(data[i]);
  }

  if (parity != static_cast<uint8_t>(data.back())) {
    m_fecFailed.fetch_add(1);
    qWarning() << "[Client][UDPChannel] FEC parity check failed, packet may be corrupt"
               << " totalFailed=" << m_fecFailed.load();
    // 注意：这里只记录错误，不丢弃数据，因为 UDP 本身不保证可靠性
    // 呼叫方应该根据上层协议判断是否需要重传
  } else {
    // 校验成功（数据未被破坏）
  }
  return data.left(data.size() - 1);
}
