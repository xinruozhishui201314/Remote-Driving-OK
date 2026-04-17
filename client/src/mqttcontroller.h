#ifndef MQTTCONTROLLER_H
#define MQTTCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <mutex>
#include <atomic>

#ifdef ENABLE_MQTT_PAHO
#include <mqtt/async_client.h>
#include <mqtt/connect_options.h>
#include <mqtt/message.h>
#include <mutex>
#include <thread>
#else
class QProcess;  // 前向声明，避免在头文件中包含 QProcess
#endif

// 前向声明 WebRTC 客户端
class WebRtcClient;

/**
 * @brief MQTT 控制器：车辆控制 JSON 经 MQTT 送达车端/桥（见 docs/TELEOP_SIGNAL_CONTRACT.md）。
 * 与 ZLM 的 WebRTC DataChannel 不用于车控投递；DataChannel 仍可由 WebRtcClient 用于编码 hint 等。
 */
class MqttController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(MqttController)

  Q_PROPERTY(QString brokerUrl READ brokerUrl WRITE setBrokerUrl NOTIFY brokerUrlChanged)
  Q_PROPERTY(QString clientId READ clientId WRITE setClientId NOTIFY clientIdChanged)
  Q_PROPERTY(
      QString controlTopic READ controlTopic WRITE setControlTopic NOTIFY controlTopicChanged)
  Q_PROPERTY(QString statusTopic READ statusTopic WRITE setStatusTopic NOTIFY statusTopicChanged)
  Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStatusChanged)
  /** 与 broker 的 MQTT 会话是否已建立（不等同于「能发控车指令」） */
  Q_PROPERTY(bool mqttBrokerConnected READ mqttBrokerConnected NOTIFY mqttBrokerConnectionChanged)
  /** 可经 MQTT 投递控车 JSON：与 broker 会话已建立（车端/carla-bridge 仅订阅 MQTT） */
  Q_PROPERTY(bool controlChannelReady READ controlChannelReady NOTIFY controlChannelReadyChanged)
  Q_PROPERTY(QString currentVin READ currentVin WRITE setCurrentVin NOTIFY currentVinChanged)
  Q_PROPERTY(QString sessionId READ sessionId WRITE setSessionId NOTIFY sessionIdChanged)

 public:
  explicit MqttController(QObject *parent = nullptr);
  ~MqttController();

  QString brokerUrl() const { return m_brokerUrl; }
  void setBrokerUrl(const QString &url);

  QString clientId() const { return m_clientId; }
  void setClientId(const QString &id);

  QString controlTopic() const { return m_controlTopic; }
  void setControlTopic(const QString &topic);

  QString statusTopic() const { return m_statusTopic; }
  void setStatusTopic(const QString &topic);

  QString currentVin() const { return m_currentVin; }
  void setCurrentVin(const QString &vin);

  QString sessionId() const { return m_sessionId; }
  void setSessionId(const QString &id);

  /** 与 controlChannelReady 一致：MQTT 控制面是否可用 */
  bool isConnected() const;
  bool mqttBrokerConnected() const { return m_isConnected; }
  bool controlChannelReady() const;

  // 设置 WebRTC 客户端（用于 DataChannel 发送）
  void setWebRtcClient(WebRtcClient *client);

  // 设置首选控制通道
  void setPreferredChannel(const QString &channelType);

  // 获取当前活动的通道类型（用于日志和调试）
  QString getActiveChannelType() const;

 public slots:
  void connectToBroker();
  void disconnectFromBroker();

  // 车辆控制指令（自动包含 VIN）
  void sendSteeringCommand(double angle);     // 方向盘角度 (-1.0 到 1.0)
  void sendThrottleCommand(double throttle);  // 油门 (0.0 到 1.0)
  void sendBrakeCommand(double brake);        // 刹车 (0.0 到 1.0)
  void sendGearCommand(int gear);  // 档位 (-1: 倒档, 0: 空档, 1: 前进, 2: 停车)
  void sendSweepCommand(const QString &sweepType, bool active);
  void sendLightCommand(const QString &lightType, bool active);
  void sendControlCommand(const QJsonObject &command);

  // 组合控制指令（常用）
  void sendDriveCommand(double steering, double throttle, double brake, int gear = 1, double speed = 0.0);
  void sendSpeedCommand(double speed);  // 目标速度 (0.0 到 100.0 km/h)
  void sendEmergencyStopCommand(
      bool enable);  // 急停命令 (enable=true: 执行急停, enable=false: 解除急停)

  /** 请求车端开始向客户端推流（发送 start_stream 指令，车端订阅后启动推流） */
  Q_INVOKABLE void requestStreamStart();

  /** 请求车端停止推流（发送 stop_stream 指令，车端停止推流进程） */
  Q_INVOKABLE void requestStreamStop();

  /** 请求远驾接管（发送 remote_control 指令，启用/禁用远驾接管状态） */
  Q_INVOKABLE void requestRemoteControl(bool enable);

  /** 经 MQTT 发布客户端编码 hint（teleop/client_encoder_hint） */
  void publishClientEncoderHint(const QJsonObject &hintPayload);

  // 获取下一个序列号（全局单调递增，避免多服务冲突）
  uint32_t nextSequenceNumber() { return m_seq.fetch_add(1); }

 signals:
  void brokerUrlChanged(const QString &url);
  void clientIdChanged(const QString &id);
  void currentVinChanged(const QString &vin);
  void sessionIdChanged(const QString &sessionId);
  void controlTopicChanged(const QString &topic);
  void statusTopicChanged(const QString &topic);
  void connectionStatusChanged(bool connected);
  /** 仅 MQTT broker 连通性变化（供 VehicleStatus::mqttConnected 等） */
  void mqttBrokerConnectionChanged(bool connected);
  /**
   * 单次「用户发起的 connectToBroker」结束（成功连上 broker 或明确失败/超时）。
   * succeeded=false 时 detail 含原因码，供 QML 清除 pendingConnectVideo 等；成功时亦可用于串联 start_stream。
   */
  void mqttConnectResolved(bool succeeded, const QString& detail);
  void controlChannelReadyChanged();
  void statusReceived(const QJsonObject &status);
  void errorOccurred(const QString &error);

 private slots:
  void onConnected();
  void onDisconnected();
  void onMessageReceived(const QByteArray &topic, const QByteArray &payload);
  void onError(const QString &error);
  void onReconnectTimer();

 private:
  /** @param qos 0=at most once, 1=at least once（start_stream/stop_stream 使用 1） */
  bool publishMessage(const QString &topic, const QJsonObject &payload, int qos = 0);
  void updateConnectionStatus(bool connected);
  void bumpControlChannelState();
  void scheduleReconnect();

  // 多通道发送控制指令
  void sendControlCommandViaDataChannel(const QJsonObject &command);
  void sendControlCommandViaMqtt(const QJsonObject &command);

  // 自动选择通道发送控制指令
  void sendControlCommandAuto(const QJsonObject &command);

  QString m_startStreamRequestedVin; // ★ 记录已发送 start_stream 的 VIN，避免重复触发 Bridge 重置
  qint64 m_lastStartStreamTime = 0;   // ★ 记录上次发送 start_stream 的时间戳，支持超时重试
  QString m_brokerUrl = "mqtt://localhost:1883";
  QString m_clientId = "remote_driving_client";
  QString m_controlTopic = "vehicle/control";
  QString m_statusTopic = "vehicle/status";
  QString m_currentVin;  // 当前车辆 VIN
  QString m_sessionId;   // 当前会话 ID
  bool m_isConnected = false;
  std::atomic<uint32_t> m_seq{0};  // 控制指令序列号（防重放）
  mutable std::mutex m_stateMutex;  // 保护 m_currentVin, m_isConnected 等状态

  // 重连增强
  QTimer *m_reconnectTimer = nullptr;
  int m_reconnectAttempts = 0;
  int m_maxReconnectAttempts = 10;
  int m_baseReconnectDelayMs = 1000;
  int m_maxReconnectDelayMs = 30000;
  bool m_reconnectScheduled = false;

  // 多通道支持
  QPointer<WebRtcClient> m_webrtcClient;  // WebRTC DataChannel
  enum class ChannelType {
    AUTO,          // 自动选择 (优先 DataChannel)
    DATA_CHANNEL,  // 强制 DataChannel
    MQTT,          // 强制 MQTT
    WEBSOCKET      // 强制 WebSocket
  };
  ChannelType m_preferredChannel = ChannelType::AUTO;
  ChannelType m_activeChannel = ChannelType::MQTT;  // 当前活动的通道
  bool m_controlChannelReadyCache = false;

  /** 用户点击连接后尚未收到 onConnected / 未判定超时 */
  bool m_brokerConnectInFlight = false;
  /** 递增以作废上一次连接看门狗定时器 */
  quint64 m_connectWatchdogGeneration = 0;

  // 通道使用统计（用于监控）
  uint64_t m_dataChannelCount = 0;
  uint64_t m_mqttCount = 0;
  uint64_t m_websocketCount = 0;

#ifdef ENABLE_MQTT_PAHO
  std::unique_ptr<mqtt::async_client> m_client;
  std::mutex m_mutex;
  std::thread m_mqttThread;
  bool m_shouldStop = false;
#else
  QProcess *m_mosquittoSubProcess = nullptr;  // 用于接收 MQTT 消息的 mosquitto_sub 进程
  QString m_mosquittoLineBuffer;              // 行缓冲，处理 readAllStandardOutput 分片
#endif
};

#endif  // MQTTCONTROLLER_H
