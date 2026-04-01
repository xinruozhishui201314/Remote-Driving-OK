#ifndef VEHICLE_CONFIG_H
#define VEHICLE_CONFIG_H

#include <string>
#include <map>
#include <vector>
#include <memory>

/**
 * @brief 车辆配置管理类
 * 支持从JSON配置文件读取配置
 */
class VehicleConfig {
public:
    struct ChassisDataConfig {
        bool enabled = true;  // 是否启用该字段
        std::string name;     // 字段名称
        std::string type;     // 字段类型（double/int/string）
        double min_value = 0.0;
        double max_value = 100.0;
    };

    static VehicleConfig& getInstance();
    
    /** 从JSON文件加载配置 */
    bool loadFromFile(const std::string& configPath);
    
    /** 获取状态发布频率（Hz） */
    int getStatusPublishFrequency() const { return m_statusPublishFrequency; }
    
    /** 获取状态发布间隔（毫秒） */
    int getStatusPublishIntervalMs() const { return 1000 / m_statusPublishFrequency; }
    
    /** 获取底盘数据字段配置 */
    const std::vector<ChassisDataConfig>& getChassisDataFields() const { return m_chassisDataFields; }
    
    /** 检查字段是否启用 */
    bool isFieldEnabled(const std::string& fieldName) const;
    
    /** 获取字段配置 */
    const ChassisDataConfig* getFieldConfig(const std::string& fieldName) const;

private:
    VehicleConfig();
    ~VehicleConfig() = default;
    VehicleConfig(const VehicleConfig&) = delete;
    VehicleConfig& operator=(const VehicleConfig&) = delete;
    
    void setDefaults();
    void parseChassisDataFields(const std::string& jsonStr);
    
    int m_statusPublishFrequency = 50;  // 默认50Hz
    std::vector<ChassisDataConfig> m_chassisDataFields;
    std::map<std::string, size_t> m_fieldIndexMap;  // 字段名到索引的映射
};

#endif // VEHICLE_CONFIG_H
