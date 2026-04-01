#include "chassis_data_generator.h"
#include <algorithm>
#include <cmath>
#include <functional>

ChassisDataGenerator::ChassisDataGenerator() {
    // 注册默认生成器
    registerGenerator("speed", [this](const VehicleController::ControlCommand& cmd, double dt) {
        return generateSpeed(cmd, dt);
    });
    
    registerGenerator("gear", [this](const VehicleController::ControlCommand& cmd, double dt) {
        return generateGear(cmd, dt);
    });
    
    registerGenerator("steering", [this](const VehicleController::ControlCommand& cmd, double dt) {
        return generateSteering(cmd, dt);
    });
    
    registerGenerator("throttle", [this](const VehicleController::ControlCommand& cmd, double dt) {
        return generateThrottle(cmd, dt);
    });
    
    registerGenerator("brake", [this](const VehicleController::ControlCommand& cmd, double dt) {
        return generateBrake(cmd, dt);
    });
    
    registerGenerator("battery", [this](const VehicleController::ControlCommand& cmd, double dt) {
        return generateBattery(cmd, dt);
    });
    
    registerGenerator("odometer", [this](const VehicleController::ControlCommand& cmd, double dt) {
        return generateOdometer(cmd, dt);
    });
    
    registerGenerator("voltage", [this](const VehicleController::ControlCommand& cmd, double dt) {
        return generateVoltage(cmd, dt);
    });
    
    registerGenerator("current", [this](const VehicleController::ControlCommand& cmd, double dt) {
        return generateCurrent(cmd, dt);
    });
    
    registerGenerator("temperature", [this](const VehicleController::ControlCommand& cmd, double dt) {
        return generateTemperature(cmd, dt);
    });
}

void ChassisDataGenerator::registerGenerator(const std::string& fieldName, GeneratorFunc generator) {
    m_generators[fieldName] = generator;
}

std::map<std::string, ChassisDataGenerator::DataValue> ChassisDataGenerator::generateAll(
    const VehicleController::ControlCommand& cmd, double dt) {
    std::map<std::string, DataValue> result;
    for (const auto& pair : m_generators) {
        result[pair.first] = pair.second(cmd, dt);
    }
    return result;
}

ChassisDataGenerator::DataValue ChassisDataGenerator::generateField(
    const std::string& fieldName, const VehicleController::ControlCommand& cmd, double dt) {
    auto it = m_generators.find(fieldName);
    if (it != m_generators.end()) {
        return it->second(cmd, dt);
    }
    return DataValue(0.0);
}

bool ChassisDataGenerator::isFieldRegistered(const std::string& fieldName) const {
    return m_generators.find(fieldName) != m_generators.end();
}

ChassisDataGenerator::DataValue ChassisDataGenerator::generateSpeed(
    const VehicleController::ControlCommand& cmd, double dt) {
    if (cmd.throttle > 0.01 && cmd.brake < 0.01) {
        m_simulatedSpeed = std::min(m_simulatedSpeed + cmd.throttle * 2.0 * dt, 30.0);
    } else if (cmd.brake > 0.01) {
        m_simulatedSpeed = std::max(m_simulatedSpeed - cmd.brake * 5.0 * dt, 0.0);
    } else {
        m_simulatedSpeed = std::max(m_simulatedSpeed - 0.5 * dt, 0.0);
    }
    return DataValue(m_simulatedSpeed);
}

ChassisDataGenerator::DataValue ChassisDataGenerator::generateGear(
    const VehicleController::ControlCommand& cmd, double dt) {
    (void)dt;
    return DataValue(cmd.gear);
}

ChassisDataGenerator::DataValue ChassisDataGenerator::generateSteering(
    const VehicleController::ControlCommand& cmd, double dt) {
    (void)dt;
    return DataValue(cmd.steering);
}

ChassisDataGenerator::DataValue ChassisDataGenerator::generateThrottle(
    const VehicleController::ControlCommand& cmd, double dt) {
    (void)dt;
    return DataValue(cmd.throttle);
}

ChassisDataGenerator::DataValue ChassisDataGenerator::generateBrake(
    const VehicleController::ControlCommand& cmd, double dt) {
    (void)dt;
    return DataValue(cmd.brake);
}

ChassisDataGenerator::DataValue ChassisDataGenerator::generateBattery(
    const VehicleController::ControlCommand& cmd, double dt) {
    (void)cmd;
    m_simulatedBattery = std::max(0.0, m_simulatedBattery - dt * 0.001);
    return DataValue(m_simulatedBattery);
}

ChassisDataGenerator::DataValue ChassisDataGenerator::generateOdometer(
    const VehicleController::ControlCommand& cmd, double dt) {
    (void)cmd;
    m_simulatedOdometer += m_simulatedSpeed * dt / 3600.0;
    return DataValue(m_simulatedOdometer);
}

ChassisDataGenerator::DataValue ChassisDataGenerator::generateVoltage(
    const VehicleController::ControlCommand& cmd, double dt) {
    (void)dt;
    m_simulatedVoltage = 48.0 + (cmd.throttle > 0.01 ? -2.0 : 0.0) + (m_simulatedSpeed > 10.0 ? -1.0 : 0.0);
    m_simulatedVoltage = std::max(42.0, std::min(52.0, m_simulatedVoltage));
    return DataValue(m_simulatedVoltage);
}

ChassisDataGenerator::DataValue ChassisDataGenerator::generateCurrent(
    const VehicleController::ControlCommand& cmd, double dt) {
    (void)dt;
    m_simulatedCurrent = cmd.throttle * 50.0 + m_simulatedSpeed * 2.0;
    m_simulatedCurrent = std::max(0.0, std::min(100.0, m_simulatedCurrent));
    return DataValue(m_simulatedCurrent);
}

ChassisDataGenerator::DataValue ChassisDataGenerator::generateTemperature(
    const VehicleController::ControlCommand& cmd, double dt) {
    (void)cmd;
    (void)dt;
    m_simulatedTemperature = 25.0 + m_simulatedCurrent * 0.1 + m_simulatedSpeed * 0.05;
    m_simulatedTemperature = std::max(20.0, std::min(60.0, m_simulatedTemperature));
    return DataValue(m_simulatedTemperature);
}
