#include "mqtt_bridge.h"
#include "json_parse.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>

#ifdef ENABLE_MQTT_PAHO
#include <mqtt/connect_options.h>
#include <mqtt/message.h>
#endif

namespace carla_bridge {

#ifdef ENABLE_MQTT_PAHO

MqttBridge::MqttBridge(const std::string& brokerHost, int brokerPort, const std::string& vin)
    : m_brokerHost(brokerHost), m_brokerPort(brokerPort), m_vin(vin) {
  std::string address = m_brokerHost + ":" + std::to_string(m_brokerPort);
  m_client = std::make_unique<mqtt::async_client>(address, "carla-bridge-cpp");
  m_client->set_message_callback([this](mqtt::const_message_ptr msg) {
    onMessage(msg->get_topic(), msg->to_string());
  });
}

MqttBridge::~MqttBridge() {
  disconnect();
}

bool MqttBridge::connect() {
  try {
    mqtt::connect_options opts;
    opts.set_clean_session(true);
    opts.set_keep_alive_interval(60);
    opts.set_connect_timeout(10);
    std::cout << "[MQTT] 连接 broker=" << m_brokerHost << ":" << m_brokerPort << " ..." << std::endl;
    m_client->connect(opts)->wait();
    std::cout << "[MQTT] 环节: TCP 连接成功 broker=" << m_brokerHost << ":" << m_brokerPort << std::endl;
    m_client->subscribe(m_controlTopic, 1)->wait();
    std::cout << "[MQTT] 环节: 已订阅 topic=" << m_controlTopic << " 本桥VIN=" << m_vin << "，等待 start_stream/remote_control/drive 消息" << std::endl;
    m_connected = true;
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[MQTT] 连接失败: " << e.what() << std::endl;
    return false;
  }
}

void MqttBridge::disconnect() {
  if (!m_connected.exchange(false)) return;
  try {
    m_client->disconnect()->wait();
  } catch (...) {}
}

void MqttBridge::getState(ControlState& out) const {
  std::lock_guard<std::mutex> lock(m_stateMutex);
  out = m_state;
}

void MqttBridge::setState(const ControlState& s) {
  std::lock_guard<std::mutex> lock(m_stateMutex);
  m_state = s;
}

void MqttBridge::onMessage(const std::string& topic, const std::string& payload) {
  std::cout << "[Control] 收到原始消息 topic=" << topic << " size=" << payload.size()
            << " payload(前120字符)=" << (payload.size() > 120 ? payload.substr(0, 120) + "..." : payload) << std::endl;
  ControlMessage msg;
  if (!parseControlMessage(payload, msg)) {
    std::cerr << "[Control] ✗ 解析失败，已丢弃 payload(前200字符)="
              << (payload.size() > 200 ? payload.substr(0, 200) + "..." : payload) << std::endl;
    return;
  }
  std::string msgVin = msg.vin;
  bool vinOk = msgVin.empty() || msgVin == m_vin;
  
  if (msg.type == "drive" || msg.type == "remote_control" || msg.type == "speed" || msg.type == "gear") {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::cout << "[Control][IN] type=" << msg.type << " seq=" << msg.seq 
              << " ts_ms=" << msg.timestampMs << " delay=" 
              << (msg.timestampMs > 0 ? (now_ms - msg.timestampMs) : 0) << "ms" << std::endl;
  }

  if (msg.type == "start_stream") {
    std::cout << "[Control] 环节: 收到 type=start_stream 消息vin=" << (msgVin.empty() ? "(空)" : msgVin) << " 本桥VIN=" << m_vin << " vin_ok=" << vinOk << std::endl;
    if (vinOk) {
      std::lock_guard<std::mutex> lock(m_stateMutex);
      m_state.streaming = true;
      std::cout << "[Control] 环节: 已置 streaming=true（VIN 匹配），主循环下一轮将调用 setStreaming(true) 启动推流" << std::endl;
    } else {
      std::cout << "[Control] 环节: ✗ 忽略 start_stream（VIN 不匹配）" << std::endl;
    }
    return;
  }
  if (msg.type == "stop_stream") {
    std::cout << "[Control] 环节: 收到 type=stop_stream vin_ok=" << vinOk << std::endl;
    if (vinOk) {
      std::lock_guard<std::mutex> lock(m_stateMutex);
      m_state.streaming = false;
      std::cout << "[Control] 环节: 已置 streaming=false，主循环将停止推流" << std::endl;
    }
    return;
  }
  if (msg.type == "speed") {
    if (vinOk && msg.ui_speed_kmh >= 0.0) {
      std::lock_guard<std::mutex> lock(m_stateMutex);
      m_state.ui_speed_kmh = std::max(0.0, std::min(100.0, msg.ui_speed_kmh));
      std::cout << "[Control] 环节: 收到 type=speed ui_speed_kmh=" << m_state.ui_speed_kmh << std::endl;
      if (m_controlCb) {
        try {
          m_controlCb("speed", m_state);
        } catch (const std::exception& e) {
          std::cerr << "[MQTT][Control] m_controlCb exception: " << e.what() << std::endl;
        }
      }
    }
    return;
  }
  if (msg.type == "remote_control") {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    bool oldVal = m_state.remote_enabled;
    m_state.remote_enabled = msg.enable;
    // ★ 精确定位：便于 docker logs carla-server 2>&1 | grep REMOTE_RECV
    std::cout << "[REMOTE_RECV] 已处理 remote_control 解析得到 enable=" << (msg.enable ? "true" : "false")
              << " 已写入 m_state.remote_enabled 并即将回调 applyControl" << std::endl;
    std::cout << "[Control] remote_control enable=" << (msg.enable ? "true" : "false")
              << " (原值=" << (oldVal ? "true" : "false") << " -> 已更新)" << std::endl;
    if (m_controlCb) {
      try {
        m_controlCb("remote_control", m_state);
      } catch (const std::exception& e) {
        std::cerr << "[MQTT][Control] m_controlCb exception: " << e.what() << std::endl;
      }
    }
    publishRemoteControlAck(m_state.remote_enabled);
    return;
  }
  if (msg.type == "drive" || (msg.steering != 0 || msg.throttle != 0 || msg.brake != 0 || msg.gear != 1)) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_state.steering = std::max(-1.0, std::min(1.0, msg.steering));
    m_state.throttle = std::max(0.0, std::min(1.0, msg.throttle));
    m_state.brake = std::max(0.0, std::min(1.0, msg.brake));
    if (msg.gear >= -1 && msg.gear <= 2) m_state.gear = msg.gear;
    if (msg.type == "drive") {
      static auto s_lastDriveLog = std::chrono::steady_clock::time_point{};
      const auto now = std::chrono::steady_clock::now();
      if (s_lastDriveLog.time_since_epoch().count() == 0 ||
          std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastDriveLog).count() >= 500) {
        s_lastDriveLog = now;
        std::cout << "[Bridge][Control][DRIVE_500ms] steer=" << m_state.steering << " thr=" << m_state.throttle
                  << " brk=" << m_state.brake << " gear=" << m_state.gear
                  << " ui_cmd_kmh=" << m_state.ui_speed_kmh << " remote=" << (m_state.remote_enabled ? "1" : "0")
                  << " stream=" << (m_state.streaming ? "1" : "0") << std::endl;
      }
      
      // ★ 关键修复：立即触发回调应用控制
      if (m_controlCb) {
        try {
          m_controlCb("drive", m_state);
        } catch (const std::exception& e) {
          std::cerr << "[MQTT][Control] m_controlCb (drive) exception: " << e.what() << std::endl;
        }
      }
    }
  }
}

void MqttBridge::publishRemoteControlAck(bool remoteEnabled) {
  if (!m_client || !m_connected.load()) return;
  auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const std::string mode = remoteEnabled ? "远驾" : "自驾";
  std::ostringstream json;
  json << "{\"type\":\"remote_control_ack\""
       << ",\"schemaVersion\":\"1.2.0\""
       << ",\"timestamp\":" << ts
       << ",\"vin\":\"" << m_vin << "\""
       << ",\"remote_control_enabled\":" << (remoteEnabled ? "true" : "false")
       << ",\"driving_mode\":\"" << mode << "\"}";
  try {
    mqtt::message_ptr msg = mqtt::make_message(m_statusTopic, json.str());
    msg->set_qos(1);
    m_client->publish(msg);
  } catch (const std::exception& e) {
    std::cerr << "[MQTT] publish remote_control_ack 失败: " << e.what() << std::endl;
  }
}

void MqttBridge::publishStatus(double speedKmh, int gear, double steering, double throttle, double brake,
                              bool remoteEnabled, bool streaming, const std::string& drivingMode) {
  if (!m_client || !m_connected.load()) return;
  auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  std::ostringstream json;
  json << "{\"type\":\"vehicle_status\""
       << ",\"schemaVersion\":\"1.2.0\""
       << ",\"vin\":\"" << m_vin << "\""
       << ",\"timestamp\":" << ts
       << ",\"speed\":" << std::fixed << std::setprecision(6) << speedKmh
       << ",\"battery\":99.9"
       << ",\"brake\":" << brake
       << ",\"throttle\":" << throttle
       << ",\"steering\":" << steering
       << ",\"gear\":" << gear
       << ",\"odometer\":0.0"
       << ",\"temperature\":25"
       << ",\"voltage\":48"
       << ",\"remote_control_enabled\":" << (remoteEnabled ? "true" : "false")
       << ",\"driving_mode\":\"" << drivingMode << "\""
       << ",\"streaming\":" << (streaming ? "true" : "false") << "}";
  try {
    mqtt::message_ptr msg = mqtt::make_message(m_statusTopic, json.str());
    msg->set_qos(0);
    m_client->publish(msg);
  } catch (const std::exception& e) {
    std::cerr << "[MQTT] publish status 失败: " << e.what() << std::endl;
  }
}

#else

MqttBridge::MqttBridge(const std::string&, int, const std::string& vin) : m_vin(vin) {}
MqttBridge::~MqttBridge() {}
bool MqttBridge::connect() { return false; }
void MqttBridge::disconnect() {}
void MqttBridge::getState(ControlState& out) const { (void)out; }
void MqttBridge::setState(const ControlState&) {}
void MqttBridge::onMessage(const std::string&, const std::string&) {}
void MqttBridge::publishRemoteControlAck(bool) {}
void MqttBridge::publishStatus(double, int, double, double, double, bool, bool, const std::string&) {}

#endif

}  // namespace carla_bridge
