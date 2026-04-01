#pragma once
#ifndef TELEOP_TELEMETRY_TELEMETRY_DATA_H
#define TELEOP_TELEMETRY_TELEMETRY_DATA_H

#include "protocol/message_types.h"

namespace teleop::telemetry {

/**
 * 遥测数据常量
 */
namespace TelemetryConstants {
    // 报告频率
    constexpr uint32_t TELEMETRY_HZ_MIN = 10;
    constexpr uint32_t TELEMETRY_HZ_MAX = 20;
    constexpr uint32_t TELEMETRY_HZ_DEFAULT = 15;
    
    // 默认值
    constexpr float DEFAULT_SPEED_KMH = 0.0f;
    constexpr int8_t DEFAULT_STEERING_DEGREE = 0;
    constexpr int8_t DEFAULT_GEAR = 0;
    constexpr float DEFAULT_VOLTAGE_V = 12.6f;
    constexpr uint32_t DEFAULT_CPU_USAGE_PCT = 0.0f;
    uint32_t DEFAULT_PACKET_LOSS_RATE_PCT = 0;
    uint32_t DEFAULT_LATENCY_MS = 100;
    
    // 警报阈值（用于触发故障）
    constexpr float HIGH_CPU_USAGE_THRESHOLD = 80.0f;
    constexpr float HIGH_PACKET_LOSS_THRESHOLD = 10.0f;
};

/**
 * 遥测信息类
 */
class TelemetryData {
public:
    // 基本信息
    std::string vin;
    uint32_t sessionId;
    
    // 车辆状态
    float speed = DEFAULT_SPEED_KMH;
    int16_t steeringDegree = DEFAULT_STEERING_DEGREE;
    int8_t gear = DEFAULT_GEAR;
    float batteryVoltage = DEFAULT_VOLTAGE_V;
    float fuelLevel = DEFAULT_FUEL_LEVEL;
    
    // 控制器状态
    int8_t controllerState = 0;
    
    // 作业系统状态
    bool sweeperRunning = false;
    bool pumpRunning = false;
    bool brushRunning = false;
    bool nozzleRunning = false;
    
    // 技术状态
    float cpuUsage = DEFAULT_CPU_USAGE_PCT;
    float memoryUsage = DEFAULT_CPU_USAGE_PCT;
    uint32_t encoderDroppedFrames = 0;
    
    // 网络统计
    uint32_t rtt = DEFAULT_LATENCY_MS;
    uint32_t packetLossRate = DEFAULT_PACKET_LOSS_RATE;
    const std::string sourceIp;
    
    /**
     * 获取完整遥测状态
     */
    void updateFromSystem();
    
    /**
     * 从 JSON 字符串解析遥测数据
     */
    void parseFromJson(const std::string& jsonStr);
    
    /**
     * 调试 JSON 格式化
     */
    std::string toJson() const;
    
    /**
     * 检查是否需要触发故障
     */
    bool shouldTriggerFault() const;
    
    /**
     * 获取故障码
     */
    template<typename FaultCodeType>
    FaultCodeType getFaultByDomain(uint8_t domain) const;

private:
    void checkSystemStatus();
};

/**
 * 遥测信息扩展（车端补充）
 */
class ExtendedTelemetryData : public TelemetryData {
public:
    // 车辆额外信息
    std::string vehicleModel;
    std::string vehicleYear;
    uint32_t odometerKm = 0;
    float distanceTraveled = 0.0f;
    
    // 环境信息
    float latitude = 0.0;
    float longitude = 0.0;
    float altitude = 0.0;
    
    float heading = 0.0f;       // 航向（度）
    float pitch = 0.0f;       // 俯仰（度）
    float roll = 0.0f;        // 侧倾（度）
    
    // 扫地车专用
    bool headlightsOn = false;
    bool taillLightsOn = false;
    bool hazardLightsOn = false;
    bool leftTurnSignal = false;
    bool rightTurnSignal = false;
    
    // 维护相关
    uint32_t serviceHours = 0;
    uint32_t lastServiceDate = 0;
    float lastServiceCost = 0.0f;
};

/**
 * 遥测收集器
 */
class TelemetryCollector {
public:
    /**
     * 添加遥测数据
     */
    void addData(const ExtendedTelemetryData::MessageHeader& data);
    
    /**
     * 获取最新的遥测数据
     */
    std::string getLatest(const std::string& vin) const;
    
    /**
     * 获取特定 VIN 的遥测历史
     */
    std::vector<ExtendedTelemetryData> getHistory(const std::string& vin, 
                                                     uint32_t count) const;
    
    /**
     * 清除指定 VIN 的历史数据
     */
    void clearHistory(const std::string& vin);
    
    /**
     * 遥测数据统计
     */
    void printStatistics(const std::string& vin) const;
    
    /**
     * 清空所有数据
     */
    void clearAll();
};

} // namespace teleop::telemetry

#endif // TELEOP_TELEMETRY_TELEMETRY_DATA_H
