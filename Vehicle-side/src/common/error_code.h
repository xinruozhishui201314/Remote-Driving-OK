#pragma once
#ifndef VEHICLE_ERROR_CODE_H
#define VEHICLE_ERROR_CODE_H

#include <cstdint>
#include <string>

/**
 * @brief 车辆控制错误码定义
 *
 * 错误码格式: [类别-编号]
 * - 1xxx: 系统级错误
 * - 2xxx: MQTT通信错误
 * - 3xxx: 车辆控制错误
 * - 4xxx: 安全相关错误
 * - 5xxx: 网络/RTT相关错误
 * - 6xxx: CARLA仿真错误
 * - 7xxx: 安全校验错误 (防重放/时序)
 * - 8xxx: 配置错误
 * - 9xxx: 推流/ZLM错误
 */
namespace vehicle::error {

// 错误码定义
enum class Code : uint32_t {
    // ===== 1xxx: 系统级错误 =====
    SUCCESS                     = 0,
    SYS_INIT_FAILED            = 1001,  // 系统初始化失败
    SYS_INVALID_PARAM          = 1002,  // 无效参数
    SYS_NULL_POINTER           = 1003,  // 空指针
    SYS_TIMEOUT                = 1004,  // 操作超时
    SYS_OUT_OF_MEMORY          = 1005,  // 内存不足
    SYS_FILE_NOT_FOUND         = 1006,  // 文件未找到
    SYS_UNEXPECTED_EXCEPTION   = 1099,  // 未预期的异常

    // ===== 2xxx: MQTT通信错误 =====
    MQTT_CONN_FAILED           = 2001,  // MQTT连接失败
    MQTT_CONN_TIMEOUT          = 2002,  // MQTT连接超时
    MQTT_DISCONNECTED          = 2003,  // MQTT已断开
    MQTT_PUBLISH_FAILED        = 2004,  // MQTT发布失败
    MQTT_SUBSCRIBE_FAILED      = 2005,  // MQTT订阅失败
    MQTT_RECONNECTING          = 2006,  // MQTT正在重连
    MQTT_BROKER_UNREACHABLE    = 2007,  // MQTT Broker不可达

    // ===== 3xxx: 车辆控制错误 =====
    CTRL_INIT_FAILED           = 3001,  // 控制器初始化失败
    CTRL_INVALID_STEERING      = 3002,  // 无效的方向盘值
    CTRL_INVALID_THROTTLE      = 3003,  // 无效的油门值
    CTRL_INVALID_BRAKE         = 3004,  // 无效的刹车值
    CTRL_INVALID_GEAR          = 3005,  // 无效的档位值
    CTRL_HW_INTERFACE_ERROR    = 3006,  // 硬件接口错误
    CTRL_COMMAND_IGNORED       = 3007,  // 命令被忽略(未启用远驾)
    CTRL_STATE_INVALID         = 3008,  // 状态机处于无效状态

    // ===== 4xxx: 安全相关错误 =====
    SAFETY_WATCHDOG_TIMEOUT    = 4001,  // 看门狗超时
    SAFETY_EMERGENCY_TRIGGERED = 4002,  // 急停触发
    SAFETY_SAFE_STOP_ACTIVE    = 4003,  // 安全停车已激活
    SAFETY_NETWORK_DEGRADED    = 4004,  // 网络质量降级
    SAFETY_NETWORK_CRITICAL    = 4005,  // 网络严重降级

    // ===== 5xxx: 网络/RTT相关错误 =====
    NET_RTT_HIGH               = 5001,  // RTT过高 (>150ms)
    NET_RTT_CRITICAL           = 5002,  // RTT严重过高 (>300ms)
    NET_LATENCY_WARNING        = 5003,  // 网络延迟警告

    // ===== 6xxx: CARLA仿真错误 =====
    CARLA_CONN_FAILED          = 6001,  // CARLA连接失败
    CARLA_ACTOR_NOT_FOUND      = 6002,  // CARLA Actor未找到
    CARLA_APPLY_CONTROL_FAILED = 6003,  // CARLA控制应用失败
    CARLA_WORLD_NOT_AVAILABLE  = 6004,  // CARLA World不可用
    CARLA_TIMEOUT              = 6005,  // CARLA操作超时

    // ===== 7xxx: 安全校验错误 (防重放/时序) =====
    SEC_REPLAY_ATTACK          = 7001,  // 检测到重放攻击
    SEC_SEQ_INVALID            = 7002,  // 无效的序列号
    SEC_TIMESTAMP_EXPIRED      = 7003,  // 时间戳已过期
    SEC_TIMESTAMP_INVALID      = 7004,  // 无效的时间戳
    SEC_SIGNATURE_INVALID      = 7005,  // 无效的签名
    SEC_SESSION_MISMATCH       = 7006,  // 会话ID不匹配
    SEC_VIN_MISMATCH           = 7007,  // VIN不匹配

    // ===== 8xxx: 配置错误 =====
    CFG_LOAD_FAILED            = 8001,  // 配置加载失败
    CFG_INVALID_VALUE          = 8002,  // 配置值无效
    CFG_KEY_NOT_FOUND          = 8003,  // 配置键未找到
    CFG_ENV_VAR_MISSING        = 8004,  // 环境变量缺失

    // ===== 9xxx: 推流/ZLM错误 =====
    STREAM_SCRIPT_NOT_FOUND   = 9001,  // 推流脚本未找到
    STREAM_START_FAILED       = 9002,  // 推流启动失败
    STREAM_ALREADY_RUNNING    = 9003,  // 推流已在运行
    STREAM_NOT_RUNNING         = 9004,  // 推流未运行
    STREAM_RESTORE_FAILED     = 9005,  // 推流自动恢复失败
    ZLM_CONN_FAILED            = 9006,  // ZLM连接失败
    WHIP_CONN_FAILED           = 9007,  // WHIP连接失败
};

/**
 * @brief 获取错误码对应的描述字符串
 */
inline const char* toString(Code code) {
    switch (code) {
        // 1xxx: 系统级错误
        case Code::SUCCESS: return "SUCCESS";
        case Code::SYS_INIT_FAILED: return "SYS_INIT_FAILED: 系统初始化失败";
        case Code::SYS_INVALID_PARAM: return "SYS_INVALID_PARAM: 无效参数";
        case Code::SYS_NULL_POINTER: return "SYS_NULL_POINTER: 空指针";
        case Code::SYS_TIMEOUT: return "SYS_TIMEOUT: 操作超时";
        case Code::SYS_OUT_OF_MEMORY: return "SYS_OUT_OF_MEMORY: 内存不足";
        case Code::SYS_FILE_NOT_FOUND: return "SYS_FILE_NOT_FOUND: 文件未找到";
        case Code::SYS_UNEXPECTED_EXCEPTION: return "SYS_UNEXPECTED_EXCEPTION: 未预期的异常";

        // 2xxx: MQTT通信错误
        case Code::MQTT_CONN_FAILED: return "MQTT_CONN_FAILED: MQTT连接失败";
        case Code::MQTT_CONN_TIMEOUT: return "MQTT_CONN_TIMEOUT: MQTT连接超时";
        case Code::MQTT_DISCONNECTED: return "MQTT_DISCONNECTED: MQTT已断开";
        case Code::MQTT_PUBLISH_FAILED: return "MQTT_PUBLISH_FAILED: MQTT发布失败";
        case Code::MQTT_SUBSCRIBE_FAILED: return "MQTT_SUBSCRIBE_FAILED: MQTT订阅失败";
        case Code::MQTT_RECONNECTING: return "MQTT_RECONNECTING: MQTT正在重连";
        case Code::MQTT_BROKER_UNREACHABLE: return "MQTT_BROKER_UNREACHABLE: MQTT Broker不可达";

        // 3xxx: 车辆控制错误
        case Code::CTRL_INIT_FAILED: return "CTRL_INIT_FAILED: 控制器初始化失败";
        case Code::CTRL_INVALID_STEERING: return "CTRL_INVALID_STEERING: 无效的方向盘值";
        case Code::CTRL_INVALID_THROTTLE: return "CTRL_INVALID_THROTTLE: 无效的油门值";
        case Code::CTRL_INVALID_BRAKE: return "CTRL_INVALID_BRAKE: 无效的刹车值";
        case Code::CTRL_INVALID_GEAR: return "CTRL_INVALID_GEAR: 无效的档位值";
        case Code::CTRL_HW_INTERFACE_ERROR: return "CTRL_HW_INTERFACE_ERROR: 硬件接口错误";
        case Code::CTRL_COMMAND_IGNORED: return "CTRL_COMMAND_IGNORED: 命令被忽略(未启用远驾)";
        case Code::CTRL_STATE_INVALID: return "CTRL_STATE_INVALID: 状态机处于无效状态";

        // 4xxx: 安全相关错误
        case Code::SAFETY_WATCHDOG_TIMEOUT: return "SAFETY_WATCHDOG_TIMEOUT: 看门狗超时";
        case Code::SAFETY_EMERGENCY_TRIGGERED: return "SAFETY_EMERGENCY_TRIGGERED: 急停触发";
        case Code::SAFETY_SAFE_STOP_ACTIVE: return "SAFETY_SAFE_STOP_ACTIVE: 安全停车已激活";
        case Code::SAFETY_NETWORK_DEGRADED: return "SAFETY_NETWORK_DEGRADED: 网络质量降级";
        case Code::SAFETY_NETWORK_CRITICAL: return "SAFETY_NETWORK_CRITICAL: 网络严重降级";

        // 5xxx: 网络/RTT相关错误
        case Code::NET_RTT_HIGH: return "NET_RTT_HIGH: RTT过高 (>150ms)";
        case Code::NET_RTT_CRITICAL: return "NET_RTT_CRITICAL: RTT严重过高 (>300ms)";
        case Code::NET_LATENCY_WARNING: return "NET_LATENCY_WARNING: 网络延迟警告";

        // 6xxx: CARLA仿真错误
        case Code::CARLA_CONN_FAILED: return "CARLA_CONN_FAILED: CARLA连接失败";
        case Code::CARLA_ACTOR_NOT_FOUND: return "CARLA_ACTOR_NOT_FOUND: CARLA Actor未找到";
        case Code::CARLA_APPLY_CONTROL_FAILED: return "CARLA_APPLY_CONTROL_FAILED: CARLA控制应用失败";
        case Code::CARLA_WORLD_NOT_AVAILABLE: return "CARLA_WORLD_NOT_AVAILABLE: CARLA World不可用";
        case Code::CARLA_TIMEOUT: return "CARLA_TIMEOUT: CARLA操作超时";

        // 7xxx: 安全校验错误
        case Code::SEC_REPLAY_ATTACK: return "SEC_REPLAY_ATTACK: 检测到重放攻击";
        case Code::SEC_SEQ_INVALID: return "SEC_SEQ_INVALID: 无效的序列号";
        case Code::SEC_TIMESTAMP_EXPIRED: return "SEC_TIMESTAMP_EXPIRED: 时间戳已过期";
        case Code::SEC_TIMESTAMP_INVALID: return "SEC_TIMESTAMP_INVALID: 无效的时间戳";
        case Code::SEC_SIGNATURE_INVALID: return "SEC_SIGNATURE_INVALID: 无效的签名";
        case Code::SEC_SESSION_MISMATCH: return "SEC_SESSION_MISMATCH: 会话ID不匹配";
        case Code::SEC_VIN_MISMATCH: return "SEC_VIN_MISMATCH: VIN不匹配";

        // 8xxx: 配置错误
        case Code::CFG_LOAD_FAILED: return "CFG_LOAD_FAILED: 配置加载失败";
        case Code::CFG_INVALID_VALUE: return "CFG_INVALID_VALUE: 配置值无效";
        case Code::CFG_KEY_NOT_FOUND: return "CFG_KEY_NOT_FOUND: 配置键未找到";
        case Code::CFG_ENV_VAR_MISSING: return "CFG_ENV_VAR_MISSING: 环境变量缺失";

        // 9xxx: 推流/ZLM错误
        case Code::STREAM_SCRIPT_NOT_FOUND: return "STREAM_SCRIPT_NOT_FOUND: 推流脚本未找到";
        case Code::STREAM_START_FAILED: return "STREAM_START_FAILED: 推流启动失败";
        case Code::STREAM_ALREADY_RUNNING: return "STREAM_ALREADY_RUNNING: 推流已在运行";
        case Code::STREAM_NOT_RUNNING: return "STREAM_NOT_RUNNING: 推流未运行";
        case Code::STREAM_RESTORE_FAILED: return "STREAM_RESTORE_FAILED: 推流自动恢复失败";
        case Code::ZLM_CONN_FAILED: return "ZLM_CONN_FAILED: ZLM连接失败";
        case Code::WHIP_CONN_FAILED: return "WHIP_CONN_FAILED: WHIP连接失败";

        default: return "UNKNOWN_ERROR: 未知错误";
    }
}

/**
 * @brief 将错误码转换为整数
 */
inline uint32_t toInt(Code code) {
    return static_cast<uint32_t>(code);
}

/**
 * @brief Result结构体，用于返回操作结果和错误信息
 */
struct Result {
    bool success;
    Code code;
    std::string message;
    std::string detail;  // 详细错误信息，如异常内容

    Result() : success(true), code(Code::SUCCESS) {}

    Result(Code c, const std::string& msg = "", const std::string& detail_msg = "")
        : success(c == Code::SUCCESS)
        , code(c)
        , message(msg)
        , detail(detail_msg) {}

    static Result ok() { return Result(); }

    static Result error(Code c, const std::string& msg = "", const std::string& detail_msg = "") {
        return Result(c, msg, detail_msg);
    }

    const char* toString() const {
        return success ? "OK" : vehicle::error::toString(code);
    }
};

}  // namespace vehicle::error

// 便捷宏定义
#define ERR_OK() vehicle::error::Result::ok()
#define ERR_ERROR(code, msg, detail) vehicle::error::Result::error(vehicle::error::Code::code, msg, detail)

#endif  // VEHICLE_ERROR_CODE_H
