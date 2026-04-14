#include "mqtt_bridge.h"
#include "carla_runner.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>
#include <cstdlib>

static std::atomic<bool> g_running{true};

static void onSignal(int) {
  g_running = false;
}

static std::string getEnv(const char* name, const std::string& defaultVal) {
  const char* v = std::getenv(name);
  return v && v[0] ? std::string(v) : defaultVal;
}

static int getEnvInt(const char* name, int defaultVal) {
  const char* v = std::getenv(name);
  if (!v || !v[0]) return defaultVal;
  return std::atoi(v);
}

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  std::string carlaHost = getEnv("CARLA_HOST", "127.0.0.1");
  uint16_t carlaPort = static_cast<uint16_t>(getEnvInt("CARLA_PORT", 2000));
  std::string carlaMap = getEnv("CARLA_MAP", "");
  std::string mqttBroker = getEnv("MQTT_BROKER", "127.0.0.1");
  int mqttPort = getEnvInt("MQTT_PORT", 1883);
  std::string zlmHost = getEnv("ZLM_HOST", "127.0.0.1");
  int zlmRtmp = getEnvInt("ZLM_RTMP_PORT", 1935);
  std::string zlmApp = getEnv("ZLM_APP", "teleop");
  std::string vin = getEnv("VIN", "carla-sim-001");
  int statusHz = getEnvInt("STATUS_HZ", 50);
  int camW = getEnvInt("CAMERA_WIDTH", 640);
  int camH = getEnvInt("CAMERA_HEIGHT", 480);
  int camFps = getEnvInt("CAMERA_FPS", 10);

#ifdef ENABLE_LIBCARLA
  std::cout << "[Bridge] LibCarla 已启用：车辆控制走 CARLA ApplyControl/GetVelocity；推流仍取决于 RtmpPusher（无相机绑定时可能需 testsrc 或另接 Python 桥）" << std::endl;
#else
  std::cout << "[Bridge] 视频源: testsrc（ffmpeg 测试图案，非 CARLA 画面）" << std::endl;
#endif
  std::cout << "[Bridge] CARLA=" << carlaHost << ":" << carlaPort
            << " MQTT=" << mqttBroker << ":" << mqttPort
            << " ZLM=" << zlmHost << ":" << zlmRtmp << " VIN=" << vin << std::endl;

  carla_bridge::MqttBridge mqtt(mqttBroker, mqttPort, vin);
  carla_bridge::CarlaRunner carla(carlaHost, carlaPort, carlaMap, zlmHost, zlmRtmp, zlmApp, camW, camH, camFps, vin);

  mqtt.setControlCallback([&carla](const std::string&, const carla_bridge::ControlState& state) {
    carla.applyControl(state);
  });

  if (!mqtt.connect()) {
    std::cerr << "[Bridge] 环节: MQTT 连接失败，退出" << std::endl;
    return 1;
  }
  std::cout << "[Bridge] 环节: MQTT 已就绪，即将启动 CARLA Runner" << std::endl;

  if (!carla.start()) {
    std::cerr << "[Bridge] 环节: CARLA Runner 启动失败，退出" << std::endl;
    return 1;
  }
  std::cout << "[Bridge] 环节: CARLA 已就绪，主循环启动 statusHz=" << statusHz << " 每 " << (1000.0 / statusHz) << " ms 一轮" << std::endl;

  double dt = 1.0 / statusHz;
  bool prevStreaming = false;
  int loopCount = 0;
  int loopErrors = 0;
  const int kLogInterval = (statusHz >= 10) ? static_cast<int>(statusHz) * 5 : 50;  // 每约 5 秒打一次

  while (g_running) {
    // ★ 异常隔离：每个循环步骤独立 try-catch，任一模块故障不导致整个主循环崩溃
    carla_bridge::ControlState state;
    try {
      mqtt.getState(state);
    } catch (const std::exception& e) {
      std::cerr << "[Bridge][MainLoop] mqtt.getState exception: " << e.what() << std::endl;
    }

    try {
      if (state.streaming && !prevStreaming) {
        std::cout << "[CARLA-Bridge][ZLM][Push] 主循环: streaming false→true，setStreaming(true)（约 5~15s 后 ZLM 可见四路 RTMP）" << std::endl;
        carla.setStreaming(true);
      } else if (!state.streaming && prevStreaming) {
        std::cout << "[CARLA-Bridge][ZLM][Push] 主循环: streaming true→false，setStreaming(false) 停止四路推流" << std::endl;
        carla.setStreaming(false);
      }
      prevStreaming = state.streaming;
    } catch (const std::exception& e) {
      std::cerr << "[Bridge][MainLoop] carla.setStreaming exception: " << e.what() << std::endl;
    }

    try {
      carla.integrateSpeedStep(dt);
    } catch (const std::exception& e) {
      std::cerr << "[Bridge][MainLoop] carla.integrateSpeedStep exception: " << e.what() << std::endl;
    }

    double speedKmh = 0.0;
    try {
      carla.getVelocity(speedKmh);
    } catch (const std::exception& e) {
      std::cerr << "[Bridge][MainLoop] carla.getVelocity exception: " << e.what() << std::endl;
    }

    carla_bridge::ControlState current;
    try {
      carla.getCurrentState(current);
    } catch (const std::exception& e) {
      std::cerr << "[Bridge][MainLoop] carla.getCurrentState exception: " << e.what() << std::endl;
    }
    std::string mode = current.remote_enabled ? "远驾" : "自驾";

    try {
      mqtt.publishStatus(speedKmh, current.gear, current.steering, current.throttle, current.brake,
                        current.remote_enabled, current.streaming, mode);
    } catch (const std::exception& e) {
      std::cerr << "[Bridge][MainLoop] mqtt.publishStatus exception: " << e.what() << std::endl;
    }

    // ★ 精确定位：周期性打印当前发布的 remote_enabled，便于确认车端实际下发值
    if (++loopCount >= kLogInterval) {
      loopCount = 0;
      std::cout << "[Bridge][STATUS_5s] remote=" << (current.remote_enabled ? "1" : "0") << " mode=" << mode
                << " pub_speed_kmh=" << speedKmh << " ui_cmd_kmh=" << current.ui_speed_kmh
                << " steer=" << current.steering << " gear=" << current.gear
                << " stream=" << (current.streaming ? "1" : "0") << std::endl;
    }

    // ★ 异常计数监控：每 10 次循环打印一次，便于判断模块是否持续异常
    if (++loopErrors % 10 == 0) {
      std::cout << "[Bridge][MainLoop] alive count=" << loopErrors
                << " mqtt_connected=" << mqtt.isConnected()
                << " streaming=" << current.streaming
                << " speed_kmh=" << speedKmh << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::duration<double>(dt));
  }

  carla.stop();
  mqtt.disconnect();
  std::cout << "[Bridge] 已退出" << std::endl;
  return 0;
}
