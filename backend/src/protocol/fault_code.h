#pragma once
#ifndef TELEOP_PROTOCOL_FAULT_CODE_H
#define TELEOP_PROTOCOL_FAULT_CODE_H

#include <string>
#include <map>

namespace teleop::protocol {

/**
 * 故障严重级别
 */
enum class FaultSeverity {
    INFO = 0,
    WARN = 1,
    ERROR = 2,
    CRITICAL = 3
};

/**
 * 故障域
 */
enum class FaultDomain {
    TELEOP = 0,   // 远程驾驶核心功能
    NETWORK = 1,  // 网络相关问题
    VEHICLE_CTRL = 2,  // 车辆控制
    CAMERA = 3,  // 摄像相关
    POWER = 4,    // 电源相关
    SWEEPER = 5,   // 扫地车功能
    SECURITY = 6   // 安全相关
};

/**
 * 故障码基础结构
 */
struct FaultCode {
    std::string code;           // 故障码，如 "TEL-1001"
    std::string name;           // 简短描述
    FaultSeverity severity = INFO; // 严重级别
    FaultDomain domain = TELEOP;   // 故障域
    bool latch = false;             // 是否锁存（需人工清除）
    std::string message;          // 详细描述
    std::string recommendedAction;   // 推荐操作
    std::string payloadExample;   // 示例 JSON payload
};

/**
 * 故障码管理器
 */
class FaultCodeManager {
public:
    /**
     * 获取故障码定义
     * @param code 故障码
     * @return 故障码定义
     */
    static const FaultCode& get(const std::string& code);

    /**
     * 检查故障码是否存在
     * @param code 故障码
     * @return 是否存在
     */
    static bool exists(const std::string& code);

    /**
     * 解析故障码名称和严重级别
     * @param faultStream 故障码字符串（如 "TEL-1001:WARN"）
     * @return FaultCode
     */
    static FaultCode parse(const std::string& faultStream);

    /**
     * 获取所有故障码列表
     */
    static std::vector<FaultCode> getAllFaultCodes();

private:
    // 故障码注册表
    static std::map<std::string, FaultCode> faultCodeMap_;

    /**
     * 注册故障码
     * @param faultCode 故障码定义
     */
    static void registerFaultCode(const FaultCode& faultCode);

    // 解析字符串 "CODE:SEVERITY:DOMAIN:..." 格式
    static void parseFaultString(const std::string& faultStream);

private:
    FaultCodeManager() = default;
};

// 预定义的故障码定义
namespace FaultCodes {

// TELEOP 域
static const FaultCode TEL_1001{"TEL-1001", "Video stream not received", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::TELEOP, false, "等待视频流"};
static const FaultCode TEL_1002{"TEL-1002", "Audio not connected", teleop::protocol::FaultSeverity::WARN, teleop::protocol::FaultDomain::TELEOP, false, "音频通道未连接"};
static const FaultCode TEL_1003{"TEL-1003", "Control channel lost", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::TELEOP, false, "控制通道断开"};
static const FaultCode TEL_1004{"TEL-1004", "Session expired", teleop::protocol::FaultSeverity::WARN, teleop::protocol::FaultDomain::TELEOP, false, "会话过期"};

// NETWORK 域
static const FaultCode NET_2001{"NET-2001", "High latency detected", teleop::protocol::FaultSeverity::WARN, teleop::protocol::FaultDomain::NETWORK, false, "高延迟检测"};
static const FaultCode NET_2002{"NET_2002", "Packet loss rate high", teleop::protocol::FaultSeverity::WARN, teleop::protocol::FaultDomain::NETWORK, false, "丢包率高"};
static const FaultCode NET_2003{"NET-2003", "Connection unstable", teleop::protocol::FaultSeverity::INFO, teleop::protocol::FaultDomain::NETWORK, false, "连接不稳定"};
static const FaultCode NET_2004{"NET-2004", "DNS resolution failed", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::NETWORK, false, "DNS解析失败"};

// VEHICLE_CTRL 域
static const FaultCode VEH_3001{"VEH-3001", "Control command rejected", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::VEHICLE_CTRL, false, "控制指令被拒绝（参数错误）"};
static const FaultCode VEH_3002{"VEH-3002", "Vehicle not responding", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::VEHICLE_CTRL, false, "车端无响应"};
static const FaultCode VEH_3003{"VEH-3003", "Invalid command format", teleop::protocol::FaultSeverity::WARN, teleop::protocol::FaultDomain::VEHICLE_CTRL, false, "指令格式错误"};
static const FaultCode VEH_3004{"VEH-3004", "Safe stop triggered", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::VEHICLE_CTRL, false, "安全停车已触发"};
static const FaultCode VEH_3005{"VEH-3005", "CAN bus error", teleop::protocol::FaultSeverity::WARN, teleop::protocol::FaultDomain::VEHICLE_CTRL, false, "CAN总线错误"};

// CAMERA 域
static const FaultCode CAM_4001{"CAM-4001", "Camera offline", teleop::protocol::FaultSeverity::ERROR, teleop::FaultDomain::CAMERA, false, "摄像头离线"};
static const FaultCode CAM_4002{"CAM-4002", "Encoding failed", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::CAMERA, false, "编码失败"};
static const FaultCode CAM_4003{"CAM-4003", "Bitrate too high", teleop::protocol::FaultSeverity::WARN, teleop::protocol::FaultDomain::CAMERA, false, "码率过高"};

// POWER 域
static const FaultCode PWR_5001{"PWR-5001", "Battery low", teleop::protocol::FaultSeverity::WARN, teleop::protocol::FaultDomain::POWER, false, "电量低"};
static const FaultCode PWR_5002{"PWR-5002", "Voltage unstable", teleop::FaultSeverity::WARN, teleop::protocol::FaultDomain::POWER, false, "电压不稳定"};
static const FaultCode PWR_5003{"PWR-5003", "Inverter fault", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::POWER, false, "逆变器故障"};

// SWEEPER 域
static const FaultCode SWP_6001{"SWP-6001", "Sweeper offline", teleop::protocol::FaultSeverity::WARN, teleop::protocol::FaultDomain::SWEEPER, false, "扫地系统离线"};
static const FaultCode SWP_6002{"SWP-6002", "Nozzle blocked", teleop::protocol::FaultSeverity::WARN, teleop::protocol::FaultDomain::SWEEPER, false, "喷嘴堵塞"};
static const FaultCode SWP_6003{"SWP-6003", "Brush worn", teleop::protocol::FaultSeverity::INFO, teleop::protocol::FaultDomain::SWEEPER, false, "刷盘磨损"};

// SECURITY 域
static const FaultCode SEC_7001{"SEC-7001", "Invalid signature", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::SECURITY, true, "签名验证失败"};
static const FaultCode SEC_7002{"SEC-7002", "Replay detected", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::SECURITY, true, "重放攻击检测"};
static const FaultCode SEC_7003{"SEC-7003", "Frequency overload", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::SECURITY, true, "频率超限攻击检测"};
static const FaultCode SEC_7004{"SEC-7004", "Unauthorized access", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::SECURITY, true, "未授权访问"};
static const FaultCode SEC_7005{"SEC-7005", "Token expired", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::SECURITY, true, "Token 已过期"};
static const FaultCode SEC_7006{"SEC-7006", "Invalid session", teleop::protocol::FaultSeverity::ERROR, teleop::protocol::FaultDomain::SECURITY, true, "会话无效或已过期"};

} // namespace fault_codes

} // namespace teleop::protocol

#endif // TELEOP_PROTOCOL_FAULT_CODE_H
