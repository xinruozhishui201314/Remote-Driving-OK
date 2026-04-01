#include "mqtt_handler.h"
#include "control_protocol.h"
#include "vehicle_config.h"
#include "common/logger.h"
#include "common/error_code.h"
#include "common/log_macros.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <cmath>
#include <regex>
#ifdef ENABLE_MQTT_PAHO
#include <chrono>
#include <algorithm>
#include <future>
#endif

MqttHandler::MqttHandler(VehicleController *controller)
    : m_controller(controller)
    , m_dataGenerator()
{
    LOG_ENTRY();

#ifdef ENABLE_MQTT_PAHO
    m_lastStatusTime = std::chrono::steady_clock::now();
    m_lastLogTime = std::chrono::steady_clock::now();
#endif

    if (m_controller) {
        LOG_MQTT_INFO("MqttHandler created with controller");
    } else {
        LOG_MQTT_WARN("MqttHandler created with NULL controller");
    }

    LOG_EXIT();
}

MqttHandler::~MqttHandler()
{
    LOG_ENTRY();
    disconnect();
    LOG_EXIT();
}

bool MqttHandler::connect(const std::string &brokerUrl)
{
    LOG_ENTRY();
    LOG_MQTT_INFO("Attempting to connect to MQTT broker: {}", brokerUrl);

#ifdef ENABLE_MQTT_PAHO
    try {
        // 解析 broker URL
        std::string host;
        int port = 1883;

        // 简单解析 mqtt://host:port
        size_t pos = brokerUrl.find("://");
        if (pos != std::string::npos) {
            std::string url = brokerUrl.substr(pos + 3);
            size_t colon = url.find(':');
            if (colon != std::string::npos) {
                host = url.substr(0, colon);
                try {
                    port = std::stoi(url.substr(colon + 1));
                } catch (const std::exception& e) {
                    LOG_MQTT_WARN("Failed to parse port from URL, using default 1883: {}", e.what());
                    port = 1883;
                }
            } else {
                host = url;
            }
        } else {
            host = brokerUrl;
        }

        std::string address = host + ":" + std::to_string(port);
        std::string clientId = "vehicle_side_" + std::to_string(getpid());

        LOG_MQTT_INFO("Parsed broker: address={}, host={}, port={}, clientId={}",
                     address, host, port, clientId);

        // 创建 MQTT 客户端
        m_client = std::make_unique<mqtt::async_client>(address, clientId);

        // 设置消息回调
        m_client->set_message_callback([this](mqtt::const_message_ptr msg) {
            try {
                std::string topic = msg->get_topic();
                std::string payload = msg->to_string();
                LOG_MQTT_TRACE("MQTT message received: topic={}, size={} bytes",
                             topic, payload.size());
                onMessageReceived(topic, payload);
            } catch (const std::exception& e) {
                LOG_MQTT_ERROR_WITH_CODE(SYS_UNEXPECTED_EXCEPTION,
                    "Exception in message callback: {}", e.what());
            } catch (...) {
                LOG_MQTT_ERROR("Unknown exception in message callback");
            }
        });

        // 连接选项
        mqtt::connect_options connOpts;
        connOpts.set_clean_session(true);
        connOpts.set_automatic_reconnect(true);
        // 设置 Keep-Alive 间隔（60秒），防止 NAT 超时断开连接
        connOpts.set_keep_alive_interval(60);
        // 设置连接超时（10秒）
        connOpts.set_connect_timeout(10);

        LOG_MQTT_INFO("Connecting to broker: {} ...", address);

        // 连接
        try {
            m_client->connect(connOpts)->wait();
            LOG_MQTT_INFO("Connected to MQTT broker successfully");
        } catch (const std::exception& e) {
            LOG_MQTT_ERROR_WITH_CODE(MQTT_CONN_FAILED,
                "Failed to connect to broker: {}", e.what());
            LOG_EXIT_WITH_VALUE("false: connection_failed");
            return false;
        }

        // 订阅控制主题
        LOG_MQTT_INFO("Subscribing to topic: {}", m_controlTopic);
        try {
            m_client->subscribe(m_controlTopic, 1)->wait();
            LOG_MQTT_INFO("Subscribed to topic: {} successfully", m_controlTopic);
        } catch (const std::exception& e) {
            LOG_MQTT_ERROR_WITH_CODE(MQTT_SUBSCRIBE_FAILED,
                "Failed to subscribe to topic {}: {}", m_controlTopic, e.what());
            LOG_EXIT_WITH_VALUE("false: subscribe_failed");
            return false;
        }

        m_connected = true;
        LOG_MQTT_INFO("MQTT connection established: connected=true, control_topic={}, status_topic={}",
                     m_controlTopic, m_statusTopic);
        LOG_EXIT_WITH_VALUE("true");
        return true;

    } catch (const std::exception &e) {
        LOG_MQTT_ERROR_WITH_CODE(MQTT_CONN_FAILED,
            "MQTT connect exception: {}", e.what());
        LOG_EXIT_WITH_VALUE("false: exception");
        return false;
    }
#else
    LOG_MQTT_ERROR_WITH_CODE(CFG_ENV_VAR_MISSING,
        "MQTT support not compiled (ENABLE_MQTT_PAHO not defined)");
    LOG_EXIT_WITH_VALUE("false: not_compiled");
    return false;
#endif
}

void MqttHandler::disconnect()
{
    LOG_ENTRY();
    LOG_MQTT_INFO("Disconnecting MQTT...");

#ifdef ENABLE_MQTT_PAHO
    if (m_client && m_connected) {
        try {
            m_client->disconnect()->wait();
            m_client.reset();
            m_connected = false;
            LOG_MQTT_INFO("MQTT disconnected successfully");
        } catch (const std::exception &e) {
            LOG_MQTT_ERROR_WITH_CODE(MQTT_DISCONNECTED,
                "MQTT disconnect error: {}", e.what());
        }
    } else {
        LOG_MQTT_TRACE("MQTT not connected or already disconnected");
    }
#endif

    LOG_EXIT();
}

void MqttHandler::onMessageReceived(const std::string &topic, const std::string &payload)
{
    LOG_MQTT_TRACE("onMessageReceived called: topic={}, size={} bytes", topic, payload.size());

    if (payload.size() <= 200) {
        LOG_MQTT_DEBUG("MQTT payload: {}", payload);
    } else {
        LOG_MQTT_DEBUG("MQTT payload (first 200 chars): {}...", payload.substr(0, 200));
    }

    if (topic.find("control") != std::string::npos) {
        LOG_MQTT_DEBUG("Processing control command from topic: {}", topic);
        processControlCommand(payload);
    } else {
        LOG_MQTT_WARN("Unknown topic type, ignoring: {}", topic);
    }
}

void MqttHandler::processControlCommand(const std::string &jsonPayload)
{
    LOG_ENTRY();
    LOG_MQTT_DEBUG("processControlCommand called, payload length: {} bytes", jsonPayload.length());

#ifdef ENABLE_MQTT_PAHO
    // 检查是否是 start_stream 指令（仅当 VIN 匹配本车时启用状态发布）
    if (jsonPayload.find("\"type\"") != std::string::npos &&
        jsonPayload.find("start_stream") != std::string::npos) {
        std::string msg_vin;
        std::regex vin_regex("\"vin\"\\s*:\\s*\"([^\"]+)\"");
        std::smatch vm;
        if (std::regex_search(jsonPayload, vm, vin_regex))
            msg_vin = vm[1].str();
        const char* cfg_vin_env = std::getenv("VEHICLE_VIN");
        std::string cfg_vin = (cfg_vin_env && cfg_vin_env[0] != '\0') ? cfg_vin_env : "";
        bool vin_ok = cfg_vin.empty() || msg_vin.empty() || msg_vin == cfg_vin;
        if (vin_ok) {
            m_statusPublishingEnabled = true;
            m_publishCount = 0;
            m_lastLogTime = std::chrono::steady_clock::now();
            LOG_MQTT_INFO("Received start_stream command, VIN matched. Enabling chassis data publishing.");
            LOG_MQTT_INFO("  msg_vin={}, cfg_vin={}", msg_vin, cfg_vin);
        } else {
            LOG_SEC_WARN_WITH_CODE(SEC_VIN_MISMATCH,
                "Ignoring start_stream: VIN mismatch (msg_vin={}, cfg_vin={})", msg_vin, cfg_vin);
        }
    }

    // 检查是否是 remote_control 指令，如果是则先处理并立即发送确认
    // 同时提取 seq 和 timestamp 用于安全校验
    LOG_MQTT_DEBUG("Processing control command...");

    // 使用正则表达式提取关键字段
    std::regex type_regex("\"type\"\\s*:\\s*\"([^\"]+)\"");
    std::regex seq_regex("\"seq\"\\s*:\\s*(\\d+)");
    std::regex ts_regex("\"timestamp\"\\s*:\\s*(\\d+)");

    std::smatch type_match, seq_match, ts_match;
    bool isRemoteControl = false;
    bool remoteControlEnabled = false;
    uint32_t seq = 0;
    int64_t timestamp = 0;
    bool hasSeq = std::regex_search(jsonPayload, seq_match, seq_regex);
    bool hasTs = std::regex_search(jsonPayload, ts_match, ts_regex);

    if (hasSeq) {
        try {
            seq = std::stoul(seq_match[1].str());
        } catch (const std::exception& e) {
            LOG_MQTT_WARN("Failed to parse seq: {}", e.what());
        }
    }
    if (hasTs) {
        try {
            timestamp = std::stoll(ts_match[1].str());
        } catch (const std::exception& e) {
            LOG_MQTT_WARN("Failed to parse timestamp: {}", e.what());
        }
    }

    // 计算网络 RTT (简单估算)
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (hasTs && timestamp > 0) {
        double rtt = static_cast<double>(std::abs(now - timestamp)); // 单向时延近似
        // 更新控制器中的网络质量指标
        if (m_controller) {
            m_controller->setNetworkQuality(rtt);
            LOG_NET_TRACE("Network RTT updated: {:.2f}ms", rtt);
        }
    }

    if (std::regex_search(jsonPayload, type_match, type_regex)) {
        std::string typeStr = type_match[1].str();
        LOG_MQTT_DEBUG("Message type: {}", typeStr);
        isRemoteControl = (typeStr == "remote_control");

        if (isRemoteControl) {
            // 提取 enable 字段
            std::regex enable_regex("\"enable\"\\s*:\\s*(true|false)");
            std::smatch enable_match;
            if (std::regex_search(jsonPayload, enable_match, enable_regex)) {
                remoteControlEnabled = (enable_match[1].str() == "true");
            }
            LOG_MQTT_INFO("remote_control command: enable={}", remoteControlEnabled);
        }
    } else {
        LOG_MQTT_WARN("No 'type' field found in message");
    }

    bool handled = false;
    try {
        handled = handle_control_json(m_controller, jsonPayload);
        LOG_MQTT_DEBUG("handle_control_json returned: {}", handled);
    } catch (const std::exception &e) {
        LOG_MQTT_ERROR_WITH_CODE(SYS_UNEXPECTED_EXCEPTION,
            "handle_control_json exception: {}", e.what());
        handled = false;
    }

    // 如果是 remote_control 指令，无论处理成功与否都发送确认消息
    if (isRemoteControl) {
        LOG_MQTT_DEBUG("Processing remote_control ack: handled={}, remoteControlEnabled={}",
                     handled, remoteControlEnabled);

        // 确定要发送的状态
        bool ackState = remoteControlEnabled;
        if (!handled && m_controller) {
            ackState = m_controller->isRemoteControlEnabled();
            LOG_MQTT_WARN("Handling failed, using current controller state: {}", ackState);
        }

        publishRemoteControlAck(ackState);
        LOG_MQTT_INFO("remote_control command processed: ackState={}", ackState);
    }

    LOG_EXIT_WITH_VALUE("handled={}", handled);
#else
    LOG_MQTT_ERROR_WITH_CODE(CFG_ENV_VAR_MISSING,
        "MQTT support not compiled");
    LOG_EXIT_WITH_VALUE("false: not_compiled");
#endif
}


void MqttHandler::publishStatus()
{
    LOG_MQTT_TRACE("publishStatus called");

#ifdef ENABLE_MQTT_PAHO
    // 只有在启用状态发布时才发布
    if (!m_statusPublishingEnabled) {
        // 只在第一次调用时记录一次，避免日志过多
        static bool logged_once = false;
        if (!logged_once) {
            LOG_MQTT_DEBUG("Status publishing not enabled, skipping (waiting for start_stream)");
            logged_once = true;
        }
        return;
    }

    if (!m_client || !m_connected || !m_controller) {
        static int error_count = 0;
        if (error_count++ < 3) {
            LOG_MQTT_ERROR("Status publish failed: client={}, connected={}, controller={}",
                         (m_client ? "exists" : "null"),
                         m_connected,
                         (m_controller ? "exists" : "null"));
        }
        return;
    }

    try {
        if (!m_client->is_connected()) {
            static int disconnect_count = 0;
            if (disconnect_count++ < 3) {
                LOG_MQTT_ERROR("MQTT client not connected, cannot publish");
            }
            return;
        }

        auto cmd = m_controller->getCurrentCommand();

        // 计算时间差
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastStatusTime).count();
        double dt = elapsed / 1000.0;  // 转换为秒
        m_lastStatusTime = now;

        // 获取配置
        auto& config = VehicleConfig::getInstance();

        // 生成所有启用的数据字段
        auto allData = m_dataGenerator.generateAll(cmd, dt);

        // 更新发布统计
        m_publishCount++;
        if (m_publishCount == 1) {
            m_firstPublishTime = now;
            LOG_MQTT_INFO("Chassis data publishing started: freq={} Hz, interval={} ms",
                         config.getStatusPublishFrequency(),
                         config.getStatusPublishIntervalMs());
        }

        // 构建标准 JSON 消息（确保格式正确和兼容性）
        std::ostringstream json;
        json << std::fixed << std::setprecision(6);  // 设置浮点数精度（6位小数，避免科学计数法）
        json << "{";

        bool first = true;

        // 添加时间戳（第一个字段，便于排序）
        json << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        first = false;

        // 如果设置了 VIN，添加到消息中（第二个字段）
        if (!m_vin.empty()) {
            json << ",\"vin\":\"" << m_vin << "\"";
        }

        // 添加所有启用的数据字段（按配置顺序）
        for (const auto& fieldConfig : config.getChassisDataFields()) {
            if (!fieldConfig.enabled) {
                continue;
            }

            auto it = allData.find(fieldConfig.name);
            if (it == allData.end()) {
                continue;
            }

            json << ",\"" << fieldConfig.name << "\":";

            const auto& value = it->second;
            if (value.type == ChassisDataGenerator::DataValue::DOUBLE) {
                // 使用固定格式，避免科学计数法，确保兼容性
                json << value.d;
            } else if (value.type == ChassisDataGenerator::DataValue::INT) {
                json << value.i;
            } else {
                // 字符串需要转义特殊字符（JSON标准转义）
                json << "\"";
                for (char c : value.s) {
                    switch (c) {
                        case '"': json << "\\\""; break;
                        case '\\': json << "\\\\"; break;
                        case '\b': json << "\\b"; break;
                        case '\f': json << "\\f"; break;
                        case '\n': json << "\\n"; break;
                        case '\r': json << "\\r"; break;
                        case '\t': json << "\\t"; break;
                        default:
                            if (static_cast<unsigned char>(c) < 0x20) {
                                // 控制字符使用Unicode转义
                                json << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                                     << static_cast<int>(c) << std::dec;
                            } else {
                                json << c;
                            }
                            break;
                    }
                }
                json << "\"";
            }
        }

        // 添加远驾接管状态和驾驶模式（始终包含，不依赖配置）
        bool remoteControlEnabled = m_controller ? m_controller->isRemoteControlEnabled() : false;
        json << ",\"remote_control_enabled\":" << (remoteControlEnabled ? "true" : "false");

        std::string drivingModeStr = m_controller ? m_controller->getDrivingModeString() : "自驾";
        json << ",\"driving_mode\":\"" << drivingModeStr << "\"";

        // 添加网络质量指标 (RTT)
        double rtt = m_controller ? m_controller->getNetworkQuality() : 0.0;
        json << ",\"network_rtt\":" << rtt;

        // 添加清扫状态（始终包含，不依赖配置）
        bool sweepActive = m_controller ? m_controller->isSweepActive() : false;
        json << ",\"sweep_active\":" << (sweepActive ? "true" : "false");

        json << "}";

        std::string payload = json.str();

        // 日志记录（每50条或每5秒记录一次，避免日志过多）
        auto logElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastLogTime).count();
        bool shouldLog = (m_publishCount % 50 == 0) || (logElapsed >= 5000);

        if (shouldLog) {
            // 提取关键字段用于日志
            double speed = 0.0, battery = 0.0, odometer = 0.0;
            int gear = 0;
            if (allData.find("speed") != allData.end() && allData.at("speed").type == ChassisDataGenerator::DataValue::DOUBLE) {
                speed = allData.at("speed").d;
            }
            if (allData.find("battery") != allData.end() && allData.at("battery").type == ChassisDataGenerator::DataValue::DOUBLE) {
                battery = allData.at("battery").d;
            }
            if (allData.find("odometer") != allData.end() && allData.at("odometer").type == ChassisDataGenerator::DataValue::DOUBLE) {
                odometer = allData.at("odometer").d;
            }
            if (allData.find("gear") != allData.end() && allData.at("gear").type == ChassisDataGenerator::DataValue::INT) {
                gear = allData.at("gear").i;
            }
            // 从车辆控制器获取当前刹车值
            double brake = 0.0;
            if (m_controller) {
                VehicleController::ControlCommand currentCmd = m_controller->getCurrentCommand();
                brake = currentCmd.brake;
            }

            // 计算实际发布频率
            auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_firstPublishTime).count();
            double actualFreq = (totalElapsed > 0) ? (m_publishCount * 1000.0 / totalElapsed) : 0.0;

            LOG_MQTT_INFO("Status #{} published | topic={} | speed={:.1f} km/h | battery={:.1f}% | odometer={:.2f} km | gear={} ({}) | brake={:.2f} | sweep={} | freq={:.1f} Hz | size={} bytes",
                        m_publishCount,
                        m_statusTopic,
                        speed,
                        battery,
                        odometer,
                        gear,
                        (gear == -1 ? "R" : (gear == 0 ? "N" : (gear == 1 ? "D" : (gear == 2 ? "P" : "?")))),
                        brake,
                        (sweepActive ? "on" : "off"),
                        actualFreq,
                        payload.size());

            m_lastLogTime = now;
        }

        // 发布到通用状态主题
        try {
            mqtt::message_ptr msg = mqtt::make_message(m_statusTopic, payload);
            msg->set_qos(1);
            m_client->publish(msg)->wait();

            if (shouldLog) {
                LOG_MQTT_DEBUG("Published to topic: {}", m_statusTopic);
            }
        } catch (const std::exception &e) {
            LOG_MQTT_ERROR_WITH_CODE(MQTT_PUBLISH_FAILED,
                "Failed to publish to {}: {}", m_statusTopic, e.what());
        }

        // 如果设置了 VIN，也发布到特定车辆的状态主题
        if (!m_vin.empty()) {
            try {
                std::string vehicleStatusTopic = "vehicle/" + m_vin + "/status";
                mqtt::message_ptr vinMsg = mqtt::make_message(vehicleStatusTopic, payload);
                vinMsg->set_qos(1);
                m_client->publish(vinMsg)->wait();

                if (shouldLog) {
                    LOG_MQTT_DEBUG("Published to vehicle topic: {}", vehicleStatusTopic);
                }
            } catch (const std::exception &e) {
                LOG_MQTT_ERROR_WITH_CODE(MQTT_PUBLISH_FAILED,
                    "Failed to publish to vehicle topic: {}", e.what());
            }
        } else {
            if (shouldLog && m_publishCount == 1) {
                LOG_MQTT_WARN("VIN not set, only publishing to generic topic");
            }
        }

    } catch (const std::exception &e) {
        LOG_MQTT_ERROR_WITH_CODE(SYS_UNEXPECTED_EXCEPTION,
            "Status publish error: {}", e.what());
    }
#else
    LOG_MQTT_WARN("MQTT PAHO not compiled, publishStatus skipped");
#endif
}

void MqttHandler::publishRemoteControlAck(bool enabled)
{
    LOG_ENTRY();
    LOG_MQTT_DEBUG("publishRemoteControlAck called: enabled={}", enabled);

#ifdef ENABLE_MQTT_PAHO
    if (!m_client) {
        LOG_MQTT_ERROR("Cannot send remote control ack: MQTT client is null");
        LOG_EXIT_WITH_VALUE("false: null_client");
        return;
    }

    if (!m_connected) {
        LOG_MQTT_ERROR("Cannot send remote control ack: not connected");
        LOG_EXIT_WITH_VALUE("false: not_connected");
        return;
    }

    if (!m_controller) {
        LOG_MQTT_ERROR("Cannot send remote control ack: controller is null");
        LOG_EXIT_WITH_VALUE("false: null_controller");
        return;
    }

    try {
        // 获取当前驾驶模式
        std::string drivingModeStr = m_controller->getDrivingModeString();
        LOG_MQTT_DEBUG("Current driving mode: {}", drivingModeStr);

        // 构建确认消息 JSON
        std::ostringstream json;
        json << std::fixed << std::setprecision(6);
        json << "{";
        json << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // 如果设置了 VIN，添加到消息中
        if (!m_vin.empty()) {
            json << ",\"vin\":\"" << m_vin << "\"";
        }

        // 添加远驾接管确认字段
        json << ",\"type\":\"remote_control_ack\"";
        json << ",\"remote_control_enabled\":" << (enabled ? "true" : "false");
        json << ",\"driving_mode\":\"" << drivingModeStr << "\"";
        json << "}";

        std::string payload = json.str();
        LOG_MQTT_DEBUG("ACK payload: {}", payload);

        // 发布到状态主题（客户端订阅此主题）
        mqtt::message_ptr msg = mqtt::make_message(m_statusTopic, payload);
        msg->set_qos(1);  // QoS 1: 至少一次传递

        m_client->publish(msg);
        LOG_MQTT_INFO("Remote control ACK sent: enabled={}, mode={}, topic={}",
                     enabled, drivingModeStr, m_statusTopic);

    } catch (const std::exception &e) {
        LOG_MQTT_ERROR_WITH_CODE(MQTT_PUBLISH_FAILED,
            "Failed to send remote control ack: {}", e.what());
    }

    LOG_EXIT();
#else
    LOG_MQTT_WARN("MQTT PAHO not compiled, publishRemoteControlAck skipped");
    LOG_EXIT();
#endif
}
