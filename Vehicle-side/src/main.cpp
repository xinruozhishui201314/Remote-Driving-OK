#include <iostream>
#include <memory>
#include <signal.h>
#include <thread>
#include <chrono>
#include <cstdlib>
#include "vehicle_controller.h"
#include "mqtt_handler.h"
#include "whip_publisher.h"
#include "zlm_control_channel.h"
#include "vehicle_config.h"
#include "control_protocol.h"

#ifdef ENABLE_ROS2
#include "ros2_bridge.h"
#include <rclcpp/rclcpp.hpp>
#endif

#ifdef ENABLE_CARLA
#include <carla/client/Client.h>
#include <carla/client/World.h>
#include <carla/client/Actor.h>
#include <carla/exception/Exception.h>
#endif

volatile bool g_running = true;

void signalHandler(int signal) {
    (void)signal;
    g_running = false;
    std::cout << "\n[Vehicle-side] received signal, shutting down..." << std::endl;
}

int main(int argc, char *argv[])
{
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "==========================================" << std::endl;
    std::cout << "[Vehicle-side] Controller starting" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << std::endl;

    // 可选：WHIP Demo（仅用于验证 ZLMediaKit WHIP 接口联通性）
    const char *whip_env = std::getenv("VEHICLE_WHIP_DEMO_URL");
    if (whip_env && std::string(whip_env).size() > 0) {
        std::cout << "[Vehicle-side][WHIP] Demo URL set, sending one WHIP request..." << std::endl;
        bool ok = WhipPublisherDemo::run_once(whip_env);
        std::cout << "[Vehicle-side][WHIP] result: " << (ok ? "ok" : "fail") << std::endl;
        std::cout << "------------------------------------------" << std::endl;
    }

#ifdef ENABLE_ROS2
    // 初始化 ROS2
    rclcpp::init(argc, argv);
    auto ros2_bridge = std::make_shared<Ros2Bridge>();
    std::cout << "[Vehicle-side][ROS2] initialized" << std::endl;
#endif

    // 创建车辆控制器
    auto vehicle_controller = std::make_unique<VehicleController>();

#ifdef ENABLE_CARLA
    // 初始化 CARLA 客户端
    const char* carla_host = std::getenv("CARLA_HOST");
    const char* carla_port_env = std::getenv("CARLA_PORT");
    std::string carla_host_str = carla_host ? carla_host : "localhost";
    uint16_t carla_port = carla_port_env ? std::stoul(carla_port_env) : 2000;

    std::cout << "[Vehicle-side][Carla] Connecting to " << carla_host_str << ":" << carla_port << "..." << std::endl;
    
    try {
        auto client = std::make_shared<carla::client::Client>(carla_host_str, carla_port);
        client->SetTimeout(std::chrono::seconds(10));
        
        auto world = client->GetWorld();
        
        // 获取车辆列表，默认获取第一个找到的车辆
        auto actor_library = world.GetActors();
        auto vehicles = actor_library->Filter("vehicle.*");
        
        if (!vehicles->Empty()) {
            auto carla_vehicle = (*vehicles)[0]; // 获取第一个车辆
            std::cout << "[Vehicle-side][Carla] Found vehicle: " << carla_vehicle->GetId() 
                      << " " << carla_vehicle->GetTypeId() << std::endl;
            
            // 设置给控制器
            vehicle_controller->setCarlaVehicle(carla_vehicle);
        } else {
            std::cerr << "[Vehicle-side][Carla] Warning: No vehicles found in simulation." << std::endl;
        }
    } catch (const carla::Exception& e) {
        std::cerr << "[Vehicle-side][Carla] Connection failed: " << e.what() << std::endl;
        std::cerr << "[Vehicle-side][Carla] Continuing without simulation control..." << std::endl;
    }
#endif

    // 创建 MQTT 处理器
    auto mqtt_handler = std::make_unique<MqttHandler>(vehicle_controller.get());

    // 预留：ZLMediaKit 控制通道（当前仅占位 & 日志）
    auto zlm_control = std::make_unique<ZlmControlChannel>(vehicle_controller.get());
    
    // 连接 MQTT：优先使用环境变量 MQTT_BROKER_URL，其次使用命令行参数，最后回退到 localhost
    std::string mqtt_broker = "mqtt://localhost:1883";
    const char *env_mqtt = std::getenv("MQTT_BROKER_URL");
    if (env_mqtt && std::string(env_mqtt).size() > 0) {
        mqtt_broker = env_mqtt;
    }
    if (argc > 1) {
        mqtt_broker = argv[1];
    }
    
    std::cout << "[Vehicle-side][Startup] MQTT Broker: " << mqtt_broker << std::endl;
    const char* vin_env = std::getenv("VEHICLE_VIN");
    const char* push_env = std::getenv("VEHICLE_PUSH_SCRIPT");
    std::cout << "[Vehicle-side][Startup] VEHICLE_VIN=" << (vin_env && vin_env[0] ? vin_env : "(unset)") << std::endl;
    std::cout << "[Vehicle-side][Startup] VEHICLE_PUSH_SCRIPT=" << (push_env && push_env[0] ? push_env : "(unset)") << std::endl;
    if (!mqtt_handler->connect(mqtt_broker)) {
        std::cerr << "[Vehicle-side][MQTT] connect failed, check broker and network err=E_MQTT_CONN_FAILED" << std::endl;
        return 1;
    }

    std::cout << "[Vehicle-side][Startup] running, responding to start_stream/stop_stream for this VIN only" << std::endl;
    std::cout << "[Vehicle-side][Startup] Ctrl+C to exit; optional: WHIP_URL or AUTO_WHIP_FROM_BACKEND=1, ZLM_CONTROL_WS_URL" << std::endl;
    std::cout << std::endl;

    // 可选：执行一次 WHIP Demo（只做 HTTP 流程打通，不做真实媒体推流）
    const char *whip_env2 = std::getenv("WHIP_URL");
    const char *auto_backend_env = std::getenv("AUTO_WHIP_FROM_BACKEND");
    if (auto_backend_env && std::string(auto_backend_env).size() > 0) {
        std::cout << "[Vehicle-side][WHIP] AUTO_WHIP_FROM_BACKEND set, fetching media.whip from Backend..." << std::endl;
        WhipPublisherDemo::run_once_via_backend();
    } else if (whip_env2 && std::string(whip_env2).size() > 0) {
        std::cout << "[Vehicle-side][WHIP] WHIP_URL set, running one WHIP demo request..." << std::endl;
        WhipPublisherDemo::run_once(whip_env2);
    }

    // 加载配置
    auto& config = VehicleConfig::getInstance();
    
    // 尝试从配置文件加载（按优先级顺序）
    const char* configPath = std::getenv("VEHICLE_CONFIG_PATH");
    if (configPath && configPath[0] != '\0') {
        if (!config.loadFromFile(configPath)) {
            std::cerr << "[Vehicle-side][Config] warn: cannot load config " << configPath << ", using defaults" << std::endl;
        }
    } else {
        // 尝试默认路径
        if (!config.loadFromFile("/app/config/vehicle_config.json")) {
            // 如果默认路径不存在，使用默认配置（不报错）
            std::cout << "[Vehicle-side][Config] using default config (no file found)" << std::endl;
        }
    }
    
    // 获取上报频率
    int publishFrequency = config.getStatusPublishFrequency();
    int publishIntervalMs = config.getStatusPublishIntervalMs();
    std::cout << "[Vehicle-side][Config] status_publish_frequency=" << publishFrequency << " Hz interval_ms=" << publishIntervalMs << std::endl;
    
    // 启动 ZLM 控制通道占位（如果配置了 URL，则仅打印日志）
    zlm_control->start();

    // 主循环：看门狗 + 周期发布车辆状态到 MQTT（供远程驾驶主界面显示）
    auto lastPublishTime = std::chrono::steady_clock::now();
    auto lastStreamingCheckTime = std::chrono::steady_clock::now();
    int loopErrors = 0;
    while (g_running) {
#ifdef ENABLE_ROS2
        try {
            rclcpp::spin_some(ros2_bridge);
        } catch (const std::exception& e) {
            std::cerr << "[Vehicle-side][ROS2] spin_some exception: " << e.what() << std::endl;
        }
#endif
        try {
            vehicle_controller->watchdogTick(500);
        } catch (const std::exception& e) {
            std::cerr << "[Vehicle-side][Controller] watchdogTick exception: " << e.what() << std::endl;
        }

        // 根据配置的频率发布状态
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPublishTime).count();
        if (elapsed >= publishIntervalMs) {
            try {
                mqtt_handler->publishStatus();
            } catch (const std::exception& e) {
                std::cerr << "[Vehicle-side][MQTT] publishStatus exception: " << e.what() << std::endl;
            }
            lastPublishTime = now;
        }

        // ★ 推流健康检查：每 10 秒检查一次推流状态，如果推流应该运行但未运行，自动恢复
        auto streamingCheckElapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - lastStreamingCheckTime).count();
        if (streamingCheckElapsed >= 10) {
            check_and_restore_streaming();
            lastStreamingCheckTime = now;
        }

        // 每 10 次循环打印一次心跳日志（便于判断主循环是否卡住）
        if (++loopErrors % 10 == 0) {
            std::cout << "[Vehicle-side][MainLoop] alive count=" << loopErrors
                      << " mqtt_connected=" << mqtt_handler->isConnected()
                      << " ctrl_state=" << static_cast<int>(vehicle_controller->getSafetyState())
                      << std::endl;
        }

        // 使用较小的睡眠间隔以保证精度
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // 清理
    mqtt_handler->disconnect();
    
#ifdef ENABLE_ROS2
    rclcpp::shutdown();
#endif

    std::cout << "[Vehicle-side] shutdown complete" << std::endl;
    return 0;
}
