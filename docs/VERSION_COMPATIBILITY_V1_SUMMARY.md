# 版本兼容性V1实现总结

## Executive Summary

已成功实现版本兼容性框架的基础部分，包括：

✅ **已完成**：
- 版本协商中间件（VersionMiddleware）完整实现
- MQTT Schema升级至v1.1.0（包含schemaVersion字段）
- OpenAPI 3.0契约文档创建
- HTTP API版本协商框架（main.cpp已部分修改）
- API验证脚本框架

⏳ **待完成**：
- 完成所有HTTP API路由的版本协商
- Vehicle-side和Client的版本协商实现
- 完整的单元测试和集成测试
- E2E验证

---

## 1. 已创建的核心文件

### 1.1 版本协商中间件

#### 文件：`backend/src/middleware/version_middleware.h`
**关键类和功能**：
- `Version`结构体：语义化版本解析和比较
- `VersionMiddleware`类：版本协商中间件
- `VersionNegotiationResult`结构体：协商结果
- 辅助函数：`extract_version_from_header`, `build_version_header`

**关键方法**：
```cpp
class VersionMiddleware {
public:
    explicit VersionMiddleware(const std::string& current_backend_version = "1.1.0");
    
    // 验证客户端版本
    bool validate_client_version(const std::string& client_version, std::string& error_msg);
    
    // 获取响应版本
    std::string get_response_version(const Version& client_version) const;
    
    // 注册API最小版本
    void register_min_version(const std::string& api_path, const std::string& min_version);
    
    // 验证特定API版本
    bool validate_api_version(const std::string& api_path, 
                           const std::string& client_version, 
                           std::string& error_msg);
    
    // 获取支持的版本列表
    std::vector<std::string> get_supported_versions() const;
};
```

#### 文件：`backend/src/middleware/version_middleware.cpp`
**实现要点**：
- 正则表达式解析版本号（支持预发布标签）
- 兼容性检查规则：主版本必须一致，客户端次版本 ≤ 服务端次版本
- 详细日志记录（可配置）
- 错误响应构建

**示例使用**：
```cpp
// 初始化中间件
VersionMiddleware middleware("1.1.0");

// 验证客户端版本
std::string error_msg;
if (!middleware.validate_client_version("1.0.0", error_msg)) {
    // 返回400错误
    res.status = 400;
    res.set_content(build_version_error_response(...), "application/json");
    return;
}

// 获取响应版本
std::string response_version = middleware.get_response_version(client_version);
res.set_header("API-Version", response_version);
```

### 1.2 MQTT Schema

#### 文件：`mqtt/schemas/vehicle_control.json` (v1.1.0)
**新增字段**：
```json
{
  "schemaVersion": {
    "type": "string",
    "pattern": "^\\d+\\.\\d+\\.\\d+$",
    "default": "1.0.0"
  },
  "emergency_brake": {
    "type": "boolean",
    "default": "false"
  },
  "seq": {
    "type": "integer",
    "minimum": 0
  },
  "timestamp": {
    "type": "integer"
  },
  "sessionId": {
    "type": "string"
  }
}
```

#### 文件：`mqtt/schemas/vehicle_status.json` (v1.1.0)
**新增字段**：
```json
{
  "schemaVersion": {
    "type": "string",
    "pattern": "^\\d+\\.\\d+\\.\\d+$",
    "default": "1.0.0"
  },
  "network_quality": {
    "type": "object",
    "properties": {
      "rtt_ms": {"type": "number"},
      "packet_loss_rate": {"type": "number", "minimum": 0.0, "maximum": 1.0},
      "active_link": {"type": "string", "enum": ["5G", "4G", "WiFi", "Ethernet"]},
      "jitter_ms": {"type": "number"}
    }
  },
  "sessionId": {
    "type": "string"
  }
}
```

### 1.3 OpenAPI契约

#### 文件：`backend/api/openapi.yaml`
**结构**：
```yaml
openapi: 3.0.2
info:
  title: Teleop Backend API
  version: 1.1.0
  description: |
    版本兼容说明：
    - 主版本变更：URL路径变为 /api/v2/
    - 次版本变更：向后兼容的新增字段
    - 所有新增响应字段默认可选

paths:
  /health: ...
  /api/v1/me: ...
  /api/v1/vins: ...
  /api/v1/vins/{vin}/sessions: ...
  ...

components:
  schemas:
    UserInfoV1: ...
    VinInfoV1_1: ...
    SessionCreatedV1_1: ...
    MediaUrlsV1_1: ...
    ControlConfigV1_1: ...
```

**关键特性**：
- 每个响应Schema包含`apiVersion`字段
- 每个API端点支持`API-Version`请求头
- 响应头返回`API-Version`指示实际版本
- 完整的错误响应Schema（包括版本不匹配）

### 1.4 验证脚本

#### 文件：`scripts/validate_api_against_openapi.py`
**功能**：
- 解析OpenAPI契约文件
- 扫描Backend C++代码提取路由
- 检查所有OpenAPI定义的路径是否已实现
- 检查Backend实现的路径是否在OpenAPI中记录
- 验证响应字段定义

**使用方法**：
```bash
./scripts/validate_api_against_openapi.py
```

**输出示例**：
```
[INFO] Loading OpenAPI spec from backend/api/openapi.yaml
[INFO] API Version: 1.1.0
[INFO] Total paths defined: 12
[INFO] Scanning Backend implementation in backend
[INFO] Total handlers found: 8
[INFO] Checking if all OpenAPI paths are implemented...
  [OK] GET /health
  [OK] GET /ready
  [OK] GET /api/v1/me
  [OK] GET /api/v1/vins
...

================================================================================
VALIDATION SUMMARY
================================================================================
Total Issues: 2
  Errors:   0
  Warnings: 2

WARNINGS:
  - POST /api/v1/vins/{vin}/grant - NOT DOCUMENTED
  - POST /api/v1/vins/{vin}/revoke - NOT DOCUMENTED
```

#### 文件：`scripts/test-version-negotiation.sh`
**测试用例**：
1. 有效版本测试（1.0.0, 1.1.0）
2. 版本不兼容测试（2.0.0）
3. 无版本头测试
4. 健康检查测试（无需版本头）
5. 响应apiVersion字段验证

---

## 2. 已修改的文件

### 2.1 Backend main.cpp

**修改内容**：
1. 添加版本中间件头文件引用
2. 在main函数中初始化`VersionMiddleware`
3. 修改`/api/v1/me`路由添加版本协商

**代码示例**：
```cpp
#include "middleware/version_middleware.h"

int main() {
    // ... 其他代码
    
    // 初始化版本协商中间件
    std::string backend_version = get_env("BACKEND_VERSION", "1.1.0");
    teleop::middleware::VersionMiddleware version_middleware(backend_version);
    bool enable_version_validation = get_env("ENABLE_VERSION_VALIDATION", "true") == "true";
    
    // /api/v1/me 路由修改
    svr.Get("/api/v1/me", [..., &version_middleware, enable_version_validation](
        const httplib::Request& req, httplib::Response& res) {
        
        // 版本协商
        std::string client_version = req.get_header_value("API-Version");
        std::string error_msg;
        if (!version_middleware.validate_client_version(client_version, error_msg)) {
            res.status = 400;
            // 返回版本错误...
            return;
        }
        
        // 设置响应版本头
        res.set_header("API-Version", version_middleware.get_response_version(...));
        
        // ... 原有逻辑
        
        // 添加apiVersion字段
        nlohmann::json out;
        out["apiVersion"] = "1.1.0";
        // ... 其他字段
    });
}
```

---

## 3. 待完成的实现

### 3.1 HTTP API路由修改清单

| 路由 | 状态 | 优先级 |
|------|------|--------|
| `/api/v1/me` | ✅ 已修改 | - |
| `/api/v1/vins` | ⏳ 待修改 | 高 |
| `/api/v1/vins/{vin}/sessions` | ⏳ 待修改 | 高 |
| `/api/v1/sessions/{sessionId}/end` | ⏳ 待修改 | 中 |
| `/api/v1/sessions/{sessionId}/unlock` | ⏳ 待修改 | 中 |
| `/api/v1/vins/{vin}/grant` | ⏳ 待修改 | 低 |
| `/api/v1/vins/{vin}/revoke` | ⏳ 待修改 | 低 |
| `/api/v1/vins/{vin}/permissions` | ⏳ 待修改 | 低 |

**修改模板**：
```cpp
svr.Get("/api/v1/vins", [..., &version_middleware, enable_version_validation](
    const httplib::Request& req, httplib::Response& res) {
    
    // 1. 版本协商（复制 /api/v1/me 的代码）
    
    // 2. 原有JWT验证逻辑
    
    // 3. 原有业务逻辑
    
    // 4. 添加apiVersion字段到响应
    nlohmann::json response;
    response["apiVersion"] = "1.1.0";  // 或根据client_version决定
    response["vins"] = vins_array;
    
    res.set_content(response.dump(), "application/json");
});
```

### 3.2 WebRTC URL版本化

**待修改函数**：
```cpp
// backend/src/main.cpp

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

static std::string build_whip_url(const std::string& zlm_api_url, 
                                   const std::string& vin, 
                                   const std::string& session_id,
                                   const std::string& api_version = "1.0.0") {
    std::string host, port;
    whep_whip_host_port(host, port);
    std::string stream_name = vin + "-" + session_id;
    return "whip://" + host + ":" + port + "/index/api/webrtc?" +
           "app=teleop&stream=" + stream_name + "&type=push&apiVersion=" + api_version;
}
```

**调用示例**：
```cpp
std::string apiVersion = "1.1.0";  // 根据client_version决定
std::string whepUrl = build_whep_url(zlm_api_url, vin, session_id, apiVersion);
std::string whipUrl = build_whip_url(zlm_api_url, vin, session_id, apiVersion);
```

### 3.3 Vehicle-side修改

**文件**: `Vehicle-side/src/mqtt_handler.cpp`

**待添加代码**：
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
        if (msg.contains("seq")) {
            // 验证序号（防重放）
        }
        if (msg.contains("sessionId")) {
            m_currentSessionId = msg["sessionId"];
        }
    }
    
    // 通用字段处理（v1.0.0 + v1.1.0）
    if (type == "remote_control") {
        // ... 原有控制逻辑
    }
}

void MqttHandler::publishStatus() {
    nlohmann::json status;
    
    // 添加版本字段
    status["schemaVersion"] = "1.1.0";
    status["type"] = "vehicle_status";
    status["vin"] = m_vin;
    status["timestamp"] = teleop::common::Timestamp::now_ms();
    
    // ... 原有字段
    
    // v1.1.0 新增字段
    nlohmann::json netQual;
    netQual["rtt_ms"] = m_networkQuality.rtt_ms;
    netQual["packet_loss_rate"] = m_networkQuality.loss_rate;
    netQual["active_link"] = m_networkQuality.link_type;
    status["network_quality"] = netQual;
    
    if (!m_currentSessionId.empty()) {
        status["sessionId"] = m_currentSessionId;
    }
    
    // ... 发布逻辑
}
```

### 3.4 Client修改

**文件**: `client/src/mqttcontroller.cpp`

**待添加代码**：
```cpp
void MqttController::sendControlCommand(const QJsonObject &command) {
    nlohmann::json msg;
    
    // 添加版本字段
    msg["schemaVersion"] = "1.1.0";
    msg["type"] = command["type"].toString().toStdString();
    msg["vin"] = m_currentVin.toStdString();
    
    // ... 原有字段
    
    // v1.1.0 新增字段
    msg["seq"] = ++m_seq;
    msg["timestamp"] = teleop::common::Timestamp::now_ms();
    
    QString sessionId = command.value("sessionId").toString();
    if (!sessionId.isEmpty()) {
        msg["sessionId"] = sessionId.toStdString();
    }
    
    // ... 发布逻辑
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
    
    // ... 原有处理逻辑
}
```

---

## 4. 配置文件更新

### 4.1 Backend配置

```yaml
# config/backend_config.yaml

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

### 4.2 Client配置

```yaml
# config/client_config.yaml

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

### 4.3 Vehicle配置

```yaml
# config/vehicle_config.yaml

mqtt:
  schema_version: "1.1.0"
  supported_versions:
    - "1.0.0"
    - "1.1.0"
  auto_upgrade: false
```

---

## 5. 测试计划

### 5.1 单元测试

**文件**: `backend/tests/test_version_middleware.cpp`

**测试用例**：
```cpp
TEST(VersionTest, ParseValidVersions) {
    auto v1 = Version::parse("1.0.0");
    EXPECT_EQ(v1.major, 1);
    EXPECT_EQ(v1.minor, 0);
    EXPECT_EQ(v1.patch, 0);
    
    auto v2 = Version::parse("1.1.0");
    EXPECT_EQ(v2.major, 1);
    EXPECT_EQ(v2.minor, 1);
    EXPECT_EQ(v2.patch, 0);
    
    auto v3 = Version::parse("2.0.0-beta");
    EXPECT_EQ(v3.major, 2);
    EXPECT_EQ(v3.minor, 0);
    EXPECT_EQ(v3.patch, 0);
}

TEST(VersionTest, ParseInvalidVersions) {
    auto v = Version::parse("invalid");
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
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

### 5.2 集成测试

**文件**: `scripts/test-api-version-compat.sh`

**测试场景**：
1. Backend v1.1.0 + Client v1.0.0 → 兼容
2. Backend v1.1.0 + Client v1.1.0 → 兼容
3. Backend v1.0.0 + Client v1.1.0 → 部分兼容（新字段不可用）
4. Backend v2.0.0 + Client v1.1.0 → 不兼容（拒绝请求）

### 5.3 MQTT版本测试

**文件**: `scripts/test-mqtt-version.sh`

**测试场景**：
1. 发布v1.0.0控制消息 → Vehicle正常处理
2. 发布v1.1.0控制消息 → Vehicle正常处理，包含新字段
3. 发布v2.0.0控制消息 → Vehicle拒绝
4. 验证Vehicle发布的状态消息包含schemaVersion

### 5.4 E2E测试

**文件**: `scripts/e2e-version-negotiation.sh`

**测试场景**：
1. 完整的版本协商流程：Client登录 → 创建会话 → 控制车辆 → 结束会话
2. 使用不同版本的Client和Vehicle进行测试
3. 验证日志中的版本信息

---

## 6. 验证清单

### 6.1 功能验证

| 功能 | 状态 | 验证方法 |
|------|------|---------|
| 版本号解析 | ✅ 已实现 | 单元测试 |
| 版本兼容性检查 | ✅ 已实现 | 单元测试 |
| HTTP版本协商 | ⏳ 部分实现 | 集成测试 |
| MQTT版本协商 | ⏳ 待实现 | 集成测试 |
| OpenAPI契约 | ✅ 已创建 | 静态检查 |
| API验证脚本 | ✅ 已创建 | 运行脚本 |
| 错误响应 | ✅ 已实现 | 手动测试 |

### 6.2 代码质量

| 检查项 | 状态 | 工具 |
|--------|------|------|
| 编译通过 | ⏳ 待验证 | cmake + make |
| 代码格式 | ⏳ 待验证 | clang-format |
| 静态分析 | ⏳ 待验证 | clang-tidy |
| 内存泄漏 | ⏳ 待验证 | valgrind |
| 覆盖率 | ⏳ 待验证 | gcov |

### 6.3 文档完整性

| 文档 | 状态 |
|------|------|
| API契约 | ✅ 已创建 |
| Schema文档 | ✅ 已更新 |
| 实现文档 | ✅ 已创建 |
| 测试文档 | ⏳ 待完善 |
| 用户文档 | ⏳ 待完善 |

---

## 7. 下一步行动

### 7.1 立即行动（高优先级）

1. **完成Backend API路由修改**
   - 修改 `/api/v1/vins`
   - 修改 `/api/v1/vins/{vin}/sessions`
   - 修改 `build_whep_url` 和 `build_whip_url`
   - 测试HTTP版本协商

2. **完成Vehicle-side修改**
   - 修改 `mqtt_handler.cpp` 添加版本协商
   - 测试MQTT版本协商

3. **完成Client修改**
   - 修改 `mqttcontroller.cpp` 添加版本字段
   - 测试MQTT消息版本

### 7.2 后续行动（中优先级）

4. **创建完整测试**
   - API版本协商单元测试
   - MQTT版本协商测试
   - E2E测试

5. **完善文档**
   - 更新用户手册
   - 添加故障排查指南

6. **CI/CD集成**
   - 添加版本验证到CI流程
   - 自动化测试报告

### 7.3 长期规划（低优先级）

7. **V2功能**
   - 代码生成工具
   - 版本注册表自动化
   - 灰度发布策略

---

## 8. 风险与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|---------|
| 版本字段拼写错误 | 导致消息被拒绝 | 低 | 自动化测试验证 |
| 老版本客户端不响应版本头 | 降级到默认版本 | 中 | 中间件默认处理 |
| 性能开销 | 控制延迟增加 | 中 | 可选启用验证 |
| 版本兼容性判断错误 | 新老版本无法通信 | 低 | 充分测试 |

---

## 9. 成功标准

### 9.1 功能成功标准

- ✅ 所有API路由支持版本协商
- ✅ MQTT消息包含版本字段
- ✅ 版本不兼容时返回明确错误
- ✅ 向后兼容性保证

### 9.2 质量成功标准

- ✅ 单元测试覆盖率 > 80%
- ✅ 集成测试全部通过
- ✅ E2E测试全部通过
- ✅ 代码审查通过

### 9.3 文档成功标准

- ✅ OpenAPI契约完整
- ✅ 实现文档清晰
- ✅ 测试文档完整
- ✅ 用户文档可用

---

## 10. 总结

### 10.1 已完成

✅ **核心框架**
- 版本协商中间件完整实现
- MQTT Schema升级至v1.1.0
- OpenAPI契约创建
- 验证脚本框架

✅ **部分实现**
- Backend main.cpp版本协商（`/api/v1/me`）
- HTTP API版本协商框架
- MQTT版本字段定义

### 10.2 待完成

⏳ **HTTP API**
- 完成所有API路由版本协商
- WebRTC URL版本化

⏳ **MQTT消息**
- Vehicle-side版本协商实现
- Client版本协商实现

⏳ **测试**
- 单元测试
- 集成测试
- E2E测试

### 10.3 预期收益

📈 **收益**
- 支持多版本模块并行部署
- 接口变更可预测、可验证
- 降低因接口不匹配导致的故障
- 提升系统可维护性

---

## 附录

### A. 相关文档

- [版本兼容性实现文档](./VERSION_COMPATIBILITY_IMPLEMENTATION.md)
- [OpenAPI契约规范](../backend/api/openapi.yaml)
- [MQTT Schema规范](../mqtt/schemas/)

### B. 参考资源

- [Semantic Versioning 2.0.0](https://semver.org/)
- [OpenAPI 3.0 规范](https://swagger.io/specification/)
- [JSON Schema Draft 07](https://json-schema.org/)

### C. 联系方式

如有问题或建议，请联系开发团队。
