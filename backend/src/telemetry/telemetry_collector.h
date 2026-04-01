#pragma once
#ifndef TELEOP_TELEMETRY_TELEMETRY_COLLECTOR_H
#define TELEOP_TELEMETRY_TELEMETRY_COLLECTOR_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>

#include "telemetry_data.h"

namespace teleop::telemetry {

/**
 * 遥测收集器和缓存
 * 
 * 负责收集、存储和查询遥测数据
 */
class TelemetryCollector {
public:
    /**
     * 添加遥测数据
     */
    void addData(const ExtendedTelemetryData::MessageHeader& data);

    /**
     * 获取最新的遥测数据（只返回，不缓存）
     */
    std::string getLatest(const std::string& vin) const;

    /**
     * 获取指定 VIN 的历史遥测数据
     * @param vin VIN 码
     * @param count 返回数量
     */
    std::vector<ExtendedTelemetryData> getHistory(const std::string& vin,
                                                     uint32_t count) const;

    /**
     * 清除指定 VIN 的历史数据
     */
    void clearHistory(const std::string& vin);

    /**
     * 计算特定 VIN 的统计信息
     * @param vin VIN 筛
     * @return 统计信息文本
     */
    std::string calculateStatistics(const std::string& vin) const;

    /**
     * 检查是否需要触发故障
     * @param vin VIN 筛
     * @return 是否需要触发故障，触发故障码引用
     */
    template<typename FaultCodeType>
    bool shouldTriggerFault(const std::string& vin) const;

    /**
     * 清空所有数据
     */
    void clearAll();

private:
    std::map<std::string, std::deque<ExtendedTelemetryData>> dataStore_;
    mutable std::mutex mutex;  // 保护 dataStore_ 并发访问
    
    // 辅助裁剪函数：检查是否超出历史数据
    void trimHistory(const std::string& vin, size_t maxAgeSec);
};

} // namespace teleop::telemetry

#endif // TELEOP_TELEMETRY_TELEMETRY_COLLECTOR_H
