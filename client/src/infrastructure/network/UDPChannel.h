#pragma once
#include "../itransportmanager.h"

#include <QMap>
#include <QTimer>
#include <QUdpSocket>

#include <atomic>
#include <memory>

/**
 * UDP 直连控制通道（《客户端架构设计》§3.1.1）。
 * 专用于 CONTROL_CRITICAL 通道：低延迟 UDP + 可选 FEC。
 * 最大 P99 延迟 < 20ms。
 */
class UDPChannel : public ITransportManager {
  Q_OBJECT

 public:
  explicit UDPChannel(QObject* parent = nullptr);
  ~UDPChannel() override;

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
  void onReadyRead();
  void onHeartbeatTimer();
  void onRttTimer();

 private:
  void sendHeartbeat();
  void updateRTT(int64_t rttUs);
  QByteArray applyFEC(const QByteArray& data);
  QByteArray stripFEC(const QByteArray& data);

  TransportConfig m_config;
  EndpointInfo m_endpoint;
  std::unique_ptr<QUdpSocket> m_socket;
  QTimer m_heartbeatTimer;
  QTimer m_rttTimer;

  ConnectionState m_state = ConnectionState::DISCONNECTED;
  NetworkQuality m_quality;
  ChannelStats m_stats;

  // FEC 统计
  std::atomic<uint64_t> m_fecRecovered{0};  // 通过奇偶校验恢复的包数
  std::atomic<uint64_t> m_fecFailed{0};     // FEC 校验失败的包数

  QMap<TransportChannel, std::function<void(const uint8_t*, size_t, const PacketMetadata&)>>
      m_receivers;

  // RTT measurement
  int64_t m_lastPingTimestamp = 0;
  static constexpr int kHeartbeatIntervalMs = 100;
};
