# 接口标准化：契约管理 + 自动同步 + 验证工具

## 1. Executive Summary

- **分级真源与门禁**：以 `project_spec.md` **§12** 为权威（MVP / V1 / V2、弃用策略、CI 映射表）；本文档描述落地工具与接口形状。
- **目标**：定义跨模块接口契约（HTTP API / MQTT / 会话/流），改一处接口定义后自动同步所有使用处，避免字段缺失、类型不一致、版本错配。
- **策略**：
  - **HTTP API**：使用 OpenAPI 3.0（Swagger），由契约定义自动生成 C++/Python 客户端代码与文档。
  - **MQTT 消息**：使用 JSON Schema，所有模块共享同一 Schema 文件。
  - **会话/媒体 URL**：统一算法（WHIP/WHEP 生成）与字段命名。
  - **验证工具**：CI/CD 或本地运行脚本，检查实现是否与契约一致（字段缺失/类型错误/多余字段）。
- **收益**：减少因接口不一致导致的通信异常；改接口时只需改契约，所有使用处同步更新。

---

## 2. 当前接口与问题

| 接口类别 | 当前定义方式 | 存在问题 |
|-----------|--------------|-----------|
| HTTP API（Backend） | C++ 代码中直接定义路径与 JSON 字段 | 字段散落；改接口需逐文件查找并修改 |
| MQTT 消息 | Python/C++ 中字符串字面量（`vehicle/control`、`type=start_stream`） | 主题与消息体无统一 Schema；易写错字段名 |
| 会话/媒体 URL | `build_whep_url`、`build_whip_url` 函数各处重复 | 缺少统一算法；易生成格式不一致的 URL |
| WebRTC | WHIP/WHEP URL 直接拼接 | 缺少版本控制；难以向前/向后兼容 |

---

## 3. 标准化方案

### 3.1 HTTP API（Backend → Client）

#### 3.1.1 契约文件（OpenAPI 3.0）

位置：`backend/api/openapi.yaml`

```yaml
openapi: 3.0.2
info:
  title: Teleop Backend API
  version: 1.0.0
servers:
  - url: http://localhost:8080
    description: 本地开发

paths:
  /api/v1/me:
    get:
      summary: 获取当前用户信息
      security:
        - bearerAuth: []
      responses:
        '200':
          description: 成功
          content:
            application/json:
              schema:
                $ref: '#/components/schemas/UserInfo'

  /api/v1/vins:
    get:
      summary: 获取用户可访问的 VIN 列表
      security:
        - bearerAuth: []
      responses:
        '200':
          description: 成功
          content:
            application/json:
              schema:
                type: object
                properties:
                  vins:
                    type: array
                    items:
                      $ref: '#/components/schemas/VinInfo'

  /api/v1/vins/{vin}/sessions:
    post:
      summary: 为指定 VIN 创建会话
      parameters:
        - name: vin
          in: path
          required: true
          schema:
            type: string
      security:
        - bearerAuth: []
      responses:
        '201':
          description: 会话已创建
          content:
            application/json:
              schema:
                $ref: '#/components/schemas/SessionCreated'
        '403':
          $ref: '#/components/responses/Forbidden'
        '503':
          $ref: '#/components/responses/InternalServerError'

  /api/v1/sessions/{sessionId}/streams:
    get:
      summary: 获取会话可用流列表
      security:
        - bearerAuth: []
      responses:
        '200':
          description: 成功
          content:
            application/json:
              schema:
                $ref: '#/components/schemas/SessionStreams'

  /api/v1/sessions/{sessionId}/lock:
    post:
      summary: 获取会话控制锁（占位）
      security:
        - bearerAuth: []
      requestBody:
        required: false
      responses:
        '200':
          description: 锁已获取
          content:
            application/json:
              schema:
                type: object
                properties:
                  locked:
                    type: boolean
                  lockId:
                    type: string

components:
  securitySchemes:
    bearerAuth:
      type: http
      scheme: bearer
      bearerFormat: JWT
      description: Keycloak JWT，从 Authorization: Bearer <token> 获取

  schemas:
    UserInfo:
      type: object
      properties:
        sub:
          type: string
          description: Keycloak subject（用户 ID）
        preferred_username:
          type: string
        roles:
          type: array
          items:
            type: string

    VinInfo:
      type: object
      properties:
        vin:
          type: string
          example: "carla-sim-001"
        model:
          type: string
        status:
          type: string
          enum: [online, offline]
        controller_user_id:
          type: string
          nullable: true
        last_heartbeat:
          type: string
          format: date-time

    SessionCreated:
      type: object
      required: [sessionId, media, control]
      properties:
        sessionId:
          type: string
          format: uuid
        media:
          $ref: '#/components/schemas/MediaUrls'
        control:
          $ref: '#/components/schemas/ControlConfig'

    MediaUrls:
      type: object
      required: [whip, whep]
      properties:
        whip:
          type: string
          format: uri
          description: WHIP 推流 URL（车端用）
        whep:
          type: string
          format: uri
          description: WHEP 拉流 URL（Client 用）

    ControlConfig:
      type: object
      properties:
        algo:
          type: string
          enum: [HMAC-SHA256, NONE]
          default: HMAC-SHA256
        seqStart:
          type: integer
          default: 1
        tsWindowMs:
          type: integer
          default: 2000
        mqtt_broker_url:
          type: string
          format: uri
        mqtt_client_id:
          type: string

    SessionStreams:
      type: object
      properties:
        streams:
          type: array
          items:
            type: string
          description: 流名列表（如 ["cam_front", "cam_rear", ...]）

  responses:
    Forbidden:
      description: 禁止访问（权限不足或 VIN 未授权）
      content:
        application/json:
          schema:
            type: object
            properties:
              error:
                type: string
                enum: [forbidden]
              details:
                type: string

    InternalServerError:
      description: 内部错误（DB 连接失败等）
      content:
        application/json:
          schema:
            type: object
            properties:
              error:
                type: string
                enum: [internal]
              details:
                type: string
```

#### 3.1.2 代码生成与使用

- **文档生成**：`make generate-api-docs` → 使用 `swagger-codegen` 生成静态文档（`backend/api/docs/`）。
- **C++/Python 客户端代码生成**：可按需集成 `openapi-generator`，为 Client 生成 SDK。
- **验证工具**：`scripts/validate_api.py`，检查 Backend 实现的路径/请求体/响应体是否与 OpenAPI 一致。

---

### 3.2 MQTT 消息（Vehicle/Bridge ↔ Broker ↔ Client）

#### 3.2.1 JSON Schema 定义

位置：`mqtt/schemas/`，按主题分文件。

**mqtt/schemas/vehicle_control.json**

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "urn:teleop:schemas:vehicle_control",
  "title": "MQTT vehicle/control 消息",
  "type": "object",
  "required": ["type", "vin"],
  "properties": {
    "type": {
      "type": "string",
      "enum": ["start_stream", "stop_stream", "remote_control", "remote_control_ack"]
    },
    "vin": {
      "type": "string",
      "minLength": 1,
      "description": "目标 VIN"
    },
    "steering": {
      "type": "number",
      "minimum": -1.0,
      "maximum": 1.0,
      "description": "方向盘，-1~1"
    },
    "throttle": {
      "type": "number",
      "minimum": 0.0,
      "maximum": 1.0,
      "description": "油门，0~1"
    },
    "brake": {
      "type": "number",
      "minimum": 0.0,
      "maximum": 1.0,
      "description": "制动，0~1"
    },
    "gear": {
      "type": "integer",
      "enum": [-1, 0, 1, 2, 3, 4],
      "description": "档位，-1:R, 0:N, 1:P, 2:D, 3:L, 4:M"
    },
    "remote_enabled": {
      "type": "boolean",
      "default": true,
      "description": "是否启用远程控制"
    },
    "streaming": {
      "type": "boolean",
      "default": false,
      "description": "是否在推流（仅用于状态同步）"
    }
  },
  "additionalProperties": false
}
```

**mqtt/schemas/vehicle_status.json**

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "urn:teleop:schemas:vehicle_status",
  "title": "MQTT vehicle/status 消息",
  "type": "object",
  "required": ["vin", "type", "timestamp"],
  "properties": {
    "vin": {
      "type": "string"
    },
    "type": {
      "type": "string",
      "const": "vehicle_status"
    },
    "timestamp": {
      "type": "integer",
      "description": "毫秒时间戳"
    },
    "speed": {
      "type": "number",
      "minimum": 0.0,
      "description": "速度 km/h"
    },
    "battery": {
      "type": "number",
      "minimum": 0.0,
      "maximum": 100.0,
      "description": "电池百分比"
    },
    "brake": {
      "type": "number",
      "minimum": 0.0,
      "maximum": 1.0
    },
    "throttle": {
      "type": "number",
      "minimum": 0.0,
      "maximum": 1.0
    },
    "steering": {
      "type": "number",
      "minimum": -1.0,
      "maximum": 1.0
    },
    "gear": {
      "type": "integer",
      "enum": [-1, 0, 1, 2, 3, 4]
    },
    "odometer": {
      "type": "number",
      "minimum": 0.0,
      "description": "里程 km"
    },
    "temperature": {
      "type": "number",
      "description": "温度"
    },
    "voltage": {
      "type": "number",
      "description": "电压 V"
    },
    "remote_control_enabled": {
      "type": "boolean",
      "description": "是否启用远程控制"
    },
    "driving_mode": {
      "type": "string",
      "enum": ["远驾", "自驾"]
    },
    "streaming": {
      "type": "boolean",
      "description": "是否在推流"
    }
  },
  "additionalProperties": false
}
```

#### 3.2.2 各模块使用方式

- **Python（carla-bridge）**：使用 `jsonschema` 库在运行时验证发布的 JSON；加载 Schema 文件。
- **C++（Vehicle-side / Client）**：在编译时用 `quicktype` 或 `json-schema-validator` 生成 C++ 结构体与验证代码；或用 nlohmann::json 的 schema_validate（若支持）。

示例（carla-bridge）：

```python
import jsonschema
import json

CONTROL_SCHEMA = json.load(open("mqtt/schemas/vehicle_control.json"))
STATUS_SCHEMA = json.load(open("mqtt/schemas/vehicle_status.json"))

def publish_status(status_dict):
    jsonschema.validate(status_dict, STATUS_SCHEMA)
    mqtt_client.publish("vehicle/status", payload=json.dumps(status_dict))
```

#### 3.2.3 验证工具

`scripts/validate_mqtt_schemas.py`：扫描各模块代码，检查发布/订阅的消息是否符合 Schema。

---

### 3.3 会话/媒体 URL（WHIP/WHEP）

#### 3.3.1 统一算法与字段

- **字段命名**：`sessionId`（UUID）、`vin`（string）、`whipUrl`（WHEP/WHEP 统一）、`whepUrl`。
- **URL 模板**：
  - WHEP: `whep://<host>:<port>/index/api/webrtc?app=teleop&stream=<vin>-<sessionId>&type=play`
  - WHIP: `whip://<host>:<port>/index/api/webrtc?app=teleop&stream=<vin>-<sessionId>&type=push`
- **Host 解析**：优先 `ZLM_PUBLIC_BASE`，否则 `ZLM_API_URL`。

Backend 统一使用 `build_whep_url`、`build_whip_url`；Vehicle-side 可用相同算法（共享头文件或工具库）。

---

## 4. 验证工具

### 4.1 API 契约验证（`scripts/validate_api_against_openapi.py`）

- 扫描 Backend 实现的路径与响应体。
- 与 OpenAPI 对比：路径是否存在、方法是否一致、必填字段是否全部返回。
- 输出缺失/不一致清单；CI 入口：`./scripts/verify-contract-v1-cross-service.sh`。

### 4.2 MQTT Schema 验证（`scripts/validate_mqtt_schemas.py`）

- 解析 Python/C++ 代码中 MQTT 消息的字面量。
- 与 JSON Schema 对比：字段是否缺失、类型是否匹配、必填字段是否缺失。
- 输出报告。

### 4.3 代码生成（可选）

- **HTTP**：使用 `swagger-codegen` 生成 Client SDK（C++/Python/TypeScript）。
- **MQTT**：使用 `quicktype` 生成 C++/Python/TypeScript 类型定义。

---

## 5. 实施计划（与 project_spec.md §12 对齐）

| 阶段 | 目标 | 交付物 / 门禁 |
|------|------|----------------|
| **MVP** | 每层单一真源 + CI 至少一条机器校验 | `backend/api/openapi.yaml`；`mqtt/schemas/*.json`；`docs/CLIENT_UI_MODULE_CONTRACT.md`；`.github/workflows/contract-ci.yml` + `client-ci.yml`；`scripts/verify-contract-artifacts.sh` |
| **V1** | Golden 全覆盖 + 跨服务契约测试 + 弃用写进 spec | `mqtt/schemas/examples/manifest.json`；`scripts/validate_api_against_openapi.py` + `verify-contract-v1-cross-service.sh`；§12.5 弃用策略 |
| **V2** | 契约与发布流水线绑定 + breaking 自动检测 | PR 上 `scripts/verify-openapi-breaking-change.sh`（`oasdiff breaking`）；可选 openapi-generator/quicktype 生成物进发布 job |

---

## 6. 注意事项

- **版本控制**：在 OpenAPI 与 JSON Schema 中指定版本；字段新增默认向后兼容，删除字段需注明主版本。
- **字段命名**：统一使用 snake_case（`session_id`、`whip_url`），避免混用驼峰。
- **错误码**：在 OpenAPI 中统一 `error` 枚举；各模块使用枚举值而非自由文本。
- **验证时机**：每次改接口契约后，运行验证工具通过才提交；CI 中自动运行。

---

## 7. 示例：改一处同步所有

**场景**：在 MQTT 控制消息中新增字段 `emergency_brake`（布尔，用于远程急停）。

1. 修改 `mqtt/schemas/vehicle_control.json`，新增字段。
2. 运行 `scripts/validate_mqtt_schemas.py`，确认 Schema 合法。
3. Carla-bridge 代码中无需改动（运行时验证自动拒绝不符合 Schema 的消息，或允许但忽略未知字段）。
4. Client 端发送控制时，若改用 SDK，直接在生成类型上添加字段；若仍是手动 JSON，注意字段拼写一致。

---

## 8. 参考资料

- OpenAPI 3.0 Spec: https://swagger.io/specification/
- JSON Schema Draft 07: https://json-schema.org/
- swagger-codegen: https://github.com/swagger-api/swagger-codegen
- quicktype: https://quicktype.io/
