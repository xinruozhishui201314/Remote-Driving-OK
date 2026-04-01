#include "carla_runner.h"
#include "rtmp_pusher.h"
#include <iostream>
#include <mutex>

namespace carla_bridge {

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
  std::cout << "[CARLA] 使用 LibCarla 连接（未实现时请先使用 Python Bridge 或 testsrc）" << std::endl;
  // TODO: 在此连接 LibCarla，加载地图，spawn 车辆
#else
  std::cout << "[CARLA] 未链接 LibCarla，仅 MQTT/状态/推流；收到 start_stream 时使用 testsrc 推流" << std::endl;
#endif
  return true;
}

void CarlaRunner::stop() {
  if (!m_running.exchange(false)) return;
  stopPushers();
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
            << " steering=" << state.steering << " throttle=" << state.throttle << " brake=" << state.brake << std::endl;
#ifdef ENABLE_LIBCARLA
  // TODO: vehicle->ApplyControl(...)
#endif
}

void CarlaRunner::getVelocity(double& speedKmh) const {
  speedKmh = 0.0;
#ifdef ENABLE_LIBCARLA
  // TODO: vehicle->GetVelocity()
#endif
}

void CarlaRunner::getCurrentState(ControlState& out) const {
  std::lock_guard<std::mutex> lock(m_controlMutex);
  out = m_controlState;
}

}  // namespace carla_bridge
