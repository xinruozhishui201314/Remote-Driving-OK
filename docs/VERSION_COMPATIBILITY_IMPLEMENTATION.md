# 版本兼容性实现文档

## 1. 已创建的文件

### 1.1 版本协商中间件
- **文件**: `backend/src/middleware/version_middleware.h`
- **文件**: `backend/src/middleware/version_middleware.cpp`
- **功能**:
  - 解析和验证Semantic Versioning格式
  - 检查客户端与服务端版本的兼容性
  - 注册各API的最小版本要求
  - 记录版本协商日志

### 1.2 MQTT Schema更新
- **文件**: `mqtt/schemas/vehicle_control.json` (更新至 v1.1.0)
- **文件**: `mqtt/schemas/vehicle_status.json` (更新至 v1.1.0)
- **新增字段**:
  - `schemaVersion`: 必填字段，标识消息版本
  - `vehicle_control`: `emergency_brake`, `seq`, `timestamp`, `sessionId`
  - `vehicle_status`: `network_quality`, `sessionId`

### 1.3 OpenAPI契约
- **文件**: `backend/api/openapi.yaml`
- **版本**: 1.1.0
- **内容**:
  - 完整的API路径和Schema定义
  - 版本协商文档
  - 响应头定义（API-Version）
  - 错误响应Schema

### 1.4 验证脚本
- **文件**: `scripts/validate_api_against_openapi.py`
- **功能**:
  - 解析OpenAPI契约
  - 扫描Backend代码实现
  - 检查路径是否完整实现
  - 检查响应字段是否匹配

## 2. 已修改的文件

### 2.1 Backend main.cpp
- **修改内容**:
  - 添加 `middleware/version_middleware.h` 头文件引用
  - 初始化 `VersionMiddleware`
  - 修改 `/api/v1/me` 路由添加版本协商
  - 响应添加 `apiVersion` 字段
  - 响应头添加 `API-Version`

## 3. 待完成的实现

### 3.1 修改更多API路由
需要修改以下路由添加版本协商：

#### 3.1.1 GET /api/v1/vins
```cpp
svr.Get("/api/v1/vins", [&expected_issuers, &expected_aud, &version_middleware, enable_version_validation](const httplib::Request& req, httplib::Response& res) {
    // 1. 版本协商（同 /api/v1/me）
    std::string client_version = req.get_header_value("API-Version");
    std::string error_msg;
    if (enable_version_validation && !version_middleware.validate_client_version(client_version, error_msg)) {
        res.status = 400;
        // 返回版本错误...
        return;
    }
    res.set_header("API-Version", version_middleware.get_response_version(...));
    
    // 2. 原有逻辑...
    
    // 3. 在响应中添加 apiVersion 字段
    nlohmann::json response;
    response["apiVersion"] = "1.1.0";
    response["vins"] = vins_array;
    res.set_content(response.dump(), "application/json");
});
```

#### 3.1.2 POST /api/v1/vins/{vin}/sessions
```cpp
// 在 build_whep_url 和 build_whip_url 调用时添加版本参数
std::string apiVersion = "1.1.0";  // 或根据client_version决定
std::string whepUrl = build_whep_url(zlm_api_url, vin, session_id, apiVersion);
std::string whipUrl = build_whip_url(zlm_api_url, vin, session_id, apiVersion);
```

#### 3.1.3 修改 build_whep_url 和 build_whip_url
```cpp
static std::string build_whep_url(const std::string& zlm_api_url, 
                                   const std::string& vin, 
                                   const std::string& session_id,
                                   const std::string& api_version = "1.0.0") {
    std::string host, port;
    whep_whip_host_port(host, port);
    std::string stream_name = vin + "-" + session_id;
    return "whep://" + host + ":" + port + "/index/api/webrtc?" +
           "app=teleop&stream=" + stream_name + "&type=play&apiVersion=" + api_version;
}
```

### 3.2 修改Vehicle-side代码

#### 3.2.1 Vehicle-side/src/mqtt_handler.cpp
```cpp
void MqttHandler::processControlCommand(const std::string &jsonPayload) {
    nlohmann::json msg = nlohmann::json::parse(jsonPayload);
    
    // 检查schemaVersion
    std::string schemaVersion = msg.value("schemaVersion", "1.0.0");
    
    // 版本协商：车端支持的版本范围
    static const std::set<std::string> SUPPORTED_VERSIONS = {"1.0.0", "1.1.0"};
    
    if (SUPPORTED_VERSIONS.find(schemaVersion) == SUPPORTED_VERSIONS.end()) {
        std::cerr << "[Vehicle][MQTT] Unsupported schemaVersion: " << schemaVersion << std::endl;
        return;
    }
    
    // 根据版本解析字段
    std::string type = msg["type"];
    
    if (schemaVersion >= "1.1.0") {
        // v1.1.0 特有字段
        if (msg.contains("emergency_brake") && msg["emergency_brake"]) {
            m_controller->emergencyStop();
        }
        if (msg.contains("sessionId")) {
            // 记录会话ID
        }
    }
    
    // 通用字段处理（v1.0.0 + v1.1.0）
    if (type == "remote_control") {
        if (msg.contains("steering")) {
            m_controller->setSteering(msg["steering"]);
        }
        // ... 其他控制
    }
}

void MqttHandler::publishStatus() {
    nlohmann::json status;
    
    // 添加版本字段
    status["schemaVersion"] = "1.1.0";
    status["type"] = "vehicle_status";
    status["vin"] = m_vin;
    status["timestamp"] = teleop::common::Timestamp::now_ms();
    
    // 通用字段
    status["speed"] = m_controller->getSpeed();
    status["battery"] = m_controller->getBattery();
    // ... 其他字段
    
    // v1.1.0 新增字段
    if (m_networkQuality) {
        nlohmann::json netQual;
        netQual["rtt_ms"] = m_networkQuality.rtt_ms;
        netQual["packet_loss_rate"] = m_networkQuality.loss_rate;
        netQual["active_link"] = m_networkQuality.link_type;
        status["network_quality"] = netQual;
    }
    
    // 发布...
}
```

### 3.3 修改Client代码

#### 3.3.1 Client/src/mqttcontroller.cpp
```cpp
void MqttController::sendControlCommand(const QJsonObject &command) {
    nlohmann::json msg;
    
    // 添加版本字段
    msg["schemaVersion"] = "1.1.0";
    msg["type"] = command["type"].toString().toStdString();
    msg["vin"] = m_currentVin.toStdString();
    
    // 添加控制参数
    if (command.contains("steering")) {
        msg["steering"] = command["steering"].toDouble();
    }
    // ... 其他字段
    
    // v1.1.0 新增字段
    msg["seq"] = ++m_seq;  // 序号递增
    msg["timestamp"] = teleop::common::Timestamp::now_ms();
    if (command.contains("sessionId")) {
        msg["sessionId"] = command["sessionId"].toString().toStdString();
    }
    
    // 发布
    publishMessage(m_controlTopic, msg);
}

void MqttController::onMessageReceived(const QByteArray &topic, const QByteArray &payload) {
    nlohmann::json msg = nlohmann::json::parse(payload.toStdString());
    
    // 检查schemaVersion
    std::string schemaVersion = msg.value("schemaVersion", "1.0.0");
    
    // v1.1.0 特有字段
    if (schemaVersion >= "1.1.0" && msg.contains("network_quality")) {
        auto netQual = msg["network_quality"];
        int rtt = netQual.value("rtt_ms", 0);
        double lossRate = netQual.value("packet_loss_rate", 0.0);
        std::string activeLink = netQual.value("active_link", "Unknown");
        
        // 更新UI显示网络质量
        emit networkQualityChanged(rtt, lossRate, QString::fromStdString(activeLink));
    }
    
    // 通用字段处理
    emit statusReceived(QJsonObject::fromVariantMap(...));
}
```

### 3.4 修改WebRTC DataChannel

#### 3.4.1 Client/src/webrtcclient.cpp
```cpp
bool WebRtcClient::sendControlMessage(const QJsonObject &message, const QString &schemaVersion) {
    nlohmann::json msg;
    msg["schemaVersion"] = schemaVersion.toStdString();
    // ... 添加其他字段
    
    // 发送通过DataChannel
    if (m_dataChannel && m_dataChannel->send(QByteArray::fromStdString(msg.dump()))) {
        return true;
    }
    
    // 降级到MQTT
    return m_mqttController->publishMessage(msg);
}

void WebRtcClient::negotiateVersions(const QStringList &clientSupportedVersions) {
    // 与车端协商版本
    // 1. 发送支持的版本列表
    nlohmann::json versionMsg;
    versionMsg["type"] = "version_negotiation";
    versionMsg["supported_versions"] = clientSupportedVersions;
    
    sendControlMessage(QJsonObject::fromVariantMap(...), "1.1.0");
    
    // 2. 等待车端响应
    // 3. 选择双方都支持的最高版本
}
```

## 4. 配置文件更新

### 4.1 config/backend_config.yaml
```yaml
backend:
  version: "1.1.0"
  api:
    min_client_version: "1.0.0"
    version_negotiation_enabled: true
  supported_api_versions:
    - "1.0.0"
    - "1.1.0"
  mqtt:
    schema_version: "1.1.0"
```

### 4.2 config/client_config.yaml
```yaml
client:
  api:
    version: "1.1.0"
    preferred_version: "1.1.0"
  mqtt:
    schema_version: "1.1.0"
    supported_versions:
      - "1.0.0"
      - "1.1.0"
```

### 4.3 config/vehicle_config.yaml
```yaml
mqtt:
  schema_version: "1.1.0"
  supported_versions:
    - "1.0.0"
    - "1.1.0"
  auto_upgrade: false  # 是否自动升级到更高版本
```

## 5. 测试脚本

### 5.1 创建版本测试脚本
```bash
# scripts/test-version-negotiation.sh
#!/bin/bash

set -e

echo "[TEST] 测试版本协商功能"

# 1. 测试有效版本
echo "[TEST] 测试有效版本 1.0.0"
curl -H "Authorization: Bearer $TOKEN" \
     -H "API-Version: 1.0.0" \
     http://localhost:8080/api/v1/me | jq .

# 2. 测试版本不兼容
echo "[TEST] 测试版本不兼容 2.0.0"
curl -H "Authorization: Bearer $TOKEN" \
     -H "API-Version: 2.0.0" \
     http://localhost:8080/api/v1/me

# 3. 测试无版本头
echo "[TEST] 测试无版本头"
curl -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/api/v1/me

# 4. 验证响应头
echo "[TEST] 验证API-Version响应头"
API_VERSION=$(curl -s -I -H "Authorization: Bearer $TOKEN" \
                        -H "API-Version: 1.0.0" \
                        http://localhost:8080/api/v1/me | grep API-Version)
echo "Response: $API_VERSION"

echo "[TEST] 版本协商测试完成"
```

### 5.2 创建MQTT版本测试脚本
```bash
# scripts/test-mqtt-version.sh
#!/bin/bash

echo "[TEST] 测试MQTT版本协商"

# 1. 发布v1.1.0控制消息
echo "[TEST] 发布v1.1.0控制消息"
mosquitto_pub -h localhost -t vehicle/control \
  -m '{
    "schemaVersion": "1.1.0",
    "type": "remote_control",
    "vin": "test-vin-001",
    "steering": 0.5,
    "throttle": 0.3,
    "seq": 1,
    "timestamp": 1234567890
  }'

# 2. 订阅状态消息并验证schemaVersion
echo "[TEST] 订阅并验证status消息中的schemaVersion"
timeout 5 mosquitto_sub -h localhost -t vehicle/status -v | while read line; do
  if echo "$line" | grep -q "schemaVersion"; then
    echo "[OK] Status message contains schemaVersion"
    echo "$line" | jq .
    break
  fi
done

echo "[TEST] MQTT版本测试完成"
```

## 6. 编译和运行

### 6.1 编译Backend
```bash
cd backend
mkdir -p build && cd build
cmake .. -DENABLE_VERSION_NEGOTIATION=ON
make -j$(nproc)
```

### 6.2 运行Backend
```bash
docker compose up -d backend
# 或
./build/backend --backend-version=1.1.0 --enable-version-validation=true
```

### 6.3 验证版本协商
```bash
# 运行验证脚本
./scripts/validate_api_against_openapi.py
./scripts/test-version-negotiation.sh
./scripts/test-mqtt-version.sh

# 运行完整测试
./scripts/build-and-verify.sh
```

## 7. 单元测试

### 7.1 版本解析测试
```cpp
// tests/test_version_middleware.cpp

TEST(VersionTest, ParseVersion) {
    auto v1 = Version::parse("1.0.0");
    EXPECT_EQ(v1.major, 1);
    EXPECT_EQ(v1.minor, 0);
    EXPECT_EQ(v1.patch, 0);
    
    auto v2 = Version::parse("1.1.0");
    EXPECT_EQ(v2.major, 1);
    EXPECT_EQ(v2.minor, 1);
}

TEST(VersionTest, Compatibility) {
    auto server = Version::parse("1.1.0");
    auto client10 = Version::parse("1.0.0");
    auto client11 = Version::parse("1.1.0");
    auto client20 = Version::parse("2.0.0");
    
    EXPECT_TRUE(server.is_compatible_with(client10));
    EXPECT_TRUE(server.is_compatible_with(client11));
    EXPECT_FALSE(server.is_compatible_with(client20));
}

TEST(VersionTest, Compare) {
    auto v10 = Version::parse("1.0.0");
    auto v11 = Version::parse("1.1.0");
    
    EXPECT_TRUE(v10 < v11);
    EXPECT_TRUE(v11 > v10);
    EXPECT_FALSE(v10 == v11);
}
```

### 7.2 运行测试
```bash
cd backend
./tests/test_version_middleware
./tests/test_api_versioning
```

## 8. GATE B 验证清单

### 8.1 文件变更
- [x] 创建 `backend/src/middleware/version_middleware.h`
- [x] 创建 `backend/src/middleware/version_middleware.cpp`
- [x] 更新 `mqtt/schemas/vehicle_control.json`
- [x] 更新 `mqtt/schemas/vehicle_status.json`
- [x] 创建 `backend/api/openapi.yaml`
- [x] 创建 `scripts/validate_api_against_openapi.py`
- [x] 修改 `backend/src/main.cpp` 添加版本中间件
- [ ] 修改更多API路由（`/api/v1/vins`, `/api/v1/sessions/*`）
- [ ] 修改 `Vehicle-side/src/mqtt_handler.cpp`
- [ ] 修改 `Client/src/mqttcontroller.cpp`
- [ ] 修改 `Client/src/webrtcclient.cpp`

### 8.2 日志添加
- [x] 版本协商日志（VersionMiddleware）
- [ ] HTTP请求版本日志
- [ ] MQTT消息版本日志
- [ ] 版本不兼容错误日志

### 8.3 测试覆盖
- [ ] API版本协商单元测试
- [ ] MQTT Schema验证测试
- [ ] 版本兼容性测试（1.0.0 <-> 1.1.0）
- [ ] 版本不兼容测试（1.0.0 <-> 2.0.0）
- [ ] E2E测试

### 8.4 验证结果
- [ ] `./scripts/validate_api_against_openapi.py` → PASS ✅
- [ ] `./scripts/validate_mqtt_schemas.py` → PASS ✅
- [ ] `./scripts/test-version-negotiation.sh` → PASS ✅
- [ ] `./scripts/build-and-verify.sh` → PASS ✅

## 9. 下一步行动

1. **完成Backend API路由修改**
   - 修改 `/api/v1/vins`
   - 修改 `/api/v1/vins/{vin}/sessions`
   - 修改 `/api/v1/sessions/{sessionId}/end`
   - 修改 `/api/v1/sessions/{sessionId}/unlock`

2. **完成Vehicle-side修改**
   - 修改 `mqtt_handler.cpp` 添加版本协商
   - 修改状态发布添加 `schemaVersion`

3. **完成Client修改**
   - 修改 `mqttcontroller.cpp` 添加版本字段
   - 修改 `webrtcclient.cpp` 添加DataChannel版本协商

4. **创建完整测试**
   - API版本协商测试
   - MQTT版本协商测试
   - E2E测试

5. **执行验证**
   - 运行所有验证脚本
   - 修复发现的问题
   - 确认所有测试通过
