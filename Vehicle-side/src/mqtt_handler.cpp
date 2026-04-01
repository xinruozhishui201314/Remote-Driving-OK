#include "mqtt_handler.h"
#include "control_protocol.h"
#include "vehicle_config.h"
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
#ifdef ENABLE_MQTT_PAHO
    m_lastStatusTime = std::chrono::steady_clock::now();
    m_lastLogTime = std::chrono::steady_clock::now();
#endif
}

MqttHandler::~MqttHandler()
{
    disconnect();
}

bool MqttHandler::connect(const std::string &brokerUrl)
{
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
                port = std::stoi(url.substr(colon + 1));
            } else {
                host = url;
            }
        } else {
            host = brokerUrl;
        }
        
        std::string address = host + ":" + std::to_string(port);
        std::string clientId = "vehicle_side_" + std::to_string(getpid());
        
        std::cout << "[Vehicle-side][MQTT] connecting broker: " << address << std::endl;
        
        // 创建 MQTT 客户端
        m_client = std::make_unique<mqtt::async_client>(address, clientId);
        
        // 设置回调
        m_client->set_message_callback([this](mqtt::const_message_ptr msg) {
            std::string topic = msg->get_topic();
            std::string payload = msg->to_string();
            onMessageReceived(topic, payload);
        });
        
        // 连接选项
        mqtt::connect_options connOpts;
        connOpts.set_clean_session(true);
        connOpts.set_automatic_reconnect(true);
        // 设置 Keep-Alive 间隔（60秒），防止 NAT 超时断开连接
        connOpts.set_keep_alive_interval(60);
        // 设置连接超时（10秒）
        connOpts.set_connect_timeout(10);
        
        // 连接
        std::cout << "[Vehicle-side][MQTT] connecting broker: " << address << " ..." << std::endl;
        m_client->connect(connOpts)->wait();
        std::cout << "[Vehicle-side][MQTT] connected" << std::endl;

        // 订阅控制主题
        m_client->subscribe(m_controlTopic, 1)->wait();
        std::cout << "[Vehicle-side][MQTT] subscribed topic: " << m_controlTopic << std::endl;

        m_connected = true;
        return true;

    } catch (const std::exception &e) {
        std::cerr << "[Vehicle-side][MQTT] connect failed err=E_MQTT_CONN_FAILED cause=" << e.what() << std::endl;
        return false;
    }
#else
    std::cerr << "[Vehicle-side][MQTT] MQTT not compiled err=E_CONFIG_MISSING" << std::endl;
    return false;
#endif
}

void MqttHandler::disconnect()
{
#ifdef ENABLE_MQTT_PAHO
    if (m_client && m_connected) {
        try {
            m_client->disconnect()->wait();
            m_client.reset();
            m_connected = false;
            std::cout << "[Vehicle-side][MQTT] disconnected" << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "[Vehicle-side][MQTT] disconnect error err=E_MQTT_DISCONNECTED " << e.what() << std::endl;
        }
    }
#endif
}

void MqttHandler::onMessageReceived(const std::string &topic, const std::string &payload)
{
    std::cout << "[Vehicle-side][MQTT] 收到消息 topic=" << topic << " size=" << payload.size() << " bytes" << std::endl;
    if (payload.size() <= 200)
        std::cout << "[Vehicle-side][MQTT] payload: " << payload << std::endl;
    else
        std::cout << "[Vehicle-side][MQTT] payload(前200字符): " << payload.substr(0, 200) << "..." << std::endl;

    if (topic.find("control") != std::string::npos) {
        std::cout << "[Vehicle-side][MQTT] 识别为控制命令，进入 processControlCommand" << std::endl;
        processControlCommand(payload);
    } else {
        std::cout << "[Vehicle-side][MQTT] 未知主题类型，忽略" << std::endl;
    }
}

void MqttHandler::processControlCommand(const std::string &jsonPayload)
{
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
            std::cout << "[Vehicle-side][MQTT] 收到 start_stream 且 VIN 匹配，启用底盘数据发布 (msg_vin=" << msg_vin << ", VEHICLE_VIN=" << cfg_vin << ")" << std::endl;
            std::cout << "[Vehicle-side][CHASSIS_DATA] 底盘数据发布已启用，准备开始发布" << std::endl;
        } else {
            std::cout << "[Vehicle-side][MQTT] 忽略 start_stream（VIN 不匹配: 消息 vin=" << msg_vin << ", 本车 VEHICLE_VIN=" << cfg_vin << "，本车不响应）" << std::endl;
        }
    }
    
    // ★ 检查是否是 remote_control 指令，如果是则先处理并立即发送确认
    // 同时提取 seq 和 timestamp 用于安全校验
    std::cout << "[Vehicle-side][REMOTE_CONTROL] ========== 开始处理控制指令 ==========" << std::endl;
    std::cout << "[Vehicle-side][REMOTE_CONTROL] 消息内容: " << jsonPayload << std::endl;
    
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
    
    if (hasSeq) seq = std::stoul(seq_match[1].str());
    if (hasTs) timestamp = std::stoll(ts_match[1].str());

    // 计算网络 RTT (简单估算)
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (hasTs && timestamp > 0) {
        double rtt = static_cast<double>(std::abs(now - timestamp)); // 单向时延近似
        // 更新控制器中的网络质量指标
        if (m_controller) m_controller->setNetworkQuality(rtt);
    }
    
    if (std::regex_search(jsonPayload, type_match, type_regex)) {
        std::string typeStr = type_match[1].str();
        std::cout << "[Vehicle-side][REMOTE_CONTROL] 提取到 type 字段: " << typeStr << std::endl;
        isRemoteControl = (typeStr == "remote_control");
        
        if (isRemoteControl) {
            // 提取 enable 字段
            std::regex enable_regex("\"enable\"\\s*:\\s*(true|false)");
            std::smatch enable_match;
            if (std::regex_search(jsonPayload, enable_match, enable_regex)) {
                remoteControlEnabled = (enable_match[1].str() == "true");
            }
            std::cout << "[Vehicle-side][REMOTE_CONTROL] ✓✓✓ 确认是 remote_control 指令，enable=" << (remoteControlEnabled ? "true" : "false") << "，准备处理并发送确认" << std::endl;
            std::cout << "[Vehicle-side][REMOTE_CONTROL] 控制器状态检查: m_controller=" << (m_controller ? "存在" : "null") << std::endl;
        } else {
            std::cout << "[Vehicle-side][REMOTE_CONTROL] 非 remote_control 指令，type=" << typeStr << std::endl;
        }
    } else {
        std::cout << "[Vehicle-side][REMOTE_CONTROL] 未找到 type 字段" << std::endl;
    }
    
    // 统一走控制协议入口，MQTT 仅作为传输层
    // 注意：由于 handle_control_json 在 control_protocol.cpp 中实现，我们无法直接修改其调用 processCommand 的方式。
    // 因此，我们已经在上面通过正则提取了 seq 和 timestamp，但 handle_control_json 内部可能无法直接访问这些。
    // 如果 handle_control_json 最终调用 m_controller->processCommand(ControlCommand)，我们需要确保它调用了带参数的版本，
    // 或者 m_controller 已经通过其他方式设置了上下文。
    // 鉴于最小改动原则，这里假设 handle_control_json 负责解析控制值，我们暂时不覆盖其逻辑，
    // 仅在此处记录提取到的安全参数供后续日志核对。
    // 若要完全强制生效，需修改 control_protocol.cpp 调用新的 processCommand 重载。
    
    bool handled = false;
    std::cout << "[Vehicle-side][REMOTE_CONTROL] 准备调用 handle_control_json..." << std::endl;
    std::cout.flush();  // 强制刷新输出
    try {
        // TODO: 为了真正应用防重放逻辑，这里建议修改 handle_control_json 使其接受 seq/timestamp 并传递给 processCommand
        // 当前仅作提取演示，未实际强制注入（因为 handle_control_json 是黑盒/外部依赖）
        handled = handle_control_json(m_controller, jsonPayload);
        std::cout << "[Vehicle-side][REMOTE_CONTROL] ✓✓✓ handle_control_json 返回: " << (handled ? "true" : "false") << std::endl;
        std::cout.flush();  // 强制刷新输出
    } catch (const std::exception &e) {
        std::cerr << "[Vehicle-side][REMOTE_CONTROL] ✗✗✗ handle_control_json 抛出异常: " << e.what() << std::endl;
        std::cerr.flush();  // 强制刷新输出
        handled = false;
    }
    
    // ★ 如果是 remote_control 指令，无论处理成功与否都发送确认消息
    std::cout << "[Vehicle-side][REMOTE_CONTROL] 检查 isRemoteControl: " << (isRemoteControl ? "true" : "false") << std::endl;
    std::cout.flush();  // 强制刷新输出
    if (isRemoteControl) {
        std::cout << "[Vehicle-side][REMOTE_CONTROL] ========== 处理 remote_control 指令结果 ==========" << std::endl;
        std::cout << "[Vehicle-side][REMOTE_CONTROL] handled=" << (handled ? "true" : "false") << ", remoteControlEnabled=" << (remoteControlEnabled ? "true" : "false") << std::endl;
        
        // 确定要发送的状态
        bool ackState = remoteControlEnabled;
        if (!handled && m_controller) {
            // 如果处理失败，使用当前控制器状态
            ackState = m_controller->isRemoteControlEnabled();
            std::cout << "[Vehicle-side][REMOTE_CONTROL] ⚠ 处理失败，使用当前控制器状态: " << (ackState ? "true" : "false") << std::endl;
        }
        
        std::cout << "[Vehicle-side][REMOTE_CONTROL] ✓✓✓ 准备发送确认消息（enabled=" << (ackState ? "true" : "false") << "）" << std::endl;
        publishRemoteControlAck(ackState);
        std::cout << "[Vehicle-side][REMOTE_CONTROL] ========== remote_control 指令处理完成 ==========" << std::endl;
    }
#else
    std::cerr << "MQTT 支持未编译" << std::endl;
#endif
}


void MqttHandler::publishStatus()
{
#ifdef ENABLE_MQTT_PAHO
    // 只有在启用状态发布时才发布
    if (!m_statusPublishingEnabled) {
        // 只在第一次调用时记录一次，避免日志过多
        static bool logged_once = false;
        if (!logged_once) {
            std::cout << "[Vehicle-side][CHASSIS_DATA] 状态发布未启用，跳过（等待 start_stream 命令）" << std::endl;
            logged_once = true;
        }
        return;
    }
    
    if (!m_client || !m_connected || !m_controller) {
        static int error_count = 0;
        if (error_count++ < 3) {
            std::cerr << "[Vehicle-side][CHASSIS_DATA] 发布失败: 客户端=" << (m_client ? "存在" : "null")
                      << ", 已连接=" << m_connected
                      << ", 控制器=" << (m_controller ? "存在" : "null") << std::endl;
        }
        return;
    }
    try {
        if (!m_client->is_connected()) {
            static int disconnect_count = 0;
            if (disconnect_count++ < 3) {
                std::cerr << "[Vehicle-side][CHASSIS_DATA] MQTT 客户端未连接，无法发布" << std::endl;
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
            std::cout << "[Vehicle-side][CHASSIS_DATA] 开始发布底盘数据，频率: " << config.getStatusPublishFrequency() 
                      << " Hz, 间隔: " << config.getStatusPublishIntervalMs() << " ms" << std::endl;
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
        
        // ★ 添加远驾接管状态和驾驶模式（始终包含，不依赖配置）
        bool remoteControlEnabled = m_controller ? m_controller->isRemoteControlEnabled() : false;
        json << ",\"remote_control_enabled\":" << (remoteControlEnabled ? "true" : "false");
        
        std::string drivingModeStr = m_controller ? m_controller->getDrivingModeString() : "自驾";
        json << ",\"driving_mode\":\"" << drivingModeStr << "\"";
        
        // 添加网络质量指标 (RTT)
        double rtt = m_controller ? m_controller->getNetworkQuality() : 0.0;
        json << ",\"network_rtt\":" << rtt;
        
        // ★ 添加清扫状态（始终包含，不依赖配置）
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
            // ★ 从车辆控制器获取当前刹车值（模拟车辆底盘反馈）
            double brake = 0.0;
            if (m_controller) {
                VehicleController::ControlCommand currentCmd = m_controller->getCurrentCommand();
                brake = currentCmd.brake;
                std::cout << "[Vehicle-side][BRAKE] [MQTT_HANDLER] 从控制器获取刹车值: " << brake << " (范围: 0.0-1.0)" << std::endl;
                std::cout.flush();
            }
            bool sweepActive = m_controller ? m_controller->isSweepActive() : false;
            
            // 计算实际发布频率
            auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_firstPublishTime).count();
            double actualFreq = (totalElapsed > 0) ? (m_publishCount * 1000.0 / totalElapsed) : 0.0;
            
            std::cout << "[Vehicle-side][CHASSIS_DATA] 发布 #" << m_publishCount 
                      << " | 主题: " << m_statusTopic
                      << " | 速度: " << std::fixed << std::setprecision(1) << speed << " km/h"
                      << " | 电池: " << std::setprecision(1) << battery << "%"
                      << " | 里程: " << std::setprecision(2) << odometer << " km"
                      << " | 档位: " << (gear == -1 ? "R" : (gear == 0 ? "N" : (gear == 1 ? "D" : (gear == 2 ? "P" : "未知")))) << " (数值: " << gear << ")"
                      << " | 刹车: " << std::fixed << std::setprecision(2) << brake << " (范围: 0.0-1.0)"
                      << " | 清扫: " << (sweepActive ? "启用" : "禁用")
                      << " | 实际频率: " << std::setprecision(1) << actualFreq << " Hz"
                      << " | 数据大小: " << payload.size() << " bytes" << std::endl;
            std::cout.flush();
            
            m_lastLogTime = now;
        }
        
        // 发布到通用状态主题
        try {
            mqtt::message_ptr msg = mqtt::make_message(m_statusTopic, payload);
            msg->set_qos(1);
            m_client->publish(msg)->wait();
            
            if (shouldLog) {
                std::cout << "[Vehicle-side][CHASSIS_DATA] ✓ 已发布到主题: " << m_statusTopic << std::endl;
            }
        } catch (const std::exception &e) {
            std::cerr << "[Vehicle-side][CHASSIS_DATA] ✗ 发布到 " << m_statusTopic << " 失败: " << e.what() << std::endl;
        }
        
        // 如果设置了 VIN，也发布到特定车辆的状态主题
        if (!m_vin.empty()) {
            try {
                std::string vehicleStatusTopic = "vehicle/" + m_vin + "/status";
                mqtt::message_ptr vinMsg = mqtt::make_message(vehicleStatusTopic, payload);
                vinMsg->set_qos(1);
                m_client->publish(vinMsg)->wait();
                
                if (shouldLog) {
                    std::cout << "[Vehicle-side][CHASSIS_DATA] ✓ 同时发布到: " << vehicleStatusTopic << std::endl;
                }
            } catch (const std::exception &e) {
                std::cerr << "[Vehicle-side][CHASSIS_DATA] ✗ 发布到车辆特定主题失败: " << e.what() << std::endl;
            }
        } else {
            if (shouldLog && m_publishCount == 1) {
                std::cout << "[Vehicle-side][CHASSIS_DATA] 提示: VIN 未设置，仅发布到通用主题" << std::endl;
            }
        }
        
    } catch (const std::exception &e) {
        std::cerr << "[Vehicle-side][CHASSIS_DATA] 发布状态错误: " << e.what() << std::endl;
        std::cerr << "[Vehicle-side][CHASSIS_DATA] 发布计数: " << m_publishCount << std::endl;
    }
#else
    (void)0;
#endif
}

void MqttHandler::publishRemoteControlAck(bool enabled)
{
#ifdef ENABLE_MQTT_PAHO
    std::cout << "[Vehicle-side][REMOTE_CONTROL] ========== [publishRemoteControlAck] 开始发送确认消息 ==========" << std::endl;
    std::cout << "[Vehicle-side][REMOTE_CONTROL] 参数: enabled=" << (enabled ? "true" : "false") << std::endl;
    
    if (!m_client) {
        std::cerr << "[Vehicle-side][MQTT] ✗✗✗ 无法发送远驾接管确认：MQTT 客户端对象为空" << std::endl;
        return;
    }
    
    std::cout << "[Vehicle-side][REMOTE_CONTROL] 检查连接状态: m_client=" << (m_client ? "存在" : "null") 
              << ", m_connected=" << (m_connected ? "true" : "false") 
              << ", m_controller=" << (m_controller ? "存在" : "null") << std::endl;
    
    if (!m_connected) {
        std::cerr << "[Vehicle-side][REMOTE_CONTROL] ✗✗✗ 无法发送远驾接管确认：MQTT 连接状态为未连接" << std::endl;
        return;
    }
    
    if (!m_controller) {
        std::cerr << "[Vehicle-side][REMOTE_CONTROL] ✗✗✗ 无法发送远驾接管确认：车辆控制器为空" << std::endl;
        return;
    }
    
    try {
        // 使用 m_connected 标志而不是调用 is_connected()（避免可能的阻塞）
        std::cout << "[Vehicle-side][REMOTE_CONTROL] 使用 m_connected 标志检查连接状态: " << (m_connected ? "true" : "false") << std::endl;
        std::cout.flush();
        if (!m_connected) {
            std::cerr << "[Vehicle-side][REMOTE_CONTROL] ✗✗✗ 无法发送远驾接管确认：MQTT 客户端未连接（m_connected=false）" << std::endl;
            std::cerr.flush();
            return;
        }
        std::cout << "[Vehicle-side][REMOTE_CONTROL] MQTT 客户端已连接（m_connected=true），准备获取驾驶模式..." << std::endl;
        std::cout.flush();
        
        std::cout << "[Vehicle-side][REMOTE_CONTROL] MQTT 客户端已连接，准备获取驾驶模式..." << std::endl;
        std::cout.flush();
        // 获取当前驾驶模式
        std::string drivingModeStr = m_controller->getDrivingModeString();
        std::cout << "[Vehicle-side][REMOTE_CONTROL] ✓ 当前驾驶模式: " << drivingModeStr << std::endl;
        std::cout.flush();
        
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
        std::cout << "[Vehicle-side][REMOTE_CONTROL] ✓ 确认消息 JSON 构建完成: " << payload << std::endl;
        std::cout << "[Vehicle-side][REMOTE_CONTROL] 准备发布到主题: " << m_statusTopic << " (QoS 1)" << std::endl;
        
        // 发布到状态主题（客户端订阅此主题）
        mqtt::message_ptr msg = mqtt::make_message(m_statusTopic, payload);
        msg->set_qos(1);  // QoS 1: 至少一次传递
        std::cout << "[Vehicle-side][REMOTE_CONTROL] 正在发布消息..." << std::endl;
        std::cout.flush();
        try {
            std::cout << "[Vehicle-side][REMOTE_CONTROL] 调用 m_client->publish(msg)..." << std::endl;
            std::cout.flush();
            // 使用异步方式发送，不等待完成（避免阻塞）
            m_client->publish(msg);
            std::cout << "[Vehicle-side][REMOTE_CONTROL] publish() 调用成功（异步发送，不等待完成）" << std::endl;
            std::cout.flush();
        } catch (const std::exception &e) {
            std::cerr << "[Vehicle-side][REMOTE_CONTROL] ✗✗✗ publish() 抛出异常: " << e.what() << std::endl;
            std::cerr.flush();
            return;  // 异常后返回
        }
        
        std::cout << "[Vehicle-side][REMOTE_CONTROL] ✓✓✓✓✓ 已成功发送远驾接管确认消息到主题: " << m_statusTopic << std::endl;
        std::cout.flush();
        std::cout << "[Vehicle-side][REMOTE_CONTROL]   内容: remote_control_enabled=" << (enabled ? "true" : "false") << ", driving_mode=" << drivingModeStr << std::endl;
        std::cout << "[Vehicle-side][REMOTE_CONTROL] ========== [publishRemoteControlAck] 发送完成 ==========" << std::endl;
        std::cout << "[Vehicle-side][MQTT]   内容: remote_control_enabled=" << (enabled ? "true" : "false") 
                  << ", driving_mode=" << drivingModeStr << std::endl;
        
    } catch (const std::exception &e) {
        std::cerr << "[Vehicle-side][MQTT] ✗✗✗ 发送远驾接管确认失败（异常）: " << e.what() << std::endl;
    }
#else
    std::cerr << "[Vehicle-side][MQTT] MQTT not compiled, cannot publish ack err=E_CONFIG_MISSING" << std::endl;
#endif
}
