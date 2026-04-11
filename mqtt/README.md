# MQTT 消息契约与 Schema

## 用途

- 定义 `vehicle/control` 与 `vehicle/status` 消息的字段、类型、约束。
- 各模块（Python/C++/Qt）导入 Schema 进行验证或自动生成结构体。
- 改接口时只需修改 Schema 文件，所有使用处自动同步（配合验证工具）。

## 文件

- `schemas/vehicle_control.json`：控制消息 Schema（JSON Schema Draft 07）。
- `schemas/vehicle_status.json`：状态消息 Schema（JSON Schema Draft 07）。
- `schemas/client_encoder_hint.json`：客户端经 MQTT 转发的 `client_video_encoder_hint`（与 DataChannel 同形，车端/carla-bridge 订阅 `teleop/client_encoder_hint`）。
- `schemas/vehicle_control.py`：Python 验证函数与包装类。
- `schemas/vehicle_status.py`：Python 验证函数与包装类。
- `schemas/__init__.py`：统一导出。

## 使用方式

### Python（carla-bridge）

```python
from mqtt.schemas import VehicleControlSchema, VehicleStatusSchema

# 收到 MQTT 消息
payload = json.loads(msg.payload)

# 验证
if not VehicleControlSchema.validate(payload):
    log_control("消息不符合 Schema，忽略: %s", payload)
    return

# 正常处理
msg_type = payload["type"]
vin = payload["vin"]
```

### C++（Vehicle-side / carla-bridge C++）

可使用 `json-schema-validator` 或 `nlohmann::json` 的 `validate`（若版本支持）。

示例（伪代码）：

```cpp
#include <nlohmann/json.hpp>
#include <fstream>

nlohmann::json control_schema;
std::ifstream f("mqtt/schemas/vehicle_control.json");
f >> control_schema;

void handle_control(const nlohmann::json& msg) {
    if (!jsonschema::validate(msg, control_schema)) {
        log_error("消息不符合 Schema");
        return;
    }
    // 正常处理
    std::string type = msg["type"];
    std::string vin = msg["vin"];
}
```

### Qt/QML（Client）

Client 可用 Python 侧验证（若启用 mosquitto_sub），或在 QML 中手动字段校验（类型/范围）。

## 修改接口

1. 在 `schemas/*.json` 中修改字段（新增/删除/改类型/约束）。
2. 运行 `scripts/validate_mqtt_schemas.py` 确保所有模块的消息使用与 Schema 一致。
3. 若模块使用生成的类型/结构体，重新生成。

## 版本管理

在 Schema 文件中增加字段：

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "urn:teleop:schemas:vehicle_control",
  "$version": "1.0.0",
  "title": "...",
  ...
}
```

## 注意事项

- 字段命名统一 snake_case（`vehicle_control` 而非 `vehicleControl`）。
- 布尔值统一 `boolean` 类型，避免混用 `int` 0/1。
- 时间戳统一为毫秒整数（`timestamp`），避免混用秒/ISO8601。
- 删除字段时在主版本号（如 2.0.0）注明“字段弃用与移除”。

## 参考

- JSON Schema Draft 07: https://json-schema.org/
