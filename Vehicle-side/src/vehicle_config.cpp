#include "vehicle_config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <iomanip>

VehicleConfig& VehicleConfig::getInstance() {
    static VehicleConfig instance;
    return instance;
}

VehicleConfig::VehicleConfig() {
    setDefaults();
}

void VehicleConfig::setDefaults() {
    m_statusPublishFrequency = 50;  // 默认50Hz
    
    // 默认字段配置
    m_chassisDataFields.clear();
    
    ChassisDataConfig speedField;
    speedField.name = "speed";
    speedField.type = "double";
    speedField.enabled = true;
    speedField.min_value = 0.0;
    speedField.max_value = 200.0;
    m_chassisDataFields.push_back(speedField);
    
    ChassisDataConfig gearField;
    gearField.name = "gear";
    gearField.type = "int";
    gearField.enabled = true;
    gearField.min_value = -1.0;
    gearField.max_value = 1.0;
    m_chassisDataFields.push_back(gearField);
    
    ChassisDataConfig steeringField;
    steeringField.name = "steering";
    steeringField.type = "double";
    steeringField.enabled = true;
    steeringField.min_value = -1.0;
    steeringField.max_value = 1.0;
    m_chassisDataFields.push_back(steeringField);
    
    ChassisDataConfig throttleField;
    throttleField.name = "throttle";
    throttleField.type = "double";
    throttleField.enabled = true;
    throttleField.min_value = 0.0;
    throttleField.max_value = 1.0;
    m_chassisDataFields.push_back(throttleField);
    
    ChassisDataConfig brakeField;
    brakeField.name = "brake";
    brakeField.type = "double";
    brakeField.enabled = true;
    brakeField.min_value = 0.0;
    brakeField.max_value = 1.0;
    m_chassisDataFields.push_back(brakeField);
    
    ChassisDataConfig batteryField;
    batteryField.name = "battery";
    batteryField.type = "double";
    batteryField.enabled = true;
    batteryField.min_value = 0.0;
    batteryField.max_value = 100.0;
    m_chassisDataFields.push_back(batteryField);
    
    ChassisDataConfig odometerField;
    odometerField.name = "odometer";
    odometerField.type = "double";
    odometerField.enabled = true;
    odometerField.min_value = 0.0;
    odometerField.max_value = 999999.0;
    m_chassisDataFields.push_back(odometerField);
    
    ChassisDataConfig voltageField;
    voltageField.name = "voltage";
    voltageField.type = "double";
    voltageField.enabled = true;
    voltageField.min_value = 0.0;
    voltageField.max_value = 100.0;
    m_chassisDataFields.push_back(voltageField);
    
    ChassisDataConfig currentField;
    currentField.name = "current";
    currentField.type = "double";
    currentField.enabled = true;
    currentField.min_value = 0.0;
    currentField.max_value = 200.0;
    m_chassisDataFields.push_back(currentField);
    
    ChassisDataConfig temperatureField;
    temperatureField.name = "temperature";
    temperatureField.type = "double";
    temperatureField.enabled = true;
    temperatureField.min_value = -40.0;
    temperatureField.max_value = 100.0;
    m_chassisDataFields.push_back(temperatureField);
    
    // 构建索引映射
    for (size_t i = 0; i < m_chassisDataFields.size(); ++i) {
        m_fieldIndexMap[m_chassisDataFields[i].name] = i;
    }
}

bool VehicleConfig::loadFromFile(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "[Vehicle-side][Config] cannot open config file " << configPath << std::endl;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    // 简单的JSON解析（仅支持基本结构）
    // 查找 status_publish_frequency
    size_t freqPos = content.find("\"status_publish_frequency\"");
    if (freqPos != std::string::npos) {
        size_t colonPos = content.find(':', freqPos);
        if (colonPos != std::string::npos) {
            size_t valueStart = content.find_first_of("0123456789", colonPos);
            if (valueStart != std::string::npos) {
                size_t valueEnd = content.find_first_not_of("0123456789", valueStart);
                std::string freqStr = content.substr(valueStart, valueEnd - valueStart);
                m_statusPublishFrequency = std::stoi(freqStr);
                std::cout << "[Vehicle-side][Config] status_publish_frequency from file: " << m_statusPublishFrequency << " Hz" << std::endl;
            }
        }
    }
    
    // 查找 chassis_data_fields
    size_t fieldsPos = content.find("\"chassis_data_fields\"");
    if (fieldsPos != std::string::npos) {
        size_t arrayStart = content.find('[', fieldsPos);
        if (arrayStart != std::string::npos) {
            size_t arrayEnd = content.find(']', arrayStart);
            if (arrayEnd != std::string::npos) {
                std::string fieldsStr = content.substr(arrayStart, arrayEnd - arrayStart + 1);
                parseChassisDataFields(fieldsStr);
            }
        }
    }
    
    return true;
}

void VehicleConfig::parseChassisDataFields(const std::string& jsonStr) {
    // 简单解析，查找字段名和enabled状态
    // 这里使用简单的字符串匹配，实际项目中建议使用JSON库
    // 示例格式: [{"name":"speed","enabled":true},...]
    
    // 对于每个已知字段，检查是否在JSON中被禁用
    for (auto& field : m_chassisDataFields) {
        std::string searchPattern = "\"name\":\"" + field.name + "\"";
        size_t namePos = jsonStr.find(searchPattern);
        if (namePos != std::string::npos) {
            // 查找enabled字段
            size_t enabledPos = jsonStr.find("\"enabled\"", namePos);
            if (enabledPos != std::string::npos && enabledPos < namePos + 100) {
                size_t colonPos = jsonStr.find(':', enabledPos);
                if (colonPos != std::string::npos) {
                    size_t valueStart = colonPos + 1;
                    while (valueStart < jsonStr.length() && (jsonStr[valueStart] == ' ' || jsonStr[valueStart] == '\t')) {
                        valueStart++;
                    }
                    if (valueStart < jsonStr.length()) {
                        if (jsonStr.substr(valueStart, 4) == "true") {
                            field.enabled = true;
                        } else if (jsonStr.substr(valueStart, 5) == "false") {
                            field.enabled = false;
                        }
                    }
                }
            }
        }
    }
}


bool VehicleConfig::isFieldEnabled(const std::string& fieldName) const {
    auto it = m_fieldIndexMap.find(fieldName);
    if (it != m_fieldIndexMap.end()) {
        return m_chassisDataFields[it->second].enabled;
    }
    return false;
}

const VehicleConfig::ChassisDataConfig* VehicleConfig::getFieldConfig(const std::string& fieldName) const {
    auto it = m_fieldIndexMap.find(fieldName);
    if (it != m_fieldIndexMap.end()) {
        return &m_chassisDataFields[it->second];
    }
    return nullptr;
}
