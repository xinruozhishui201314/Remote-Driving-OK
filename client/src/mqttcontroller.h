#ifndef MQTTCONTROLLER_H
#define MQTTCONTROLLER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QTimer>
#include <QPointer>

#ifdef ENABLE_MQTT_PAHO
#include <mqtt/async_client.h>
#include <mqtt/connect_options.h>
#include <mqtt/message.h>
#include <thread>
#include <mutex>
#else
class QProcess;  // 前向声明，避免在头文件中包含 QProcess
#endif

// 前向声明 WebRTC 客户端
class WebRtcClient;

/**
 * @brief MQTT 控制器类（已扩展为多通道控制器）
 * 负责通过多个通道（DataChannel/MQTT/WebSocket）发送车辆控制指令到车端模块
 * 优先级：DataChannel > MQTT > WebSocket
 */
class MqttController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString brokerUrl READ brokerUrl WRITE setBrokerUrl NOTIFY brokerUrlChanged)
    Q_PROPERTY(QString clientId READ clientId WRITE setClientId NOTIFY clientIdChanged)
    Q_PROPERTY(QString controlTopic READ controlTopic WRITE setControlTopic NOTIFY controlTopicChanged)
    Q_PROPERTY(QString statusTopic READ statusTopic WRITE setStatusTopic NOTIFY statusTopicChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStatusChanged)

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
    
    /** 任一控制通道可用（DataChannel 或 MQTT）即视为已连接 */
    bool isConnected() const;
    
    // 设置 WebRTC 客户端（用于 DataChannel 发送）
    void setWebRtcClient(WebRtcClient* client);
    
    // 设置首选控制通道
    void setPreferredChannel(const QString &channelType);
    
    // 获取当前活动的通道类型（用于日志和调试）
    QString getActiveChannelType() const;

public slots:
    void connectToBroker();
    void disconnectFromBroker();
    
    // 车辆控制指令（自动包含 VIN）
    void sendSteeringCommand(double angle);      // 方向盘角度 (-1.0 到 1.0)
    void sendThrottleCommand(double throttle);  // 油门 (0.0 到 1.0)
    void sendBrakeCommand(double brake);         // 刹车 (0.0 到 1.0)
    void sendGearCommand(int gear);              // 档位 (-1: 倒档, 0: 空档, 1: 前进, 2: 停车)
    void sendSweepCommand(const QString &sweepType, bool active);
    void sendLightCommand(const QString &lightType, bool active);
    void sendControlCommand(const QJsonObject &command);
    
    // 组合控制指令（常用）
    void sendDriveCommand(double steering, double throttle, double brake, int gear = 1);
    void sendSpeedCommand(double speed);  // 目标速度 (0.0 到 100.0 km/h)
    void sendEmergencyStopCommand(bool enable);  // 急停命令 (enable=true: 执行急停, enable=false: 解除急停)

    /** 请求车端开始向客户端推流（发送 start_stream 指令，车端订阅后启动推流） */
    Q_INVOKABLE void requestStreamStart();
    
    /** 请求车端停止推流（发送 stop_stream 指令，车端停止推流进程） */
    Q_INVOKABLE void requestStreamStop();
    
    /** 请求远驾接管（发送 remote_control 指令，启用/禁用远驾接管状态） */
    Q_INVOKABLE void requestRemoteControl(bool enable);

signals:
    void brokerUrlChanged(const QString &url);
    void clientIdChanged(const QString &id);
    void controlTopicChanged(const QString &topic);
    void statusTopicChanged(const QString &topic);
    void connectionStatusChanged(bool connected);
    void statusReceived(const QJsonObject &status);
    void errorOccurred(const QString &error);

private slots:
    void onConnected();
    void onDisconnected();
    void onMessageReceived(const QByteArray &topic, const QByteArray &payload);
    void onError(const QString &error);

private:
    void publishMessage(const QString &topic, const QJsonObject &payload);
    void updateConnectionStatus(bool connected);
    
    // 多通道发送控制指令
    void sendControlCommandViaDataChannel(const QJsonObject &command);
    void sendControlCommandViaMqtt(const QJsonObject &command);
    
    // 自动选择通道发送控制指令
    void sendControlCommandAuto(const QJsonObject &command);

    QString m_brokerUrl = "mqtt://localhost:1883";
    QString m_clientId = "remote_driving_client";
    QString m_controlTopic = "vehicle/control";
    QString m_statusTopic = "vehicle/status";
    QString m_currentVin;  // 当前车辆 VIN
    bool m_isConnected = false;
    uint32_t m_seq = 0;  // 控制指令序列号（防重放）
    
    // 多通道支持
    QPointer<WebRtcClient> m_webrtcClient;  // WebRTC DataChannel
    enum class ChannelType {
        AUTO,           // 自动选择 (优先 DataChannel)
        DATA_CHANNEL,   // 强制 DataChannel
        MQTT,           // 强制 MQTT
        WEBSOCKET       // 强制 WebSocket
    };
    ChannelType m_preferredChannel = ChannelType::AUTO;
    ChannelType m_activeChannel = ChannelType::MQTT;  // 当前活动的通道
    
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
    QProcess* m_mosquittoSubProcess = nullptr;  // 用于接收 MQTT 消息的 mosquitto_sub 进程
    QString m_mosquittoLineBuffer;  // 行缓冲，处理 readAllStandardOutput 分片
#endif
};

#endif // MQTTCONTROLLER_H
