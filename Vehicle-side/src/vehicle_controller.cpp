#include "vehicle_controller.h"
#include "common/logger.h" // 引入统一日志组件
#include <algorithm>
#include <cmath>
#include <chrono>
#include <chrono>
#include <stdexcept>

#ifdef ENABLE_CARLA
#include <carla/client/Actor.h>
#include <boost/shared_ptr.hpp>
#endif

VehicleController::VehicleController()
    : m_lastSeq(0)
    , m_lastCmdTimestamp(0)
    , m_networkRtt(0.0)
{
    // 初始化日志系统
    vehicle::common::Logger::init("vehicle-ctrl", "info");
    LOG_INFO("Vehicle-side Controller initialized");
    
    m_lastValidCmdTime = std::chrono::steady_clock::now();
    m_state = SafetyState::IDLE;
}

VehicleController::~VehicleController()
{
    emergencyStop();
}

void VehicleController::processCommand(const ControlCommand &cmd)
{
    processCommand(cmd.steering, cmd.throttle, cmd.brake, cmd.gear);
}

void VehicleController::processCommand(double steering, double throttle, double brake, int gear)
{
    // 调用带安全参数的重载版本（兼容旧接口，seq和timestamp默认为0）
    processCommand(steering, throttle, brake, gear, 0, 0);
}

void VehicleController::processCommand(double steering, double throttle, double brake, int gear, uint32_t seq, int64_t timestampMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Task 1: Anti-replay and Timestamp validation
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    
    if (seq > 0) {
        // Recovery logic: If current timestamp is much newer than last, allow reset (e.g. after restart)
        if (seq <= m_lastSeq) {
            // If timestamp indicates this is a very recent packet vs old sequence, allow reset
            if (std::abs(now - m_lastCmdTimestamp) > 2000 && m_lastCmdTimestamp != 0) {
                 LOG_WARN("Sequence reset detected due to timestamp jump. LastSeq: {}, CurrSeq: {}", m_lastSeq, seq);
                 m_lastSeq = seq; // Allow reset
            } else {
                 LOG_ERROR("SEC-7002: Replay attack or invalid sequence. Last: {}, Current: {}", m_lastSeq, seq);
                 return; // 丢弃指令
            }
        }
        if (std::abs(now - timestampMs) > 2000 && m_lastCmdTimestamp != 0) {
             // 注意：这里简单对比时间差，实际应对比系统当前时间与 timestampMs
             // 为简化，这里假设 timestampMs 是最近一次有效指令时间，且单调递增
             // 若要严格校验，应获取当前系统时间 std::chrono::milliseconds
             auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
             if (std::abs(current_time - timestampMs) > 2000) {
                LOG_ERROR("SEC-7001: Timestamp expired. Diff: {}ms", std::abs(current_time - timestampMs));
                return; // 丢弃指令
             }
        }
        m_lastSeq = seq;
        m_lastCmdTimestamp = timestampMs;
    }
    
    // --- 安全校验：防重放与时序 ---
    if (seq > 0) {
        // Recovery logic: If current timestamp is much newer than last, allow reset (e.g. after restart)
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (seq <= m_lastSeq) {
            // If timestamp indicates this is a very recent packet vs old sequence, allow reset
            if (std::abs(nowMs - m_lastCmdTimestamp) > 2000 && m_lastCmdTimestamp != 0) {
                 LOG_WARN("Sequence reset detected due to timestamp jump. LastSeq: {}, CurrSeq: {}", m_lastSeq, seq);
                 m_lastSeq = seq; // Allow reset
            } else {
                 LOG_ERROR("SEC-7002: Replay attack or invalid sequence. Last: {}, Current: {}", m_lastSeq, seq);
                 return; // 丢弃指令
            }
        }
        if (std::abs(timestampMs - m_lastCmdTimestamp) > 2000 && m_lastCmdTimestamp != 0) {
             auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
             if (std::abs(now - timestampMs) > 2000) {
                LOG_ERROR("SEC-7001: Timestamp expired. Diff: {}ms", std::abs(now - timestampMs));
                return; // 丢弃指令
             }
        }
        m_lastSeq = seq;
        m_lastCmdTimestamp = timestampMs;
    }

    // --- 网络质量降级 ---
    double effectiveThrottle = throttle;
    if (m_networkRtt > 300.0) {
        // 重度降级：强制安全停车
        effectiveThrottle = 0.0;
        LOG_WARN("Network Critically Degraded (RTT={:.1f}ms > 300ms). SAFE_STOP.", m_networkRtt);

        m_currentCommand.steering = 0.0;
        m_currentCommand.throttle = 0.0;
        m_currentCommand.brake = 1.0;
        m_currentCommand.gear = 0;

        applySteering(0.0);
        applyThrottle(0.0);
        applyBrake(1.0);
        applyGear(0);
        return;
    } else if (m_networkRtt > 150.0) {
        // 轻度降级：限制油门 20%
        effectiveThrottle = std::clamp(throttle, 0.0, 0.2);
        LOG_WARN("Network Degraded (RTT={:.1f}ms). Throttle limited to 20%.", m_networkRtt);
    }

    int oldGear = m_currentCommand.gear;

    // 限制值范围
    m_currentCommand.steering = std::clamp(steering, -1.0, 1.0);
    m_currentCommand.throttle = std::clamp(effectiveThrottle, 0.0, 1.0);
    m_currentCommand.brake = std::clamp(brake, 0.0, 1.0);

    // 档位值：-1=R, 0=N, 1=D, 2=P
    if (gear == -1 || gear == 0 || gear == 1 || gear == 2) {
        m_currentCommand.gear = gear;
    } else {
        m_currentCommand.gear = 1;
        LOG_WARN("Invalid gear {}, default to Drive (1)", gear);
    }

    // 应用控制指令到硬件接口
    applySteering(m_currentCommand.steering);
    applyThrottle(m_currentCommand.throttle);
    applyBrake(m_currentCommand.brake);
    applyGear(m_currentCommand.gear);

#ifdef ENABLE_CARLA
    applyControlToCarla();
#endif

    // 档位字符串用于日志
    std::string gearStr;
    switch (m_currentCommand.gear) {
        case -1: gearStr = "R"; break;
        case 0:  gearStr = "N"; break;
        case 1:  gearStr = "D"; break;
        case 2:  gearStr = "P"; break;
        default: gearStr = "UNK"; break;
    }

    LOG_INFO("Control command processed: steering={:.2f}, throttle={:.2f}, brake={:.2f}, gear={}",
              m_currentCommand.steering,
              m_currentCommand.throttle,
              m_currentCommand.brake,
              m_currentCommand.gear);

    if (oldGear != m_currentCommand.gear) {
        LOG_INFO("Gear changed: {} -> {}", oldGear, m_currentCommand.gear);
    }
}

VehicleController::ControlCommand VehicleController::getCurrentCommand() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentCommand;
}

VehicleController::SafetyState VehicleController::getSafetyState() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

void VehicleController::watchdogTick(int timeoutMs)
{
    using namespace std::chrono;
    std::lock_guard<std::mutex> lock(m_mutex);

    auto now = steady_clock::now();
    auto elapsedMs = duration_cast<milliseconds>(now - m_lastValidCmdTime).count();

    // Pre-timeout warning (0.8x threshold)
    if (elapsedMs > timeoutMs * 0.8 && m_state != SafetyState::SAFE_STOP) {
        LOG_WARN("Watchdog approaching timeout. Elapsed: {}ms (Threshold: {}ms)", elapsedMs, timeoutMs);
    }

    if (elapsedMs > timeoutMs && m_state != SafetyState::SAFE_STOP) {
        // 记录 ERROR 级别日志，因为触发了安全停车
        LOG_ERROR("Watchdog timeout! Elapsed: {}ms > Threshold: {}ms. Entering SAFE_STOP.", elapsedMs, timeoutMs);
        m_state = SafetyState::SAFE_STOP;
        // Ensure Safe Stop is applied only once
        if (m_state == SafetyState::SAFE_STOP) {
            applySafeStopLocked();
        }
#ifdef ENABLE_CARLA
        applyControlToCarla(); // 确保 CARLA 收到停车指令
#endif
    }
}

void VehicleController::emergencyStop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    LOG_CRITICAL("Emergency Stop triggered manually!");
    m_state = SafetyState::SAFE_STOP;
    applySafeStopLocked();
#ifdef ENABLE_CARLA
    applyControlToCarla(); // 确保 CARLA 收到停车指令
#endif
}

void VehicleController::setRemoteControlEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    bool oldValue = m_remoteControlEnabled;
    m_remoteControlEnabled = enabled;
    
    // 使用 AUDIT 级别记录关键操作变更
    if (enabled) {
        LOG_INFO("Remote Control ENABLED");
    } else {
        LOG_INFO("Remote Control DISABLED");
    }
}

bool VehicleController::isRemoteControlEnabled() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_remoteControlEnabled;
}

void VehicleController::setDrivingMode(DrivingMode mode)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DrivingMode oldMode = m_drivingMode;
    m_drivingMode = mode;
    
    std::string modeStr;
    switch (mode) {
        case DrivingMode::REMOTE_CONTROL: modeStr = "Remote Control"; break;
        case DrivingMode::AUTONOMOUS:  modeStr = "Autonomous"; break;
        case DrivingMode::REMOTE_DRIVING: modeStr = "Remote Driving"; break;
        default: modeStr = "Unknown"; break;
    }

    LOG_INFO("Driving mode changed: {} -> {}", static_cast<int>(oldMode), static_cast<int>(mode));
}

VehicleController::DrivingMode VehicleController::getDrivingMode() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_drivingMode;
}

std::string VehicleController::getDrivingModeString() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    switch (m_drivingMode) {
        case DrivingMode::REMOTE_CONTROL: return "Remote Control";
        case DrivingMode::AUTONOMOUS: return "Autonomous";
        case DrivingMode::REMOTE_DRIVING: return "Remote Driving";
        default: return "Unknown";
    }
}

void VehicleController::setNetworkQuality(double rtt)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_networkRtt = rtt;
}

double VehicleController::getNetworkQuality() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_networkRtt;
}

void VehicleController::setSweepActive(bool active)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sweepActive != active) {
        m_sweepActive = active;
        LOG_INFO("Sweep status updated: {}", active ? "Active" : "Inactive");
    }
}

bool VehicleController::isSweepActive() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sweepActive;
}

#ifdef ENABLE_CARLA
void VehicleController::setCarlaVehicle(boost::shared_ptr<carla::client::Actor> actor)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_carlaActor = actor;
    if (actor) {
        LOG_INFO("CARLA Vehicle attached: ID={}", actor->GetId());
    } else {
        LOG_WARN("CARLA Vehicle detached");
    }
}
#endif

void VehicleController::applySafeStopLocked()
{
    // 在 SAFE_STOP 中强制停车：方向置 0，油门 0，刹车 1，档位 0
    m_currentCommand.steering = 0.0;
    m_currentCommand.throttle = 0.0;
    m_currentCommand.brake = 1.0;
    m_currentCommand.gear = 0;

    applySteering(0.0);
    applyThrottle(0.0);
    applyBrake(1.0);
    applyGear(0);
    
    LOG_WARN("Safe Stop Applied: Throttle=0, Brake=1, Gear=N");
}

void VehicleController::applySteering(double angle)
{
    // TODO: 实现实际的方向盘控制
    // 例如：通过 CAN 总线、串口、ROS2 话题等
}

void VehicleController::applyThrottle(double value)
{
    // TODO: 实现实际的油门控制
}

void VehicleController::applyBrake(double value)
{
    // TODO: 实现实际的刹车控制
}

void VehicleController::applyGear(int gear)
{
    // TODO: 实现实际的档位控制
    // 实现硬件逻辑时可以参考这里：
    /*
    std::string gearStr = (gear == -1 ? "R" : (gear == 0 ? "N" : (gear == 1 ? "D" : "P")));
    LOG_DEBUG("Applying Gear: {} ({})", gear, gearStr);
    */
}

#ifdef ENABLE_CARLA
void VehicleController::applyControlToCarla()
{
    if (!m_carlaActor) return;

    // 尝试转换为 Vehicle
    auto vehicle = boost::static_pointer_cast<carla::client::Vehicle>(m_carlaActor);

    if (vehicle) {
        carla::rpc::VehicleControl control;

        control.steer = static_cast<float>(m_currentCommand.steering);
        control.throttle = static_cast<float>(m_currentCommand.throttle);
        control.brake = static_cast<float>(m_currentCommand.brake);

        // 档位映射：-1=Reverse, 0=Neutral, 1=Drive
        if (m_currentCommand.gear == -1) {
            control.gear = -1;
        } else if (m_currentCommand.gear == 0) {
            control.gear = 0;
        } else {
            control.gear = 1;
        }

        // 简单的手动/自动逻辑：如果油门>0且档位正确则前进，刹车>0则停车
        // 在 CARLA 中，设置 Reverse 标志位或 Gear 均可控制方向
        control.reverse = (m_currentCommand.gear == -1);

        // ★ 异常保护：CARLA actor 在 simulation 停止后已失效，此时 ApplyControl 会抛异常。
        // 捕获后记录日志并进入 SAFE_STOP，防止崩溃。
        try {
            vehicle->ApplyControl(control);

            // 记录控制状态到日志
            LOG_LATENCY("Control sent to CARLA: steer={:.2f}, throt={:.2f}, brake={:.2f}, gear={}",
                       control.steer, control.throttle, control.brake, control.gear);
        } catch (const std::exception& e) {
            LOG_ERROR("CARLA ApplyControl failed: {}. Entering SAFE_STOP.", e.what());
            m_state = SafetyState::SAFE_STOP;
            applySafeStopLocked();
        }
    }
}
#endif
