#ifndef CARLA_BRIDGE_MQTT_BRIDGE_H
#define CARLA_BRIDGE_MQTT_BRIDGE_H

#include <string>
#include <functional>
#include <mutex>
#include <atomic>

#ifdef ENABLE_MQTT_PAHO
#include <mqtt/async_client.h>
#endif

namespace carla_bridge {

struct ControlState {
  double steering = 0.0;
  double throttle = 0.0;
  double brake = 0.0;
  int gear = 1;
  /** 客户端 UI / 键盘目标车速 (km/h)，来自 type=speed 的 MQTT 包；无 LibCarla 时由桥内积分器跟踪 */
  double ui_speed_kmh = 0.0;
  bool remote_enabled = false;
  bool streaming = false;
};

using ControlCallback = std::function<void(const std::string& type, const ControlState& state)>;

class MqttBridge {
 public:
  MqttBridge(const std::string& brokerHost, int brokerPort, const std::string& vin);
  ~MqttBridge();

  bool connect();
  void disconnect();
  bool isConnected() const { return m_connected.load(); }

  void setControlCallback(ControlCallback cb) { m_controlCb = std::move(cb); }
  void getState(ControlState& out) const;
  void setState(const ControlState& s);

  void publishStatus(double speedKmh, int gear, double steering, double throttle, double brake,
                     bool remoteEnabled, bool streaming, const std::string& drivingMode);

 private:
  void onMessage(const std::string& topic, const std::string& payload);
  void publishRemoteControlAck(bool remoteEnabled);

  std::string m_brokerHost;
  int m_brokerPort;
  std::string m_vin;
  ControlCallback m_controlCb;
  mutable std::mutex m_stateMutex;
  ControlState m_state;
  std::atomic<bool> m_connected{false};

#ifdef ENABLE_MQTT_PAHO
  std::unique_ptr<mqtt::async_client> m_client;
  std::string m_controlTopic = "vehicle/control";
  std::string m_statusTopic = "vehicle/status";
#endif
};

}  // namespace carla_bridge

#endif
