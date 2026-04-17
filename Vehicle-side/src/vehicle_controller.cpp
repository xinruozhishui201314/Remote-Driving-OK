#include "vehicle_controller.h"
#include "common/logger.h"    // 引入统一日志组件
#include "common/error_code.h" // 引入错误码
#include "common/log_macros.h" // 引入增强日志宏
#include <algorithm>
#include <cmath>
#include <iostream>
#include <chrono>
#include <chrono>
#include <stdexcept>
#include <sstream>

#ifdef ENABLE_CARLA
#include <carla/client/Actor.h>
#include <boost/shared_ptr.hpp>
#endif

using namespace std;
using namespace vehicle::error;

VehicleController::VehicleController()
    : m_lastSeq(0)
    , m_lastCmdTimestamp(0)
    , m_networkRtt(0.0)
{
    LOG_ENTRY();  // 函数入口追踪

    try {
        LOG_INFO("Vehicle-side Controller initialized");
        LOG_INFO("Controller version: 1.0.0, Build timestamp: {}", __DATE__ " " __TIME__);

        m_lastValidCmdTime = std::chrono::steady_clock::now();
        m_state = SafetyState::IDLE;

        LOG_CTRL_INFO("VehicleController constructed successfully");
        LOG_CTRL_INFO("Initial state: IDLE, RemoteControl: disabled, DrivingMode: AUTONOMOUS");
        LOG_INFO("RESTART_DETECTED: VehicleController has been (re)initialized. Driving mode defaulted to AUTONOMOUS.");
        LOG_EXIT_WITH_VALUE("success");
    } catch (const std::exception& e) {
        LOG_CTRL_CRITICAL("CRITICAL: Failed to construct VehicleController: {}", e.what());
        LOG_CTRL_CRITICAL("System may be in unstable state!");
        throw;  // 重新抛出异常，因为控制器初始化失败是致命错误
    }
}

VehicleController::~VehicleController()
{
    LOG_ENTRY();

    try {
        // 执行安全停车
        emergencyStop();
        LOG_CTRL_INFO("VehicleController destroyed, emergency stop applied");
        LOG_EXIT();
    } catch (const std::exception& e) {
        // 析构函数中尽量不要抛出异常，但需要记录日志
        std::cerr << "[CTRL][CRITICAL] Exception in destructor: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[CTRL][CRITICAL] Unknown exception in destructor" << std::endl;
    }
}

void VehicleController::processCommand(const ControlCommand &cmd)
{
    LOG_CTRL_TRACE("processCommand(ControlCommand) called: steering={}, throttle={}, brake={}, gear={}",
                   cmd.steering, cmd.throttle, cmd.brake, cmd.gear);
    processCommand(cmd.steering, cmd.throttle, cmd.brake, cmd.gear);
}

void VehicleController::processCommand(double steering, double throttle, double brake, int gear)
{
    LOG_CTRL_DEBUG("processCommand(steering={}, throttle={}, brake={}, gear={}) - calling overload with default seq=0, timestamp=0",
                   steering, throttle, brake, gear);
    // 调用带安全参数的重载版本（兼容旧接口，seq和timestamp默认为0）
    processCommand(steering, throttle, brake, gear, 0, 0);
}

void VehicleController::processCommand(double steering, double throttle, double brake, int gear, uint32_t seq, int64_t timestampMs)
{
    LOG_ENTRY();
    std::lock_guard<std::mutex> lock(m_mutex);

    // 生成操作追踪ID
    std::stringstream trace_ss;
    trace_ss << "proc-" << seq << "-" << timestampMs;
    LOG_CTRL_TRACE("Processing command: steer={:.3f}, throttle={:.3f}, brake={:.3f}, gear={}, seq={}, ts={}",
                   steering, throttle, brake, gear, seq, timestampMs);

    try {
        // ==================== 参数验证 ====================
        // 验证方向盘值
        if (std::isnan(steering) || std::isinf(steering)) {
            LOG_CTRL_ERROR_WITH_CODE(Code::CTRL_INVALID_STEERING,
                "Invalid steering value: {} (NaN={}, Inf={}), defaulting to 0.0",
                steering, std::isnan(steering), std::isinf(steering));
            steering = 0.0;
        }

        // 验证油门值
        if (std::isnan(throttle) || std::isinf(throttle)) {
            LOG_CTRL_ERROR_WITH_CODE(Code::CTRL_INVALID_THROTTLE,
                "Invalid throttle value: {} (NaN={}, Inf={}), defaulting to 0.0",
                throttle, std::isnan(throttle), std::isinf(throttle));
            throttle = 0.0;
        }

        // 验证刹车值
        if (std::isnan(brake) || std::isinf(brake)) {
            LOG_CTRL_ERROR_WITH_CODE(Code::CTRL_INVALID_BRAKE,
                "Invalid brake value: {} (NaN={}, Inf={}), defaulting to 0.0",
                brake, std::isnan(brake), std::isinf(brake));
            brake = 0.0;
        }

        // 验证档位值
        if (gear != -1 && gear != 0 && gear != 1 && gear != 2) {
            LOG_CTRL_ERROR_WITH_CODE(Code::CTRL_INVALID_GEAR,
                "Invalid gear value: {} (valid: -1, 0, 1, 2), defaulting to 1 (Drive)",
                gear);
            gear = 1;
        }

        // ==================== Task 1: 防重放与时序校验 ====================
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (seq > 0) {
            // 序列号校验
            if (seq <= m_lastSeq) {
                // recovery逻辑: 如果当前时间戳比上次新很多，允许重置（如重启后）
                if (std::abs(now - m_lastCmdTimestamp) > 2000 && m_lastCmdTimestamp != 0) {
                    LOG_SAFE_WARN("SEC: Sequence reset detected due to timestamp jump. LastSeq={}, CurrSeq={}, TimeDiff={}ms",
                                 m_lastSeq, seq, std::abs(now - m_lastCmdTimestamp));
                    m_lastSeq = seq;  // 允许重置
                } else {
                    LOG_SEC_ERROR_WITH_CODE(Code::SEC_SEQ_INVALID,
                        "SEC-7002: Replay attack detected! LastSeq={}, CurrentSeq={}, TimeDiff={}ms",
                        m_lastSeq, seq, std::abs(now - m_lastCmdTimestamp));
                    LOG_EXIT_WITH_VALUE("rejected: replay_attack");
                    return;  // 丢弃指令
                }
            }

            // 时间戳校验
            if (std::abs(now - timestampMs) > 2000 && m_lastCmdTimestamp != 0) {
                LOG_SEC_ERROR_WITH_CODE(Code::SEC_TIMESTAMP_EXPIRED,
                    "SEC-7003: Timestamp expired! CurrTime={}, CmdTime={}, Diff={}ms > 2000ms threshold",
                    now, timestampMs, std::abs(now - timestampMs));
                LOG_EXIT_WITH_VALUE("rejected: timestamp_expired");
                return;  // 丢弃指令
            }

            m_lastSeq = seq;
            m_lastCmdTimestamp = timestampMs;
            LOG_CTRL_TRACE("Security check passed: seq={}, ts={}", seq, timestampMs);
        }

        // ==================== 状态更新与生存期重置 ====================
        m_lastValidCmdTime = std::chrono::steady_clock::now();
        if (m_state == SafetyState::SURVIVAL) {
            LOG_SAFE_INFO("Network recovered. Exiting SURVIVAL mode.");
        }
        m_state = SafetyState::ACTIVE;

        // ==================== 网络质量降级处理 ====================
        double effectiveThrottle = throttle;
        if (m_networkRtt > 300.0) {
            // 严重降级：强制安全停车
            effectiveThrottle = 0.0;
            LOG_SAFE_ERROR_WITH_CODE(Code::SAFETY_NETWORK_CRITICAL,
                "Network CRITICAL (RTT={:.1f}ms > 300ms). Forcing SAFE_STOP!",
                m_networkRtt);

            m_currentCommand.steering = 0.0;
            m_currentCommand.throttle = 0.0;
            m_currentCommand.brake = 1.0;
            m_currentCommand.gear = 0;

            applySteering(0.0);
            applyThrottle(0.0);
            applyBrake(1.0);
            applyGear(0);
            LOG_EXIT_WITH_VALUE("safe_stop: network_critical");
            return;
        } else if (m_networkRtt > 150.0) {
            // 轻度降级：限制油门 20%
            effectiveThrottle = std::clamp(throttle, 0.0, 0.2);
            LOG_SAFE_WARN("Network degraded (RTT={:.1f}ms > 150ms). Throttle limited to 20%.",
                         m_networkRtt);
        }

        int oldGear = m_currentCommand.gear;

        // ==================== 限制值范围 ====================
        m_currentCommand.steering = std::clamp(steering, -1.0, 1.0);
        m_currentCommand.throttle = std::clamp(effectiveThrottle, 0.0, 1.0);
        m_currentCommand.brake = std::clamp(brake, 0.0, 1.0);

        // 备份最后一次合法指令用于插值
        m_lastValidCommand = m_currentCommand;

        // 档位值：-1=R, 0=N, 1=D, 2=P
        if (gear == -1 || gear == 0 || gear == 1 || gear == 2) {
            m_currentCommand.gear = gear;
        } else {
            LOG_CTRL_WARN("Gear {} invalid, defaulting to Drive (1)", gear);
            m_currentCommand.gear = 1;
        }

        // ==================== 应用控制指令到硬件接口 ====================
        try {
            applySteering(m_currentCommand.steering);
            applyThrottle(m_currentCommand.throttle);
            applyBrake(m_currentCommand.brake);
            applyGear(m_currentCommand.gear);
        } catch (const std::exception& e) {
            LOG_CTRL_ERROR_WITH_CODE(Code::CTRL_HW_INTERFACE_ERROR,
                "Hardware interface error during applyControl: {}", e.what());
            // 继续执行，因为可能有软件层面的保护
        }

#ifdef ENABLE_CARLA
        try {
            applyControlToCarla();
        } catch (const std::exception& e) {
            LOG_CARLA_ERROR_WITH_CODE(Code::CARLA_APPLY_CONTROL_FAILED,
                "Failed to apply control to CARLA: {}", e.what());
        }
#endif

        // ==================== 档位字符串用于日志 ====================
        std::string gearStr;
        switch (m_currentCommand.gear) {
            case -1: gearStr = "R"; break;
            case 0:  gearStr = "N"; break;
            case 1:  gearStr = "D"; break;
            case 2:  gearStr = "P"; break;
            default: gearStr = "UNK"; break;
        }

        LOG_CTRL_INFO("CMD_PROCESSED: steer={:.2f}, throttle={:.2f}, brake={:.2f}, gear={} ({})"
                      " | NetworkRTT={:.1f}ms | State={}",
                      m_currentCommand.steering,
                      m_currentCommand.throttle,
                      m_currentCommand.brake,
                      m_currentCommand.gear, gearStr,
                      m_networkRtt,
                      static_cast<int>(m_state));

        if (oldGear != m_currentCommand.gear) {
            LOG_CTRL_INFO("Gear changed: {} -> {} ({})", oldGear, m_currentCommand.gear, gearStr);
        }

        LOG_EXIT_WITH_VALUE("success");

    } catch (const std::exception& e) {
        LOG_CTRL_ERROR_WITH_CODE(Code::SYS_UNEXPECTED_EXCEPTION,
            "Unexpected exception in processCommand: {}", e.what());
        LOG_EXIT_WITH_VALUE("exception");
        throw;  // 重新抛出，因为这是核心控制逻辑
    }
}

VehicleController::ControlCommand VehicleController::getCurrentCommand() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    LOG_CTRL_TRACE("getCurrentCommand called: steer={:.2f}, throttle={:.2f}, brake={:.2f}, gear={}",
                   m_currentCommand.steering, m_currentCommand.throttle,
                   m_currentCommand.brake, m_currentCommand.gear);
    return m_currentCommand;
}

VehicleController::SafetyState VehicleController::getSafetyState() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    LOG_CTRL_TRACE("getSafetyState called: state={}", static_cast<int>(m_state));
    return m_state;
}

void VehicleController::watchdogTick(int timeoutMs)
{
    LOG_ENTRY();

    using namespace std::chrono;
    std::lock_guard<std::mutex> lock(m_mutex);

    auto now = steady_clock::now();
    auto elapsedMs = duration_cast<milliseconds>(now - m_lastValidCmdTime).count();

    LOG_CTRL_TRACE("Watchdog tick: elapsed={}ms, timeout={}ms, state={}",
                   elapsedMs, timeoutMs, static_cast<int>(m_state));

    // ==================== 生存期/插值逻辑 (Smoothing) ====================
    // 策略：150ms-500ms 之间进入 SURVIVAL 模式，执行指令平滑衰减
    const int64_t survivalThresholdMs = 150; 
    
    if (elapsedMs > survivalThresholdMs && elapsedMs <= timeoutMs && 
        m_state != SafetyState::SAFE_STOP && m_state != SafetyState::IDLE) {
        
        if (m_state != SafetyState::SURVIVAL) {
            LOG_SAFE_WARN("Network jitter detected (elapsed={}ms). Entering SURVIVAL mode.", elapsedMs);
            m_state = SafetyState::SURVIVAL;
        }
        
        // 执行插值算法
        applyInterpolatedControlLocked(elapsedMs - survivalThresholdMs, timeoutMs - survivalThresholdMs);
        
        LOG_EXIT_WITH_VALUE("survival_interpolating");
        return;
    }

    // Pre-timeout warning (0.8x threshold)
    if (elapsedMs > timeoutMs * 0.8 && m_state != SafetyState::SAFE_STOP) {
        LOG_SAFE_WARN("Watchdog approaching timeout! Elapsed={}ms (threshold={}ms, warning at 80%)",
                     elapsedMs, timeoutMs);
    }

    if (elapsedMs > timeoutMs && m_state != SafetyState::SAFE_STOP) {
        // 记录 ERROR 级别日志，因为触发了安全停车
        LOG_SAFE_ERROR_WITH_CODE(Code::SAFETY_WATCHDOG_TIMEOUT,
            "WATCHDOG TIMEOUT! Elapsed={}ms > Threshold={}ms. Entering SAFE_STOP immediately! "
            "LastCmdSeq={}, LastCmdTs={}, RemoteEnabled={}, DrivingMode={}",
            elapsedMs, timeoutMs, m_lastSeq, m_lastCmdTimestamp, m_remoteControlEnabled, static_cast<int>(m_drivingMode));

        m_state = SafetyState::SAFE_STOP;
        m_remoteControlEnabled = false; // 超时自动释放
        m_activeSessionId.clear();      // 清除会话锁

        // Ensure Safe Stop is applied only once
        if (m_state == SafetyState::SAFE_STOP) {
            LOG_SAFE_ERROR("Executing SAFE_STOP: steering=0, throttle=0, brake=1.0, gear=N");
            applySafeStopLocked();
        }

#ifdef ENABLE_CARLA
        try {
            applyControlToCarla();  // 确保 CARLA 收到停车指令
            LOG_CARLA_INFO("CARLA notified of SAFE_STOP");
        } catch (const std::exception& e) {
            LOG_CARLA_ERROR_WITH_CODE(Code::CARLA_APPLY_CONTROL_FAILED,
                "Failed to notify CARLA of SAFE_STOP: {}", e.what());
        }
#endif

        LOG_EXIT_WITH_VALUE("safe_stop_triggered");
        return;
    }

    LOG_EXIT_WITH_VALUE("ok");
}

void VehicleController::applyInterpolatedControlLocked(int64_t survivalElapsedMs, int64_t survivalWindowMs)
{
    if (survivalWindowMs <= 0) return;

    // 计算生存进度 (0.0 -> 1.0)
    double progress = std::clamp(static_cast<double>(survivalElapsedMs) / survivalWindowMs, 0.0, 1.0);

    // 1. 油门线性衰减至 0
    m_currentCommand.throttle = m_lastValidCommand.throttle * (1.0 - progress);

    // 2. 方向盘：前 50% 时间保持，后 50% 线性回正
    if (progress < 0.5) {
        m_currentCommand.steering = m_lastValidCommand.steering;
    } else {
        double steeringProgress = (progress - 0.5) * 2.0;
        m_currentCommand.steering = m_lastValidCommand.steering * (1.0 - steeringProgress);
    }

    // 3. 刹车：平滑增加压力（如果原先没刹车，则从 0 增加到 0.3 作为保护）
    double baseBrake = m_lastValidCommand.brake;
    double targetBrake = std::max(baseBrake, 0.3);
    m_currentCommand.brake = baseBrake + (targetBrake - baseBrake) * progress;

    LOG_CTRL_TRACE("SURVIVAL_INTERPOLATION: progress={:.2f}, steer={:.2f}, throttle={:.2f}, brake={:.2f}",
                   progress, m_currentCommand.steering, m_currentCommand.throttle, m_currentCommand.brake);

    // 应用到硬件
    try {
        applySteering(m_currentCommand.steering);
        applyThrottle(m_currentCommand.throttle);
        applyBrake(m_currentCommand.brake);
        // 档位保持不变
    } catch (...) {}

#ifdef ENABLE_CARLA
    applyControlToCarla();
#endif
}

void VehicleController::emergencyStop()
{
    LOG_ENTRY();

    std::lock_guard<std::mutex> lock(m_mutex);

    try {
        LOG_SAFE_CRITICAL("EMERGENCY STOP TRIGGERED! (manual trigger)");
        LOG_SAFE_CRITICAL("Current state: {}, RemoteControl: {}, DrivingMode: {}",
                         static_cast<int>(m_state),
                         m_remoteControlEnabled,
                         static_cast<int>(m_drivingMode));

        m_state = SafetyState::SAFE_STOP;

        // 记录当前命令状态
        LOG_SAFE_INFO("Overriding current command: steer={:.2f}, throttle={:.2f}, brake={:.2f}, gear={}",
                     m_currentCommand.steering, m_currentCommand.throttle,
                     m_currentCommand.brake, m_currentCommand.gear);

        applySafeStopLocked();

#ifdef ENABLE_CARLA
        try {
            applyControlToCarla();  // 确保 CARLA 收到停车指令
            LOG_CARLA_INFO("CARLA notified of EMERGENCY STOP");
        } catch (const std::exception& e) {
            LOG_CARLA_ERROR_WITH_CODE(Code::CARLA_APPLY_CONTROL_FAILED,
                "Failed to notify CARLA of EMERGENCY_STOP: {}", e.what());
        }
#endif

        LOG_SAFE_CRITICAL("Emergency Stop completed. Vehicle is in SAFE_STOP state.");
        LOG_EXIT_WITH_VALUE("emergency_stop_applied");

    } catch (const std::exception& e) {
        LOG_SAFE_CRITICAL("CRITICAL: Exception during emergencyStop: {}", e.what());
        LOG_EXIT_WITH_VALUE("exception");
        throw;
    }
}

void VehicleController::setRemoteControlEnabled(bool enabled, const std::string& sessionId)
{
    LOG_ENTRY();
    std::lock_guard<std::mutex> lock(m_mutex);

    bool oldValue = m_remoteControlEnabled;
    
    if (enabled) {
        // 尝试开启远驾：检查是否已有活跃会话
        if (!m_activeSessionId.empty() && m_activeSessionId != sessionId) {
            LOG_CTRL_ERROR("Cannot enable remote control: active session {} exists (requested by {})", 
                          m_activeSessionId, sessionId);
            return;
        }
        m_activeSessionId = sessionId;
        m_remoteControlEnabled = true;
        LOG_CTRL_INFO("REMOTE_CONTROL ENABLED by system (session: {})", sessionId);
    } else {
        // 尝试关闭远驾：检查 sessionId 是否匹配
        if (!m_activeSessionId.empty() && !sessionId.empty() && m_activeSessionId != sessionId) {
            LOG_CTRL_WARN("Ignoring disable request from mismatching session: {} (active: {})", 
                          sessionId, m_activeSessionId);
            return;
        }
        m_remoteControlEnabled = false;
        m_activeSessionId.clear();
        LOG_CTRL_INFO("REMOTE_CONTROL DISABLED by system");
    }

    LOG_EXIT_WITH_VALUE("enabled={}, changed={}", enabled, (oldValue != enabled));
}

std::string VehicleController::getActiveSessionId() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeSessionId;
}

bool VehicleController::isRemoteControlEnabled() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    LOG_CTRL_TRACE("isRemoteControlEnabled called: {}", m_remoteControlEnabled);
    return m_remoteControlEnabled;
}

void VehicleController::setDrivingMode(DrivingMode mode)
{
    LOG_ENTRY();
    std::lock_guard<std::mutex> lock(m_mutex);

    DrivingMode oldMode = m_drivingMode;
    m_drivingMode = mode;

    std::string modeStr;
    std::string oldModeStr;
    switch (mode) {
        case DrivingMode::REMOTE_CONTROL: modeStr = "Remote Control"; break;
        case DrivingMode::AUTONOMOUS:  modeStr = "Autonomous"; break;
        case DrivingMode::REMOTE_DRIVING: modeStr = "Remote Driving"; break;
        default: modeStr = "Unknown"; break;
    }
    switch (oldMode) {
        case DrivingMode::REMOTE_CONTROL: oldModeStr = "Remote Control"; break;
        case DrivingMode::AUTONOMOUS:  oldModeStr = "Autonomous"; break;
        case DrivingMode::REMOTE_DRIVING: oldModeStr = "Remote Driving"; break;
        default: oldModeStr = "Unknown"; break;
    }

    LOG_CTRL_INFO("Driving mode changed: {} ({} -> {}) | Session: {} | RemoteEnabled: {}", 
                  modeStr, oldModeStr, modeStr, m_activeSessionId, m_remoteControlEnabled);
    LOG_EXIT();
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
    LOG_ENTRY();
    std::lock_guard<std::mutex> lock(m_mutex);

    double oldRtt = m_networkRtt;
    m_networkRtt = rtt;

    LOG_NET_TRACE("Network RTT updated: {:.2f}ms -> {:.2f}ms", oldRtt, rtt);

    // 记录网络质量警告
    if (rtt > 300.0) {
        LOG_NET_ERROR_WITH_CODE(Code::NET_RTT_CRITICAL,
            "Network RTT CRITICAL: {:.2f}ms (>300ms threshold)", rtt);
    } else if (rtt > 150.0) {
        LOG_NET_WARN_WITH_CODE(Code::NET_RTT_HIGH,
            "Network RTT HIGH: {:.2f}ms (>150ms threshold)", rtt);
    }

    LOG_EXIT_WITH_VALUE("rtt={:.2f}", rtt);
}

double VehicleController::getNetworkQuality() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_networkRtt;
}

void VehicleController::setSweepActive(bool active)
{
    LOG_ENTRY();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sweepActive != active) {
        m_sweepActive = active;
        LOG_CTRL_INFO("Sweep status changed: {}", active ? "ACTIVE" : "INACTIVE");
    }
    LOG_EXIT_WITH_VALUE("active={}", active);
}

bool VehicleController::isSweepActive() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sweepActive;
}

#ifdef ENABLE_CARLA
void VehicleController::setCarlaVehicle(boost::shared_ptr<carla::client::Actor> actor)
{
    LOG_ENTRY();
    std::lock_guard<std::mutex> lock(m_mutex);

    if (actor) {
        m_carlaActor = actor;
        LOG_CARLA_INFO("CARLA Vehicle attached: ID={}, TypeId={}",
                      actor->GetId(), actor->GetTypeId());
    } else {
        m_carlaActor.reset();
        LOG_CARLA_WARN("CARLA Vehicle detached");
    }

    LOG_EXIT();
}
#endif

void VehicleController::applySafeStopLocked()
{
    LOG_SAFE_TRACE("applySafeStopLocked called");

    // 在 SAFE_STOP 中强制停车：方向置 0，油门 0，刹车 1，档位 0
    m_currentCommand.steering = 0.0;
    m_currentCommand.throttle = 0.0;
    m_currentCommand.brake = 1.0;
    m_currentCommand.gear = 0;

    LOG_SAFE_INFO("SAFE_STOP applied: steer=0.0, throttle=0.0, brake=1.0, gear=N(0)");

    try {
        applySteering(0.0);
        applyThrottle(0.0);
        applyBrake(1.0);
        applyGear(0);
        LOG_SAFE_INFO("SAFE_STOP commands sent to hardware interfaces");
    } catch (const std::exception& e) {
        LOG_SAFE_ERROR_WITH_CODE(Code::CTRL_HW_INTERFACE_ERROR,
            "Failed to apply SAFE_STOP to hardware: {}", e.what());
        // 继续执行，因为已经设置了软件层面的状态
    }
}

void VehicleController::applySteering(double angle)
{
    // TODO: 实现实际的方向盘控制
    // 例如：通过 CAN 总线、串口、ROS2 话题等
    LOG_CTRL_TRACE("applySteering called: angle={:.3f} (TODO: hardware implementation)", angle);
}

void VehicleController::applyThrottle(double value)
{
    // TODO: 实现实际的油门控制
    LOG_CTRL_TRACE("applyThrottle called: value={:.3f} (TODO: hardware implementation)", value);
}

void VehicleController::applyBrake(double value)
{
    // TODO: 实现实际的刹车控制
    LOG_CTRL_TRACE("applyBrake called: value={:.3f} (TODO: hardware implementation)", value);
}

void VehicleController::applyGear(int gear)
{
    // TODO: 实现实际的档位控制
    // 实现硬件逻辑时可以参考这里：
    /*
    std::string gearStr = (gear == -1 ? "R" : (gear == 0 ? "N" : (gear == 1 ? "D" : "P")));
    LOG_DEBUG("Applying Gear: {} ({})", gear, gearStr);
    */
    LOG_CTRL_TRACE("applyGear called: gear={} (TODO: hardware implementation)", gear);
}

#ifdef ENABLE_CARLA
void VehicleController::applyControlToCarla()
{
    LOG_CARLA_TRACE("applyControlToCarla called");

    if (!m_carlaActor) {
        LOG_CARLA_WARN("CARLA actor not set, skipping ApplyControl");
        return;
    }

    try {
        // 尝试转换为 Vehicle
        auto vehicle = boost::static_pointer_cast<carla::client::Vehicle>(m_carlaActor);

        if (!vehicle) {
            LOG_CARLA_ERROR_WITH_CODE(Code::CARLA_ACTOR_NOT_FOUND,
                "Failed to cast actor to Vehicle type");
            return;
        }

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

        LOG_CARLA_TRACE("Applying CARLA control: steer={:.3f}, throttle={:.3f}, brake={:.3f}, gear={}, reverse={}",
                       control.steer, control.throttle, control.brake, control.gear, control.reverse);

        vehicle->ApplyControl(control);

        // ==================== T5: 硬件执行埋点 (Latency Model) ====================
        auto t5_now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        LOG_LATENCY("E2E_LATENCY_REPORT: vin={}, seq={}, T1_Client_Capture={}, T5_Hardware_Apply={}, Total_E2E={}ms",
                   "TODO_VIN", m_lastSeq, m_lastCmdTimestamp, t5_now, (t5_now - m_lastCmdTimestamp));

    } catch (const std::exception& e) {
        LOG_CARLA_ERROR_WITH_CODE(Code::CARLA_APPLY_CONTROL_FAILED,
            "CARLA ApplyControl failed: {}. Entering SAFE_STOP immediately!", e.what());

        m_state = SafetyState::SAFE_STOP;
        applySafeStopLocked();

        LOG_EXIT();
        return;
    }

    LOG_CARLA_TRACE("applyControlToCarla completed successfully");
}
#endif
