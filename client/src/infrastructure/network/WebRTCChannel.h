#pragma once
#include "../itransportmanager.h"

#include <QMap>
#include <QObject>

#include <memory>

class WebRtcStreamManager;
class WebRtcClient;

/**
 * WebRTC 传输通道（《客户端架构设计》§3.1.1）。
 * 将现有 WebRtcClient/WebRtcStreamManager 封装为 ITransportManager 通道。
 * 视频通道：VIDEO_PRIMARY/SECONDARY
 * 控制通道（DataChannel）：通过 WebRTC DataChannel 发送控制命令（与 MQTT 互补）
 */
class WebRTCChannel : public ITransportManager {
  Q_OBJECT

 public:
  explicit WebRTCChannel(QObject* parent = nullptr);
  ~WebRTCChannel() override;

  // ITransportManager 接口
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

  // WebRTC 特有：访问底层 stream manager（供 QML 和旧代码访问）
  WebRtcStreamManager* streamManager() const { return m_streamManager.get(); }
  WebRtcClient* primaryClient() const { return m_primaryClient.get(); }

 private slots:
  void onPrimaryConnectionChanged(bool connected);
  void onVideoFrameReceived(const QByteArray& frame, uint32_t cameraId);

 private:
  TransportConfig m_config;
  EndpointInfo m_endpoint;
  std::unique_ptr<WebRtcStreamManager> m_streamManager;
  std::unique_ptr<WebRtcClient> m_primaryClient;

  QMap<TransportChannel, std::function<void(const uint8_t*, size_t, const PacketMetadata&)>>
      m_receivers;

  ConnectionState m_state = ConnectionState::DISCONNECTED;
  NetworkQuality m_quality;
  QMap<TransportChannel, ChannelStats> m_stats;
};
