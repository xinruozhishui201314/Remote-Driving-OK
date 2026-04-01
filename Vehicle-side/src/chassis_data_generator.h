#ifndef CHASSIS_DATA_GENERATOR_H
#define CHASSIS_DATA_GENERATOR_H

#include <string>
#include <map>
#include <functional>
#include <memory>
#include "vehicle_controller.h"

/**
 * @brief 底盘数据生成器
 * 支持灵活扩展的数据字段生成机制
 */
class ChassisDataGenerator {
public:
    // 数据值类型
    struct DataValue {
        enum Type { DOUBLE, INT, STRING };
        Type type;
        union {
            double d;
            int i;
        };
        std::string s;
        
        DataValue() : type(DOUBLE), d(0.0) {}
        DataValue(double v) : type(DOUBLE), d(v) {}
        DataValue(int v) : type(INT), i(v) {}
        DataValue(const std::string& v) : type(STRING), s(v) {}
    };
    
    // 数据生成器函数类型
    using GeneratorFunc = std::function<DataValue(const VehicleController::ControlCommand&, double dt)>;
    
    ChassisDataGenerator();
    ~ChassisDataGenerator() = default;
    
    /** 注册数据生成器 */
    void registerGenerator(const std::string& fieldName, GeneratorFunc generator);
    
    /** 生成所有启用的数据字段 */
    std::map<std::string, DataValue> generateAll(const VehicleController::ControlCommand& cmd, double dt);
    
    /** 生成单个字段 */
    DataValue generateField(const std::string& fieldName, const VehicleController::ControlCommand& cmd, double dt);
    
    /** 检查字段是否已注册 */
    bool isFieldRegistered(const std::string& fieldName) const;

private:
    std::map<std::string, GeneratorFunc> m_generators;
    
    // 模拟数据状态
    double m_simulatedSpeed = 0.0;
    double m_simulatedOdometer = 0.0;
    double m_simulatedVoltage = 48.0;
    double m_simulatedCurrent = 0.0;
    double m_simulatedTemperature = 25.0;
    double m_simulatedBattery = 100.0;
    
    // 内置生成器实现
    DataValue generateSpeed(const VehicleController::ControlCommand& cmd, double dt);
    DataValue generateGear(const VehicleController::ControlCommand& cmd, double dt);
    DataValue generateSteering(const VehicleController::ControlCommand& cmd, double dt);
    DataValue generateThrottle(const VehicleController::ControlCommand& cmd, double dt);
    DataValue generateBrake(const VehicleController::ControlCommand& cmd, double dt);
    DataValue generateBattery(const VehicleController::ControlCommand& cmd, double dt);
    DataValue generateOdometer(const VehicleController::ControlCommand& cmd, double dt);
    DataValue generateVoltage(const VehicleController::ControlCommand& cmd, double dt);
    DataValue generateCurrent(const VehicleController::ControlCommand& cmd, double dt);
    DataValue generateTemperature(const VehicleController::ControlCommand& cmd, double dt);
};

#endif // CHASSIS_DATA_GENERATOR_H
