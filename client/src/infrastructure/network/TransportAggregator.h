#pragma once
#include <infrastructure/itransportmanager.h>

#include <QMap>
#include <memory>

/**
 * 传输聚合器（《客户端架构设计》§3.1.1）。
 * 实现双链路冗余（WebRTC DataChannel + MQTT）的热切换逻辑。
 * 
 * 策略：
 *   - 控制指令 (CONTROL_CRITICAL)：默认双发，或根据 RTT 优选。
 *   - 视频 (VIDEO_*)：仅走 WebRTC。
 *   - 遥测 (TELEMETRY)：优先走 MQTT。
 *   - 信令 (SIGNALING)：MQTT。
 */
class TransportAggregator : public ITransportManager {
  Q_OBJECT

 public:
  explicit TransportAggregator(QObject* parent = nullptr);
  ~TransportAggregator() override;

  void addTransport(const QString& name, ITransportManager* transport, bool isPrimary = false);

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
  void onSubTransportQualityChanged(const NetworkQuality& quality);
  void onSubTransportStateChanged(ConnectionState state);

 private:
  struct TransportEntry {
    QString name;
    ITransportManager* transport;
    bool isPrimary;
    NetworkQuality lastQuality;
    ConnectionState state;
  };

  QMap<QString, TransportEntry> m_transports;
  QMap<TransportChannel, std::function<void(const uint8_t*, size_t, const PacketMetadata&)>>
      m_receivers;
  
  TransportConfig m_config;
};
