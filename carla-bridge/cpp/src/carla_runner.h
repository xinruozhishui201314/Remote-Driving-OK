#ifndef CARLA_BRIDGE_CARLA_RUNNER_H
#define CARLA_BRIDGE_CARLA_RUNNER_H

#include "mqtt_bridge.h"
#include <string>
#include <memory>
#include <vector>
#include <atomic>

namespace carla_bridge {

#ifdef ENABLE_LIBCARLA
struct CarlaLibImpl;
#endif

class CarlaRunner {
 public:
  // vin: 车辆 VIN，用于构造多车隔离流名 {vin}_cam_front 等；空字符串时退化为无前缀（兼容单车测试）
  CarlaRunner(const std::string& host, uint16_t port, const std::string& mapName,
              const std::string& zlmHost, int zlmRtmpPort, const std::string& zlmApp,
              int camWidth, int camHeight, int camFps,
              const std::string& vin = "");
  ~CarlaRunner();

  bool start();
  void stop();
  bool isRunning() const { return m_running.load(); }
  void setStreaming(bool on);
  void applyControl(const ControlState& state);
  void getVelocity(double& speedKmh) const;
  void getCurrentState(ControlState& out) const;
  /** 无 LibCarla 时：按 remote/ui_speed/throttle/brake 积分出用于 vehicle/status 的演示车速 */
  void integrateSpeedStep(double dtSec);

 private:
  void startPushers();
  void stopPushers();

  std::string m_host;
  uint16_t m_port;
  std::string m_mapName;
  std::string m_zlmHost;
  int m_zlmRtmpPort;
  std::string m_zlmApp;
  std::string m_vin;
  int m_camWidth, m_camHeight, m_camFps;
  std::atomic<bool> m_running{false};
  std::atomic<bool> m_streaming{false};
  mutable std::mutex m_controlMutex;
  ControlState m_controlState;
  double m_stubSpeedKmh = 0.0;
  std::vector<std::unique_ptr<class RtmpPusher>> m_pushers;
  static const std::vector<std::string> kStreamIds;
#ifdef ENABLE_LIBCARLA
  mutable std::mutex m_carlaMutex;
  std::unique_ptr<CarlaLibImpl> m_lib;
#endif
};

}  // namespace carla_bridge

#endif
