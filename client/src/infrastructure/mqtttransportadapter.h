#pragma once
#include "itransportmanager.h"
#include "../core/antireplayguard.h"
#include "../core/commandsigner.h"
#include <QMap>

class MqttController;

/**
 * MQTT 传输适配器（《客户端架构设计》§3.1.1）。
 * 将 MqttController 适配为 ITransportManager，主要负责：
 *   - SIGNALING 通道（WebRTC 信令）
 *   - TELEMETRY 通道（遥测数据上报）
 *   - CONTROL_CRITICAL 通道（降级时的控制命令，UDP 不可用时回退）
 */
class MqttTransportAdapter final : public ITransportManager {
    Q_OBJECT

public:
    explicit MqttTransportAdapter(MqttController* mqtt, QObject* parent = nullptr);
    ~MqttTransportAdapter() override;

    /**
     * 设置会话凭证，启用 HMAC 签名和反重放验证。
     * 应在会话建立后（获取到 token）立即调用。
     */
    void setSessionCredentials(const QString& vin, const QString& sessionId,
                                const QString& token);

    // ITransportManager interface
    bool initialize(const TransportConfig& config) override;
    void shutdown() override;
    void connectAsync(const EndpointInfo& endpoint) override;
    void disconnect() override;
    SendResult send(TransportChannel channel, const uint8_t* data, size_t len,
                    SendFlags flags = SendFlags::Default) override;
    void registerReceiver(
        TransportChannel channel,
        std::function<void(const uint8_t*, size_t, const PacketMetadata&)> callback) override;
    NetworkQuality getNetworkQuality() const override;
    ChannelStats getChannelStats(TransportChannel ch) const override;
    ConnectionState connectionState() const override;

private slots:
    void onMqttConnected();
    void onMqttDisconnected();
    void onMqttMessageReceived(const QString& topic, const QByteArray& payload);

private:
    MqttController*    m_mqtt  = nullptr;
    ConnectionState    m_state = ConnectionState::DISCONNECTED;
    NetworkQuality     m_quality;
    QMap<TransportChannel, std::function<void(const uint8_t*, size_t, const PacketMetadata&)>> m_receivers;
    EndpointInfo       m_endpoint;

    // 安全组件
    AntiReplayGuard m_inboundReplay;   // 针对入站遥测/状态消息的反重放
    CommandSigner   m_signer;          // 出站命令签名（出站方向，可选验证入站）
    bool            m_signingEnabled = false;
};
