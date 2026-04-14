#include "carla_runner.h"
#include "rtmp_pusher.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>

#ifdef ENABLE_LIBCARLA
#include <carla/client/Client.h>
#include <carla/client/World.h>
#include <carla/client/Vehicle.h>
#include <carla/exception/Exception.h>
#include <carla/geom/Vector3D.h>
#include <carla/rpc/VehicleControl.h>
#include <boost/shared_ptr.hpp>
#endif

namespace carla_bridge {

#ifdef ENABLE_LIBCARLA
struct CarlaLibImpl {
  std::shared_ptr<carla::client::Client> client;
  boost::shared_ptr<carla::client::Vehicle> vehicle;
};

namespace {

double velocityToKmh(const carla::geom::Vector3D& v) {
  return std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y +
                   static_cast<double>(v.z) * v.z) *
         3.6;
}

void gearToCarla(int gear, bool& reverse, bool& hand_brake, int& gear_num, bool& manual) {
  const int g = gear;
  if (g == -1) {
    reverse = true;
    hand_brake = false;
    gear_num = 1;
    manual = false;
  } else if (g == 0) {
    reverse = false;
    hand_brake = false;
    gear_num = 0;
    manual = true;
  } else if (g == 2) {
    reverse = false;
    hand_brake = true;
    gear_num = 1;
    manual = false;
  } else {
    reverse = false;
    hand_brake = false;
    gear_num = 1;
    manual = false;
  }
}

/** 与 carla_bridge.py::_apply_vehicle_control / _gear_to_carla 对齐（无 emergency_stop 字段时略过急停分支） */
void applyControlToCarlaVehicle(const boost::shared_ptr<carla::client::Vehicle>& vehicle, const ControlState& s) {
  double steer = std::clamp(s.steering, -1.0, 1.0);
  double throttle = std::clamp(s.throttle, 0.0, 1.0);
  double brake = std::clamp(s.brake, 0.0, 1.0);
  int gear = s.gear;
  const double target_speed = s.ui_speed_kmh;
  const bool remote_ok = s.remote_enabled;

  if (remote_ok && target_speed > 0.1 && throttle < 0.01) {
    double curr_speed = 0.0;
    try {
      curr_speed = velocityToKmh(vehicle->GetVelocity());
    } catch (...) {
    }
    const double diff = target_speed - curr_speed;
    if (diff > 0) {
      throttle = std::clamp(diff * 0.05, 0.15, 0.8);
      brake = 0.0;
    } else {
      throttle = 0.0;
      brake = std::clamp(-diff * 0.05, 0.0, 0.5);
    }
    if (gear == 0) gear = 1;
  }

  bool reverse = false;
  bool hand_brake = false;
  int gear_num = 1;
  bool manual = false;
  if (!remote_ok) {
    throttle = 0.0;
    brake = 0.5;
    reverse = false;
    hand_brake = false;
    gear_num = 0;
    manual = true;
  } else {
    gearToCarla(gear, reverse, hand_brake, gear_num, manual);
  }

  carla::rpc::VehicleControl ctrl;
  ctrl.steer = static_cast<float>(steer);
  ctrl.throttle = static_cast<float>(throttle);
  ctrl.brake = static_cast<float>(brake);
  ctrl.hand_brake = hand_brake;
  ctrl.reverse = reverse;
  ctrl.manual_gear_shift = manual;
  ctrl.gear = gear_num;
  vehicle->ApplyControl(ctrl);
}

}  // namespace
#endif

const std::vector<std::string> CarlaRunner::kStreamIds = {"cam_front", "cam_rear", "cam_left", "cam_right"};

CarlaRunner::CarlaRunner(const std::string& host, uint16_t port, const std::string& mapName,
                         const std::string& zlmHost, int zlmRtmpPort, const std::string& zlmApp,
                         int camWidth, int camHeight, int camFps,
                         const std::string& vin)
    : m_host(host), m_port(port), m_mapName(mapName),
      m_zlmHost(zlmHost), m_zlmRtmpPort(zlmRtmpPort), m_zlmApp(zlmApp),
      m_vin(vin),
      m_camWidth(camWidth), m_camHeight(camHeight), m_camFps(camFps) {}

CarlaRunner::~CarlaRunner() {
  stop();
}

bool CarlaRunner::start() {
  if (m_running.exchange(true)) return true;
  std::cout << "[CARLA] 配置 host=" << m_host << " port=" << m_port
            << " map=" << (m_mapName.empty() ? "(默认)" : m_mapName)
            << " ZLM=" << m_zlmHost << ":" << m_zlmRtmpPort << " app=" << m_zlmApp << std::endl;
#ifdef ENABLE_LIBCARLA
  std::cout << "[CARLA] ENABLE_LIBCARLA：连接仿真、加载地图(可选)、spawn 车辆并下发 ApplyControl" << std::endl;
  auto local = std::make_unique<CarlaLibImpl>();
  try {
    local->client = std::make_shared<carla::client::Client>(m_host, m_port);
    local->client->SetTimeout(std::chrono::seconds(20));
    auto world = local->client->GetWorld();
    if (!m_mapName.empty()) {
      auto map = world.GetMap();
      const std::string& cur_name = map->GetName();
      if (cur_name.find(m_mapName) == std::string::npos) {
        world = local->client->LoadWorld(m_mapName);
      }
    }
    auto bp_lib = world.GetBlueprintLibrary();
    const char* bp_env = std::getenv("CARLA_VEHICLE_BP");
    std::string bp_filter = (bp_env && bp_env[0]) ? std::string(bp_env) : std::string("vehicle.*");
    auto bps = bp_lib->Filter(bp_filter);
    if (bps->empty()) {
      bps = bp_lib->Filter("vehicle.*");
    }
    if (bps->empty()) {
      std::cerr << "[CARLA] 无匹配车辆蓝图，请检查 CARLA_VEHICLE_BP" << std::endl;
      m_running.store(false);
      return false;
    }
    const auto& blueprint = (*bps)[0];
    auto carla_map = world.GetMap();
    const auto& spawns = carla_map->GetRecommendedSpawnPoints();
    if (spawns.empty()) {
      std::cerr << "[CARLA] 地图无推荐 spawn 点" << std::endl;
      m_running.store(false);
      return false;
    }
    int spawn_idx = 0;
    if (const char* si = std::getenv("CARLA_SPAWN_INDEX")) {
      spawn_idx = std::atoi(si);
    }
    spawn_idx = std::clamp(spawn_idx, 0, static_cast<int>(spawns.size()) - 1);
    const auto& tf = spawns[static_cast<size_t>(spawn_idx)];
    auto actor = world.TrySpawnActor(blueprint, tf);
    if (!actor) {
      std::cerr << "[CARLA] TrySpawnActor 失败（占位被占或碰撞），可换 CARLA_SPAWN_INDEX" << std::endl;
      m_running.store(false);
      return false;
    }
    local->vehicle = boost::static_pointer_cast<carla::client::Vehicle>(actor);
    std::cout << "[CARLA] 已 spawn 车辆 id=" << local->vehicle->GetId() << " type=" << local->vehicle->GetTypeId()
              << " spawn_idx=" << spawn_idx << std::endl;
  } catch (const carla::Exception& e) {
    std::cerr << "[CARLA] LibCarla 异常: " << e.what() << std::endl;
    m_running.store(false);
    return false;
  } catch (const std::exception& e) {
    std::cerr << "[CARLA] 启动异常: " << e.what() << std::endl;
    m_running.store(false);
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(m_carlaMutex);
    m_lib = std::move(local);
  }
#else
  std::cout << "[CARLA] 未链接 LibCarla，仅 MQTT/状态/推流；收到 start_stream 时使用 testsrc 推流" << std::endl;
#endif
  return true;
}

void CarlaRunner::stop() {
  if (!m_running.exchange(false)) return;
  stopPushers();
#ifdef ENABLE_LIBCARLA
  std::unique_ptr<CarlaLibImpl> dead;
  {
    std::lock_guard<std::mutex> lock(m_carlaMutex);
    dead = std::move(m_lib);
  }
  if (dead && dead->vehicle) {
    try {
      dead->vehicle->Destroy();
    } catch (const std::exception& e) {
      std::cerr << "[CARLA] Destroy vehicle: " << e.what() << std::endl;
    }
    dead->vehicle.reset();
  }
#endif
}

void CarlaRunner::setStreaming(bool on) {
  if (m_streaming.exchange(on) == on) return;
  if (on)
    startPushers();
  else
    stopPushers();
}

void CarlaRunner::startPushers() {
  stopPushers();
  std::string base = "rtmp://" + m_zlmHost + ":" + std::to_string(m_zlmRtmpPort) + "/" + m_zlmApp;
  // 多车隔离：流名加 VIN 前缀，格式 {vin}_cam_front；m_vin 为空时不加前缀（兼容单车测试）
  std::string vinPrefix = m_vin.empty() ? "" : (m_vin + "_");
  std::cout << "[CARLA-Bridge][ZLM][Push] startPushers 开始 ZLM=" << m_zlmHost << ":" << m_zlmRtmpPort
            << " app=" << m_zlmApp << " vin=" << (m_vin.empty() ? "(none)" : m_vin) << std::endl;
  std::cout << "[CARLA-Bridge][ZLM][Push] 推流目标 " << base << "/{" << vinPrefix << "cam_front,...}" << std::endl;
  for (size_t i = 0; i < kStreamIds.size(); ++i) {
    const std::string& sid = kStreamIds[i];
    std::string streamName = vinPrefix + sid;
    std::string url = base + "/" + streamName;
    std::cout << "[CARLA-Bridge][ZLM][Push] 启动第 " << (i + 1) << "/4 路 stream=" << streamName << " url=" << url << std::endl;
    auto pusher = std::make_unique<RtmpPusher>(streamName, url, m_camWidth, m_camHeight, m_camFps);
    try {
#ifdef ENABLE_LIBCARLA
      pusher->start();
#else
      pusher->startTestPattern();
#endif
      std::cout << "[CARLA-Bridge][ZLM][Push] 第 " << (i + 1) << "/4 路推流启动成功 stream=" << streamName << std::endl;
    } catch (const std::exception& e) {
      std::cerr << "[CARLA-Bridge][ZLM][Push] 第 " << (i + 1) << "/4 路推流启动失败 stream=" << streamName
                << " err=" << e.what() << " — 继续其余路推流" << std::endl;
    } catch (...) {
      std::cerr << "[CARLA-Bridge][ZLM][Push] 第 " << (i + 1) << "/4 路推流启动失败 stream=" << streamName
                << "（未知异常） — 继续其余路推流" << std::endl;
    }
    m_pushers.push_back(std::move(pusher));
  }
  std::cout << "[CARLA-Bridge][ZLM][Push] 四路推流已启动 app=" << m_zlmApp
            << " vin=" << (m_vin.empty() ? "(none)" : m_vin) << " 共 " << kStreamIds.size() << " 路" << std::endl;
}

void CarlaRunner::stopPushers() {
  for (auto& p : m_pushers) {
    if (p) p->stop();
  }
  m_pushers.clear();
  std::cout << "[CARLA-Bridge][ZLM][Push] 四路推流已停止" << std::endl;
}

void CarlaRunner::applyControl(const ControlState& state) {
  {
    std::lock_guard<std::mutex> lock(m_controlMutex);
    m_controlState = state;
  }
  std::cout << "[CARLA] applyControl 已更新状态 remote_enabled=" << (state.remote_enabled ? "true" : "false")
            << " steering=" << state.steering << " throttle=" << state.throttle << " brake=" << state.brake
            << " ui_speed_kmh=" << state.ui_speed_kmh << std::endl;
#ifdef ENABLE_LIBCARLA
  {
    std::lock_guard<std::mutex> carlaLock(m_carlaMutex);
    if (m_lib && m_lib->vehicle) {
      try {
        applyControlToCarlaVehicle(m_lib->vehicle, state);
      } catch (const std::exception& e) {
        std::cerr << "[CARLA] ApplyControl 失败: " << e.what() << std::endl;
      }
    }
  }
#endif
}

void CarlaRunner::integrateSpeedStep(double dtSec) {
#ifdef ENABLE_LIBCARLA
  (void)dtSec;
  // 车速由仿真 GetVelocity 提供
#else
  if (dtSec <= 0.0 || !std::isfinite(dtSec)) return;
  std::lock_guard<std::mutex> lock(m_controlMutex);
  const ControlState& s = m_controlState;
  if (!s.remote_enabled) {
    m_stubSpeedKmh = std::max(0.0, m_stubSpeedKmh - 30.0 * dtSec);
    return;
  }
  const double target = std::clamp(s.ui_speed_kmh, 0.0, 100.0);
  constexpr double kTau = 0.75;
  const double alpha = std::min(1.0, dtSec / kTau);
  m_stubSpeedKmh += (target - m_stubSpeedKmh) * alpha;
  m_stubSpeedKmh += (s.throttle * 38.0 - s.brake * 48.0) * dtSec;
  m_stubSpeedKmh = std::clamp(m_stubSpeedKmh, 0.0, 120.0);
  {
    static auto s_stubLog = std::chrono::steady_clock::time_point{};
    const auto n = std::chrono::steady_clock::now();
    if (s_stubLog.time_since_epoch().count() == 0 ||
        std::chrono::duration_cast<std::chrono::milliseconds>(n - s_stubLog).count() >= 2000) {
      s_stubLog = n;
      std::cout << "[Bridge][StubPhysics][2s] simSpeed_kmh=" << m_stubSpeedKmh << " ui_target_kmh=" << s.ui_speed_kmh
                << " steer=" << s.steering << " remote=" << (s.remote_enabled ? "1" : "0") << std::endl;
    }
  }
#endif
}

void CarlaRunner::getVelocity(double& speedKmh) const {
#ifdef ENABLE_LIBCARLA
  std::lock_guard<std::mutex> carlaLock(m_carlaMutex);
  speedKmh = 0.0;
  if (m_lib && m_lib->vehicle) {
    try {
      speedKmh = velocityToKmh(m_lib->vehicle->GetVelocity());
    } catch (...) {
      speedKmh = 0.0;
    }
  }
#else
  std::lock_guard<std::mutex> lock(m_controlMutex);
  speedKmh = m_stubSpeedKmh;
#endif
}

void CarlaRunner::getCurrentState(ControlState& out) const {
  std::lock_guard<std::mutex> lock(m_controlMutex);
  out = m_controlState;
}

}  // namespace carla_bridge
