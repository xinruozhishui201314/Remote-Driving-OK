#include "telemetry_collector.h"

namespace teleoptelemetry {

TelemetryCollector::TelemetryCollector() {}

void TelemetryCollector::addData(const ExtendedTelemetryData::MessageHeader& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& history = dataStore_[data.vin];
    history.push_front(data);
    
    // 保持历史数据在合理范围内（最多10分钟或1000条）
    const size_t maxAgeSec = 600;  // 10分钟
    const size_t maxHistorySize = 1000;
    
    while (history.size() > maxHistorySize) {
        history.pop_front();
    }
    
    // 裁查是否需要触发高温故障
    if (data.cpuUsage > 80.0f && !data.vin.empty()) {
        // TODO: 触发 CPU 高温故障
    }
}

std::string TelemetryCollector::getLatest(const std::string& vin) const {
    if (vin.empty() || dataStore_.find(vin) == dataStore_.end()) {
        // 返回空 JSON
        std::string empty_result = "{\"header\":{\"vin\":\"\"}";
        return empty_result;
    }
    
    const auto& history = dataStore_.at(vin);
    auto latest = history.back();
    
    auto jsonData = latest.toJson().substr(20); // 跳过 "header":
    
    return jsonData;
}

std::vector<ExtendedTelemetryData> TelemetryCollector::getHistory(const std::string& vin, uint32_t count) const {
    std::vector<ExtendedTelemetryData> result;
    
    if (vin.empty() || dataStore_.find(vin) == dataStore_.end()) {
        return result;
    }
    
    const auto& history = dataStore_.at(vin);
    
    size_t startIdx = std::max(0, (int)history.size() - count);
    result.resize(std::min(history.size(), startIdx));
    
    size_t idx = 0;
    for (auto it = history.rbegin(), prev = history.rend(); it != history.rend(); ++it) {
        if (idx >= count) break;
        result[idx++] = *prev;
    }
    
    return result;
}

void TelemetryCollector::clearHistory(const std::string& vin) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = dataStore_.find(vin);
    if (it != dataStore_.end()) {
        dataStore_[vin].clear();
    }
}

std::string TelemetryCollector::calculateStatistics(const std::string& vin) const {
    if (vin.empty() || dataStore_.find(vin) == dataStore_.end()) {
        return "";
    }
    
    const auto& history = dataStore_.at(vin);
    if (history.empty()) {
        return std::string("No telemetry data for VIN: ") + vin;
    }
    
    uint32_t totalCount = history.size();
    std::map<std::string, uint32_t> counts;

    for (const auto& item : history) {
        counts["total"]++;
        if (item.sweeperRunning) counts["sweepers_on"]++;
        if (item.pumpRunning) counts["pumps_on"]++;
        if (item.brushRunning) counts["brushes_on"]++;
        if (item.nozzleRunning) counts["nozzles_on"]++;
        if (item.headlightsOn) counts["headlights_on"]++;
        if (item.taillLightsOn) counts["taillights_on"]++;
        
        counts["total_frames"] = item.encoderDroppedFrames;
        counts["packet_loss"] = item.packetLossRate;
    }
    
    std::string stats = "{";
    bool first = true;
    
    for (const auto& entry : counts) {
        if (!first) {
            first = false;
        }
        stats += ", " + entry.first + ":" + entry.second;
    }
    
    stats += ", total_records:" + std::to_string(totalCount);
    stats += ", current_cpu:" + std::to_string(history.back().cpuUsage) + "%";
    stats += ", max_latency:" + std::to_string(
        std::max_element(history, [](const auto& a, const auto& b) {
            return a.rtt > b.rtt;
        }).value_or(0);
    
    return stats;
}

bool TelemetryCollector::shouldTriggerFault(const std::string& vin) const {
    if (vin.empty()) {
        return false;
    }
    
    const auto& history = dataStore_.at(vin);
    if (history.empty()) {
        return false;
    }
    
    const auto& latest = history.back();
    
    // 检查告警条件
    return latest.cpuUsage > 80.0f ||
           latest.packetLossRate > 5.0f ||
           latest.encoderDroppedFrames > 100;
}

void TelemetryCollector::clearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    dataStore_.clear();
}

} // namespace teleoptelemetry
