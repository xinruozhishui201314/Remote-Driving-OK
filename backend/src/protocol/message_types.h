#pragma once
#ifndef TELEOP_PROTOCOL_MESSAGE_TYPES_H
#define TELEOP_PROTOCOL_MESSAGE_TYPES_H

#include <cstdint>
#include <string>

namespace teleop::protocol {

/**
 * 消息类型枚举 - 支持扩展
 */
enum class MessageType : uint8_t {
    DRIVE_CMD = 1,   // 驾驶员控制指令
    MODE_CMD = 2,      // 模式控制指令
    ESTOP_CMD = 3,    // 远程急停指令
    HEARTBEAT = 4,      // 心跳/保活
    TELEMETRY = 5,      // 遥测数据上报
    FAULT_REPORT = 6,    // 故障上报
    SESSION_ACK = 7,    // 会话应答
    NAVIGATION_CMD = 8 // 导航指令（预留）
};

/**
 * 协议版本号
 */
constexpr uint32_t PROTOCOL_VERSION = 1;

/**
 * 消息基础结构 - 所有消息的通用字段
 */
struct MessageHeader {
    uint32_t schemaVersion = PROTOCOL_VERSION;
    std::string vin;               // 车辆唯一标识
    uint32_t sessionId;           // 会话 ID (UUID)
    uint32_t seq;                 // 序列号（单调递增）
    uint64_t timestampMs;         // 时间戳（客户端时间，毫秒）
    uint32_t nonce;                // 随机数（可选，用于防重放）
};

/**
 * 控制指令
 */
struct DriveCommand {
    MessageHeader header;
    float throttle = 0.0f;      // 油门/加速踏板（0.0~1.0）
    float brake = 0.0f;          // 刹车（0.0~1.0）
    float steering = 0.0f;       // 方向（-1.0~1.0）
    int8_t gear = 1;              // 档位: -1=倒档, 0=空档, 1=前进, 2=驻车
    uint16_t maxSpeed = 60;       // 最大限速（km/h，0=无限制）
    
    // 验证标志
    bool deadman = false;        // 死手保护（必须为 true 才可输出油门）
    bool emergency = false;    // 紧急模式
    bool autopilot = false;     // 自动驾驶
};

/**
 * 模式控制指令
 */
struct ModeCommand {
    MessageHeader header;
    bool enable = false;        // 启用远程驾驶
    bool stop = false;           // 停止远程驾驶
    bool lightsOn = false;      // 打开灯光
    bool hornOn = false;        // 鸣叭
    bool sweeperOn = false;     // 启动扫地功能
};

/**
 * 远程急停指令
 */
struct EStopCommand {
    MessageHeader header;
    bool trigger = true;         // 触发急停
    uint64_t triggerId;         // 紧停 ID（用于审计）
};

/**
 * 心跳消息
 */
struct Heartbeat {
    MessageHeader header;
    uint32_t serverTimestampMs;   // 服务器时间戳回显
};

/**
 * 遥测数据上报（周期性，10-20Hz）
 */
struct TelemetryData {
    MessageHeader header;
    // 车辆状态
    float speed = 0.0f;            // 车速 km/h
    int16_t steering = 0;          // 方向角度（度，左负右正）
    int8_t gear = 0;             // 档位
    // 电量/电源
    float batteryVoltage = 0.0f;    // 电池电压 V
    float fuelLevel = 0.0f;        // 燃料百分比 0-100
    // 控制器状态
    int8_t controllerState = 0;    // 控制器状态
    // 作业状态
    bool sweeperRunning = false;    // 扫盘运行中
    bool pumpRunning = false;       // 水泵运行中
    bool brushRunning = false;     // 刷盘运行中
    bool nozzleRunning = false;    // 喷嘴运行中
    // 温度和性能
    float cpuUsage = 0.0f;         // CPU 使用率 %
    float memoryUsage = 0.0f;     // 内存使用率 %
    float temperature = 0.0f;     // 温度 ℃
    uint32_t encoderDroppedFrames = 0; // 编码器丢帧
    // 网络统计
    uint32_t rtt = 0;             // RTT ms
    uint32_t packetLossRate = 0;      // 丢包率%
    uint32_t jitter = 0;           // 抖动 ms
};

/**
 * 故障码定义
 */
struct FaultCode {
    std::string code;               // 故障码，如 "TEL-1001"
    std::string severity;              // INFO/WARN/ERROR/CRITICAL
    std::string domain;               // TELEOP/NETWORK/VEHICLE_CTRL/CAMERA/POWER/SWEEPER/SECURITY
    bool latch = false;             // 是否锁存（需人工清除）
    std::string recommendedAction;     // 推荐操作
};

/**
 * 故障事件上报
 */
struct FaultEvent {
    MessageHeader header;       // 关联会话
    FaultCode fault;              // 故障码
    std::string message;           // 故障描述
    std::string payload;          // 结构化数据（JSON）
};

/**
 * 会话配置
 */
struct SessionConfig {
    uint32_t watchDogTimeoutMs = 500;    // 看狗超时（毫秒）
    uint32_t commandFrequency = 30;      // 控制频率（Hz）
    uint32_t telemetryFrequency = 15;  // 遥测频率（Hz）
    uint32_t safeStopDurationMs = 5000; // 安全停车持续时间
    uint32_t idleTimeoutMs = 30000;    // 空闲超时
    bool enableSafeStopOnDisconnect = true; // 断链是否触发安全停车
    bool requireDeadmanForThrottle = true;   // 油门控制
};

} // namespace teleop::protocol

#endif // TELEOP_PROTOCOL_MESSAGE_TYPES_H
