#ifndef VEHICLE_CONTROLLER_H
#define VEHICLE_CONTROLLER_H

#include <string>
#include <memory>
#include <mutex>
#include <chrono>
#include <algorithm>

#ifdef ENABLE_CARLA
#include <carla/client/Actor.h>
#include <boost/shared_ptr.hpp>
#endif

/**
 * @brief 车辆控制器
 * 处理车辆控制指令（方向盘、油门、刹车、档位）+ 基础安全状态机
 */
class VehicleController {
public:
    enum class SafetyState {
        IDLE,      ///< 无会话 / 未准备
        ARMED,     ///< 已有会话但尚未进入正常控制
        ACTIVE,    ///< 正常控制
        SAFE_STOP  ///< 安全停车（看门狗/故障触发）
    };

    struct ControlCommand {
        double steering = 0.0;  // -1.0 到 1.0
        double throttle = 0.0;  // 0.0 到 1.0
        double brake = 0.0;     // 0.0 到 1.0
        int gear = 1;           // -1: 倒档, 0: 空档, 1: 前进
    };

    VehicleController();
    ~VehicleController();

    // 处理控制指令（视为“有效指令”，会刷新 lastValidCmd 并驱动状态机）
    void processCommand(const ControlCommand &cmd);
    void processCommand(double steering, double throttle, double brake, int gear);
    
    // 处理控制指令（带安全参数：防重放与时序校验）
    void processCommand(double steering, double throttle, double brake, int gear, uint32_t seq, int64_t timestampMs);
    
    // 设置网络质量（RTT），用于 Fail-safe 降级
    void setNetworkQuality(double rtt);

    // 获取当前控制状态
    ControlCommand getCurrentCommand() const;

    // 当前安全状态
    SafetyState getSafetyState() const;

    // 看门狗：在主循环中周期调用，timeoutMs 默认 500
    void watchdogTick(int timeoutMs = 500);

    // 紧急停止（立即进入 SAFE_STOP）
    void emergencyStop();

    // 获取网络质量（供遥测上报使用）
    double getNetworkQuality() const;

    // 远驾接管状态管理
    void setRemoteControlEnabled(bool enabled);
    bool isRemoteControlEnabled() const;
    
    // 驾驶模式管理（遥控、自驾、远驾）
    enum class DrivingMode {
        REMOTE_CONTROL,  // 遥控模式
        AUTONOMOUS,      // 自驾模式
        REMOTE_DRIVING   // 远驾模式
    };
    void setDrivingMode(DrivingMode mode);
    DrivingMode getDrivingMode() const;
    std::string getDrivingModeString() const;  // 返回字符串："遥控"、"自驾"、"远驾"
    
    // 清扫状态管理
    void setSweepActive(bool active);
    bool isSweepActive() const;

#ifdef ENABLE_CARLA
    // 设置 CARLA 仿真车辆句柄
    void setCarlaVehicle(boost::shared_ptr<carla::client::Actor> actor);
#endif

private:
    mutable std::mutex m_mutex;
    ControlCommand m_currentCommand;
    SafetyState m_state{SafetyState::IDLE};
    std::chrono::steady_clock::time_point m_lastValidCmdTime{};
    bool m_remoteControlEnabled = false;  // 远驾接管状态
    DrivingMode m_drivingMode = DrivingMode::AUTONOMOUS;  // 驾驶模式（默认自驾）
    bool m_sweepActive = false;  // 清扫状态
    
    // 安全与网络状态
    uint32_t m_lastSeq = 0;
    int64_t m_lastCmdTimestamp = 0;
    double m_networkRtt = 0.0;

#ifdef ENABLE_CARLA
    boost::shared_ptr<carla::client::Actor> m_carlaActor;
#endif

    void applySafeStopLocked();  // 需要在已持有锁前提下调用
    
    // 实际车辆控制接口（需要根据实际硬件实现）
    void applySteering(double angle);
    void applyThrottle(double value);
    void applyBrake(double value);
    void applyGear(int gear);
    
#ifdef ENABLE_CARLA
    void applyControlToCarla();
#endif
};

#endif // VEHICLE_CONTROLLER_H
