#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <string>
#include "vehicle_controller.h"
#include "chassis_data_generator.h"
#include "vehicle_config.h"

#ifdef ENABLE_MQTT_PAHO
#include <mqtt/async_client.h>
#include <mqtt/connect_options.h>
#include <mqtt/message.h>
#include <chrono>
#include <memory>
#endif

/**
 * @brief MQTT 处理器
 * 处理来自客户端的 MQTT 控制指令
 */
class MqttHandler {
public:
    explicit MqttHandler(VehicleController *controller);
    ~MqttHandler();

    bool connect(const std::string &brokerUrl);
    void disconnect();
    bool isConnected() const { return m_connected; }

    /** 发布当前车辆状态到 vehicle/status（供远程驾驶客户端主界面显示） */
    void publishStatus();
    
    /** 立即发布远驾接管确认消息（用于响应 remote_control 指令） */
    void publishRemoteControlAck(bool enabled);
    
    /** 设置是否启用状态发布（接收到 start_stream 后启用） */
    void setStatusPublishingEnabled(bool enabled) { m_statusPublishingEnabled = enabled; }
    
    /** 设置车辆 VIN（用于状态发布） */
    void setVin(const std::string &vin) { m_vin = vin; }

private:
    void onMessageReceived(const std::string &topic, const std::string &payload);
    void processControlCommand(const std::string &jsonPayload);

    VehicleController *m_controller;
    bool m_connected = false;
    bool m_statusPublishingEnabled = false;  // 默认不发布，接收到 start_stream 后启用
    std::string m_vin;  // 车辆 VIN
    ChassisDataGenerator m_dataGenerator;  // 数据生成器
    
#ifdef ENABLE_MQTT_PAHO
    std::unique_ptr<mqtt::async_client> m_client;
    std::string m_controlTopic = "vehicle/control";
    std::string m_statusTopic = "vehicle/status";
    std::chrono::steady_clock::time_point m_lastStatusTime;
    // 日志统计
    uint64_t m_publishCount = 0;  // 发布计数
    std::chrono::steady_clock::time_point m_firstPublishTime;  // 首次发布时间
    std::chrono::steady_clock::time_point m_lastLogTime;  // 上次日志时间
#endif
};

#endif // MQTT_HANDLER_H
