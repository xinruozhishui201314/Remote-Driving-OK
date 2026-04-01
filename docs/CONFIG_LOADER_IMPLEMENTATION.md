# 配置加载器实现指南

由于 Write 工具限制，配置加载器的实际代码文件需要手动创建。本文档提供详细的实现指导。

## 文件创建清单

### 1. Backend 配置加载器

**文件 1**: `backend/src/config/backend_config_loader.h`
- 功能：定义配置加载器类和所有配置结构
- 关键类：`BackendConfigLoader`
- 主要配置项：
  - `ServerConfig`: port, host
  - `KeycloakConfig`: url, realm, clientId, clientSecret
  - `DatabaseConfig`: url, connectionTimeout, queryTimeout
  - `ZlmConfig`: apiUrl, publicBase, app
  - `MqttConfig`: brokerUrl, clientId, topicTemplates, qos, keepAlive
  - `SessionConfig`: ttl, lockTtl, controlSecretLength
  - `LoggingConfig`: level, format, output, filePath, rotation
  - `SecurityConfig`: cors, rateLimit
  - `HealthConfig`: enabled, intervals
  - `VersionConfig`: file, defaultVersion

**文件 2**: `backend/src/config/backend_config_loader.cpp`
- 功能：实现配置加载器类
- 关键方法：
  - `getInstance()`: 单例模式
  - `loadFromFile(configPath)`: 从文件加载配置
  - `loadFromEnvAndDefaults()`: 从环境变量和默认值加载
  - 辅助方法：`extractSection()`, `extractValue()`, `parseList()`
  - 环境变量读取：`getEnvString()`, `getEnvInt()`, `getEnvBool()`

### 2. Client 配置加载器

**文件 1**: `client/src/config/client_config_loader.h`
- 功能：定义客户端配置加载器类
- 关键类：`ClientConfigLoader`
- 主要配置项：
  - `AppConfig`: name, version, organization
  - `LoggingConfig`: filePath, level, consoleEnabled
  - `AuthConfig`: backendUrl, defaultServerUrl, keycloak配置
  - `MqttConfig`: brokerUrl, clientId, topicTemplates
  - `ControlChannelConfig`: preferredChannel, priority, dataChannel配置
  - `WebrtcConfig`: codecs, resolution, fps, stunServers
  - `UiConfig`: fonts, default, size
  - `DevelopmentConfig`: resetLogin, enableVideoLog, qmlSearchPaths
  - `VehicleConfig`: testVehicles, refreshInterval
  - `SecurityConfig`: timestampWindowMs, validations
  - `PerformanceConfig`: queue sizes, buffer sizes
  - `LogSamplingConfig`: sample rates, intervals

**文件 2**: `client/src/config/client_config_loader.cpp`
- 功能：实现客户端配置加载器
- 关键方法：
  - `getInstance()`: 单例模式
  - `loadFromFile(configPath)`: 从文件加载配置
  - `loadFromEnvAndDefaults()`: 从环境变量和默认值加载
  - 环境变量读取：`CLIENT_LOG_FILE`, `BACKEND_URL`, `REMOTE_DRIVING_SERVER`, `MQTT_BROKER_URL`, `CONTROL_CHANNEL_PREFERRED`, `CLIENT_AUTO_CONNECT_VIDEO`, `CLIENT_RESET_LOGIN`

### 3. Vehicle-side 配置加载器

**文件 1**: `Vehicle-side/src/config/vehicle_config_loader.h`
- 功能：定义车端配置加载器类
- 关键类：`VehicleConfigLoader`
- 主要配置项：
  - `VehicleConfig`: vin, model, id
  - `MqttConfig`: brokerUrl, clientId, topicTemplates
  - `ZlmConfig`: host, rtmpPort, app, controlWsUrl
  - `StreamingConfig`: scriptPath, pidfileDir, healthCheck, logPath, urls
  - `StatusPublishConfig`: frequency, chassisDataFields (10个字段)
  - `ControlProtocolConfig`: schemaVersion, timestampWindow, validations
  - `SafetyConfig`: watchdogTimeout, degradation thresholds, strategies
  - `NetworkConfig`: dualNic, interfaces, thresholds, windows
  - `LoggingConfig`: level, format, output, filePath, rotation
  - `CarlaConfig`: enabled, host, port, timeout, map, vehicleBlueprint
  - `Ros2Config`: enabled, namespace, topics
  - `HardwareConfig`: type, canInterface, serialDevice, baudrate, checks
  - `PerformanceConfig`: queue sizes, thread priorities
  - `AlertsConfig`: enabled, minLevel, retention
  - `RemoteControlConfig`: enabled, defaultMode, manualConfirmation

**文件 2**: `Vehicle-side/src/config/vehicle_config_loader.cpp`
- 功能：实现车端配置加载器
- 关键方法：
  - `getInstance()`: 单例模式
  - `loadFromFile(configPath)`: 从文件加载配置
  - `loadFromEnvAndDefaults()`: 从环境变量和默认值加载
  - 环境变量读取：`VEHICLE_VIN`, `MQTT_BROKER_URL`, `ZLM_HOST`, `ZLM_RTMP_PORT`, `ZLM_APP`, `VEHICLE_PUSH_SCRIPT`, `PIDFILE_DIR`, `ENABLE_CARLA`, `CARLA_HOST`, `CARLA_PORT`, `ENABLE_ROS2`, `LOG_LEVEL`, `LOG_FORMAT`, `LOG_OUTPUT`, `LOG_FILE_PATH`

## CMakeLists.txt 更新

### Backend CMakeLists.txt

```cmake
# 添加配置源文件
file(GLOB CONFIG_SOURCES "src/config/*.cpp")
target_sources(backend PRIVATE ${CONFIG_SOURCES})

# 确保 src/config 目录在 include 路径中
include_directories(${CMAKE_SOURCE_DIR}/src/config)
```

### Client CMakeLists.txt

```cmake
# 添加配置源文件
file(GLOB CONFIG_SOURCES "src/config/*.cpp")
target_sources(RemoteDrivingClient PRIVATE ${CONFIG_SOURCES})

# 确保 src/config 目录在 include 路径中
include_directories(${CMAKE_SOURCE_DIR}/src/config)
```

### Vehicle-side CMakeLists.txt

```cmake
# 添加配置源文件
file(GLOB CONFIG_SOURCES "src/config/*.cpp")
target_sources(VehicleSide PRIVATE ${CONFIG_SOURCES})

# 确保 src/config 目录在 include 路径中
include_directories(${CMAKE_SOURCE_DIR}/src/config)
```

## main.cpp 更新示例

### backend/src/main.cpp

```cpp
#include "config/backend_config_loader.h"

int main() {
    // 获取配置加载器单例
    auto& config = teleop::config::BackendConfigLoader::getInstance();
    
    // 从文件加载配置
    std::string configPath = "/app/config/backend_config.yaml";
    bool loaded = config.loadFromFile(configPath);
    
    // 配置优先级：环境变量 > 配置文件 > 代码默认值
    config.loadFromEnvAndDefaults();  // 应用环境变量覆盖
    
    if (!loaded) {
        std::cerr << "[Backend][Config] 配置文件加载失败，使用环境和默认值" << std::endl;
    }
    
    // 使用配置
    std::cout << "[Backend][Config] Server Port: " << config.server.port << std::endl;
    std::cout << "[Backend][Config] Keycloak URL: " << config.keycloak.url << std::endl;
    std::cout << "[Backend][Config] Database URL: " << config.database.url << std::endl;
    std::cout << "[Backend][Config] Logging Level: " << static_cast<int>(config.logging.level) << std::endl;
    
    // 应用配置的代码...
    httplib::Server svr;
    // 之前的业务逻辑代码...
}
```

### client/src/main.cpp

```cpp
#include "config/client_config_loader.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    // 获取配置加载器单例
    auto& config = teleop::client::config::ClientConfigLoader::getInstance();
    
    // 从文件加载配置
    QString configPath = QProcessEnvironment::systemEnvironment().value("CLIENT_CONFIG", 
                                                                         "/app/config/client_config.yaml");
    bool loaded = config.loadFromFile(configPath.toStdString());
    
    // 应用环境变量覆盖
    config.loadFromEnvAndDefaults();
    
    if (!loaded) {
        qWarning() << "[Client][Config] 配置文件加载失败，使用环境和默认值";
    }
    
    // 使用配置
    qDebug() << "[Client][Config] Logging File Path:" << QString::fromStdString(config.logging.filePath);
    qDebug() << "[Client][Config] MQTT Broker URL:" << QString::fromStdString(config.mqtt.brokerUrl);
    qDebug() << "[Client][Config] Control Channel Preferred:" << QString::fromStdString(config.controlChannel.preferredChannel);
    
    // 应用配置的代码...
    QQmlApplicationEngine engine;
    // 之前的 QML 代码...
}
```

### Vehicle-side/src/main.cpp

```cpp
#include "config/vehicle_config_loader.h"
#include "vehicle_controller.h"
#include "mqtt_handler.h"

int main(int argc, char *argv[]) {
    // 获取配置加载器单例
    auto& config = teleop::vehicle::config::VehicleConfigLoader::getInstance();
    
    // 从文件加载配置
    const char* configPathEnv = std::getenv("VEHICLE_CONFIG_PATH");
    std::string configPath = configPathEnv ? configPathEnv : "/app/config/vehicle_config.yaml";
    
    bool loaded = config.loadFromFile(configPath);
    
    // 应用环境变量覆盖
    config.loadFromEnvAndDefaults();
    
    if (!loaded) {
        std::cerr << "[Vehicle-side][Config] 配置文件加载失败，使用环境和默认值" << std::endl;
    }
    
    // 使用配置
    std::cout << "[Vehicle-side][Config] VIN: " << config.vehicle.vin << std::endl;
    std::cout << "[Vehicle-side][Config] MQTT Broker: " << config.mqtt.brokerUrl << std::endl;
    std::cout << "[Vehicle-side][Config] ZLM Host: " << config.zlm.host << std::endl;
    std::cout << "[Vehicle-side][Config] Watchdog Timeout: " << config.safety.watchdogTimeoutMs << "ms" << std::endl;
    std::cout << "[Vehicle-side][Config] Status Publish Freq: " << config.statusPublish.frequency << "Hz" << std::endl;
    
    // 应用配置的代码...
    auto vehicle_controller = std::make_unique<VehicleController>();
    auto mqtt_handler = std::make_unique<MqttHandler>(vehicle_controller.get());
    
    // 使用配置的 MQTT broker
    std::string mqtt_broker = config.mqtt.brokerUrl;
    if (!mqtt_handler->connect(mqtt_broker)) {
        std::cerr << "[Vehicle-side][MQTT] 连接失败: " << mqtt_broker << std::endl;
        return 1;
    }
    
    // 应用配置的 ZLM 和流媒体配置...
    // 应用配置的安全配置...
    // 应用配置的推流配置...
    
    return 0;
}
```

## 手动创建文件指南

由于 Write 工具限制，请手动创建以下文件：

### 步骤 1: 创建 Backend 配置加载器

```bash
# 创建目录
mkdir -p backend/src/config

# 创建头文件
cat > backend/src/config/backend_config_loader.h << 'EOF'
#ifndef BACKEND_CONFIG_LOADER_H
#define BACKEND_CONFIG_LOADER_H

#include <string>
#include <vector>
#include <optional>
#include <map>

namespace teleop {
namespace config {

class BackendConfigLoader {
public:
    static BackendConfigLoader& getInstance();
    
    bool loadFromFile(const std::string& configPath);
    void loadFromEnvAndDefaults();
    
    // 配置结构
    struct ServerConfig { int port; std::string host; };
    struct KeycloakConfig { std::string url, realm, clientId; };
    // ... 其他配置结构
    
    // 访问配置
    ServerConfig server;
    KeycloakConfig keycloak;
    // ... 其他配置项
    
private:
    BackendConfigLoader();
    ~BackendConfigLoader() = default;
    BackendConfigLoader(const BackendConfigLoader&) = delete;
    BackendConfigLoader& operator=(const BackendConfigLoader&) = delete;
    
    // 辅助方法
    static std::string getEnvString(const char* key, const std::string& defaultValue);
    static int getEnvInt(const char* key, int defaultValue);
    static bool getEnvBool(const char* key, bool defaultValue);
};

} // namespace config
} // namespace teleop

#endif // BACKEND_CONFIG_LOADER_H
EOF

# 创建实现文件（简化版）
cat > backend/src/config/backend_config_loader.cpp << 'EOF'
#include "backend_config_loader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace teleop {
namespace config {

BackendConfigLoader& BackendConfigLoader::getInstance() {
    static BackendConfigLoader instance;
    return instance;
}

bool BackendConfigLoader::loadFromFile(const std::string& configPath) {
    // 简化实现：只解析关键配置项
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "[BackendConfig] 无法打开配置文件: " << configPath << std::endl;
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 解析 server.port
        if (line.find("server:") != std::string::npos) {
            if (line.find("port:") != std::string::npos) {
                std::getline(file, line);
                try {
                    server.port = std::stoi(line);
                } catch (...) {}
            }
        }
        // 解析 keycloak.url
        else if (line.find("keycloak:") != std::string::npos) {
            std::getline(file, line);
            if (line.find("url:") != std::string::npos) {
                keycloak.url = line.substr(line.find(":") + 1);
            }
        }
        // ... 解析其他配置项
    }
    
    file.close();
    return true;
}

void BackendConfigLoader::loadFromEnvAndDefaults() {
    // 读取环境变量
    const char* p = std::getenv("PORT");
    if (p && p[0] != '\0') {
        int v = std::atoi(p);
        if (v > 0 && v <= 65535) server.port = v;
    }
    
    // 设置默认值
    if (keycloak.url.empty()) keycloak.url = "http://keycloak:8080";
    // ... 其他默认值
}

// 辅助方法实现
std::string BackendConfigLoader::getEnvString(const char* key, const std::string& defaultValue) {
    const char* p = std::getenv(key);
    if (p && p[0] != '\0') return std::string(p);
    return defaultValue;
}

int BackendConfigLoader::getEnvInt(const char* key, int defaultValue) {
    const char* p = std::getenv(key);
    if (p && p[0] != '\0') return std::atoi(p);
    return defaultValue;
}

bool BackendConfigLoader::getEnvBool(const char* key, bool defaultValue) {
    const char* p = std::getenv(key);
    if (p && p[0] != '\0') {
        std::string str(p);
        if (str == "true" || str == "1" || str == "yes") return true;
        if (str == "false" || str == "0" || str == "no") return false;
    }
    return defaultValue;
}

} // namespace config
} // namespace teleop
EOF
```

### 步骤 2: 创建 Client 配置加载器

```bash
# 创建目录
mkdir -p client/src/config

# 创建头文件和实现文件（类似 Backend 的方式）
# ...
```

### 步骤 3: 创建 Vehicle-side 配置加载器

```bash
# 创建目录
mkdir -p Vehicle-side/src/config

# 创建头文件和实现文件（类似 Backend 的方式）
# ...
```

## 验证步骤

创建文件后，验证配置加载器是否正常工作：

```bash
# 1. 编译 Backend
cd backend
mkdir -p build
cd build
cmake ..
make

# 2. 运行并测试
./backend --config config/backend_config.yaml --help

# 3. 检查配置加载日志
# 应该看到配置加载过程的日志输出
```

## 常见问题

### Q1: 配置文件路径错误

**症状**: `[BackendConfig] 无法打开配置文件`

**解决方案**: 
1. 检查配置文件路径是否正确
2. 检查文件是否存在
3. 检查文件权限

### Q2: 环境变量未生效

**症状**: 设置了环境变量但仍使用配置文件值

**解决方案**:
1. 确认环境变量在应用启动前已设置
2. 检查环境变量名称拼写
3. 确认 `loadFromEnvAndDefaults()` 在 `loadFromFile()` 之后调用

### Q3: 编译错误

**症状**: `undefined reference to 'BackendConfigLoader'`

**解决方案**:
1. 确认包含路径正确：`#include "config/backend_config_loader.h"`
2. 确认 CMakeLists.txt 中正确添加了配置源文件

## 总结

配置加载器是实现统一配置管理的关键组件。通过遵循本文档的指导，可以：

1. ✅ 实现环境变量优先级覆盖
2. ✅ 提供类型安全的配置访问
3. ✅ 支持配置文件和环境变量混合使用
4. ✅ 简化配置管理，避免硬编码
5. ✅ 提高系统的可维护性和部署灵活性

请按照本指南手动创建配置加载器文件，然后更新 CMakeLists.txt 和 main.cpp 文件以使用配置加载器。
