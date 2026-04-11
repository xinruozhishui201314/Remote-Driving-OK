#include "mqtttransportadapter.h"

#include "../mqttcontroller.h"
#include "../utils/TimeUtils.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

MqttTransportAdapter::MqttTransportAdapter(MqttController* mqtt, QObject* parent)
    : ITransportManager(parent), m_mqtt(mqtt) {
  if (m_mqtt) {
    connect(m_mqtt, &MqttController::connectionStatusChanged, this, [this](bool connected) {
      if (connected)
        onMqttConnected();
      else
        onMqttDisconnected();
    });
    // Route vehicle status messages to TELEMETRY receivers
    connect(m_mqtt, &MqttController::statusReceived, this, [this](const QJsonObject& status) {
      const QByteArray payload = QJsonDocument(status).toJson(QJsonDocument::Compact);
      onMqttMessageReceived("telemetry", payload);
    });
  }
}

MqttTransportAdapter::~MqttTransportAdapter() { shutdown(); }

bool MqttTransportAdapter::initialize(const TransportConfig& config) {
  Q_UNUSED(config)
  qInfo() << "[Client][MqttTransportAdapter] initialized";
  return m_mqtt != nullptr;
}

void MqttTransportAdapter::shutdown() { m_state = ConnectionState::DISCONNECTED; }

void MqttTransportAdapter::connectAsync(const EndpointInfo& endpoint) {
  m_endpoint = endpoint;
  m_state = ConnectionState::CONNECTING;
  emit connectionStateChanged(m_state);
  // Actual connection is handled by MqttController
}

void MqttTransportAdapter::disconnect() {
  if (m_mqtt)
    m_mqtt->disconnectFromBroker();
  m_state = ConnectionState::DISCONNECTED;
  emit connectionStateChanged(m_state);
  emit disconnected();
}

SendResult MqttTransportAdapter::send(TransportChannel channel, const uint8_t* data, size_t len,
                                      SendFlags /*flags*/) {
  if (!m_mqtt)
    return SendResult{false, "No MQTT controller"};

  const QByteArray payload(reinterpret_cast<const char*>(data), static_cast<int>(len));

  switch (channel) {
    case TransportChannel::CONTROL_CRITICAL:
    case TransportChannel::SIGNALING: {
      // Try to parse as JSON command
      const QJsonDocument doc = QJsonDocument::fromJson(payload);
      if (!doc.isNull() && doc.isObject()) {
        m_mqtt->sendControlCommand(doc.object());
      } else {
        // Raw bytes → wrap in JSON envelope
        QJsonObject envelope;
        envelope["raw"] = QString::fromUtf8(payload.toBase64());
        m_mqtt->sendControlCommand(envelope);
      }
      return SendResult{true, {}};
    }
    case TransportChannel::TELEMETRY:
    case TransportChannel::DIAGNOSTIC: {
      // MQTT controller doesn't have dedicated publish; wrap in control command
      const QJsonDocument doc = QJsonDocument::fromJson(payload);
      if (!doc.isNull() && doc.isObject()) {
        m_mqtt->sendControlCommand(doc.object());
      }
      return SendResult{true, {}};
    }
    default:
      return SendResult{false, "Channel not supported by MQTT adapter"};
  }
}

void MqttTransportAdapter::registerReceiver(
    TransportChannel channel,
    std::function<void(const uint8_t*, size_t, const PacketMetadata&)> callback) {
  m_receivers[channel] = std::move(callback);
}

NetworkQuality MqttTransportAdapter::getNetworkQuality() const { return m_quality; }

ChannelStats MqttTransportAdapter::getChannelStats(TransportChannel ch) const {
  ChannelStats stats;
  stats.channel = ch;
  return stats;
}

ConnectionState MqttTransportAdapter::connectionState() const { return m_state; }

void MqttTransportAdapter::onMqttConnected() {
  m_state = ConnectionState::CONNECTED;
  emit connectionStateChanged(m_state);
  emit connected();
  qInfo() << "[Client][MqttTransportAdapter] MQTT connected";
}

void MqttTransportAdapter::onMqttDisconnected() {
  m_state = ConnectionState::DISCONNECTED;
  emit connectionStateChanged(m_state);
  emit disconnected();
  qWarning() << "[Client][MqttTransportAdapter] MQTT disconnected";
}

void MqttTransportAdapter::setSessionCredentials(const QString& vin, const QString& sessionId,
                                                 const QString& token) {
  m_signer.setCredentials(vin, sessionId, token);
  m_inboundReplay.reset();
  m_signingEnabled = true;
  qInfo() << "[Client][MqttTransportAdapter] security enabled for vin=" << vin;
}

void MqttTransportAdapter::onMqttMessageReceived(const QString& topic, const QByteArray& payload) {
  // 主题路由
  TransportChannel ch = TransportChannel::TELEMETRY;  // default
  if (topic.contains("control"))
    ch = TransportChannel::CONTROL_CRITICAL;
  else if (topic.contains("signal"))
    ch = TransportChannel::SIGNALING;

  const int64_t localNow = TimeUtils::wallClockMs();

  // ── 对遥测/状态消息做反重放验证 ──────────────────────────────────
  if (m_signingEnabled &&
      (ch == TransportChannel::TELEMETRY || ch == TransportChannel::CONTROL_CRITICAL)) {
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isNull() && doc.isObject()) {
      const QJsonObject obj = doc.object();
      const uint32_t seq = static_cast<uint32_t>(obj.value("seq").toDouble(0));
      const int64_t msgTs = static_cast<int64_t>(obj.value("timestamp").toDouble(0));

      if (seq > 0 || msgTs > 0) {
        QString reason;
        if (!m_inboundReplay.checkAndRecord(seq, msgTs, localNow, &reason)) {
          qWarning() << "[Client][MqttTransportAdapter] REPLAY BLOCKED"
                     << "topic=" << topic << "reason=" << reason;
          return;  // 丢弃，不转发给上层
        }
      }

      // 若服务端在响应消息中加了 hmac，也可以验证
      if (obj.contains("hmac") && m_signer.isReady()) {
        QString reason;
        if (!m_signer.verify(obj, &reason)) {
          qWarning() << "[Client][MqttTransportAdapter] HMAC verify failed"
                     << "topic=" << topic << "reason=" << reason;
          // 不直接拒绝（服务端可能未签名），仅告警
        }
      }
    }
  }

  auto it = m_receivers.constFind(ch);
  if (it != m_receivers.constEnd() && it.value()) {
    PacketMetadata meta;
    meta.receiveTimestampMs = localNow;
    meta.channel = ch;
    it.value()(reinterpret_cast<const uint8_t*>(payload.constData()),
               static_cast<size_t>(payload.size()), meta);
  }
}
