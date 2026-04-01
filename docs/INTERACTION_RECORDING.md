# 模块间交互数据记录与回放分析

## 1. Executive Summary

- **目标**：把 backend、carla-bridge、client、media、Vehicle-side 五模块之间的交互数据与关键中间数据记录到文件，出问题时载入分析，高效精准定位。
- **格式**：统一使用 **NDJSON**（每行一条 JSON），便于按行追加、按行过滤、与时间线对齐。
- **开关**：通过环境变量 `RECORD_INTERACTION=1` 开启；`RECORD_INTERACTION_DIR` 指定目录（默认 `./recordings`）。
- **分析**：使用 `scripts/analyze_interaction_log.py` 按时间、模块、主题、VIN、session 过滤并输出时间线或导出子集。

---

## 2. 记录格式（NDJSON）

每条记录一行，UTF-8，字段如下（均为可选，但建议至少包含 `ts`、`module`、`direction`、`topic_or_path`）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ts` | string | ISO8601 时间戳，如 `2026-02-10T12:00:00.123Z` |
| `module` | string | 产生该记录的模块：`backend` / `carla-bridge` / `client` / `media` / `Vehicle-side` |
| `direction` | string | `in` / `out`（相对于本模块的入/出） |
| `peer` | string | 交互对端：`mqtt` / `http` / `zlm` / `webrtc` / `carla` 等 |
| `topic_or_path` | string | MQTT 主题或 HTTP path 或 ZLM 流 ID 等 |
| `payload_summary` | string | 简短摘要（如 `type=start_stream,vin=xxx`），便于 grep |
| `payload_size` | number | 原始 payload 字节数 |
| `vin` | string | 车辆 VIN（若有） |
| `session_id` | string | 会话 ID（若有） |
| `payload` | string | 完整 payload（可选，高频时建议不写或采样） |
| `error` | string | 若为错误/异常，简要描述 |

**示例**：

```json
{"ts":"2026-02-10T12:00:00.123Z","module":"carla-bridge","direction":"in","peer":"mqtt","topic_or_path":"vehicle/control","payload_summary":"type=start_stream,vin=carla-sim-001","payload_size":45,"vin":"carla-sim-001"}
{"ts":"2026-02-10T12:00:00.124Z","module":"carla-bridge","direction":"out","peer":"mqtt","topic_or_path":"vehicle/status","payload_summary":"speed=0,gear=1,streaming=true","payload_size":120,"vin":"carla-sim-001"}
```

---

## 3. 环境变量

| 变量 | 说明 | 默认 |
|------|------|------|
| `RECORD_INTERACTION` | 设为 `1` 或 `true` 时开启记录 | 不设置则不写文件 |
| `RECORD_INTERACTION_DIR` | 记录文件所在目录 | `./recordings` |
| `RECORD_INTERACTION_FULL_PAYLOAD` | 设为 `1` 时在记录中写入完整 payload（慎用，体积大） | 不设置则只写 summary/size |

各模块将在此目录下写入 **按模块名 + 日期 + 可选 run-id 命名的文件**，例如：

- `recordings/carla-bridge_2026-02-10_12-00-00.jsonl`
- `recordings/client_2026-02-10_12-00-00.jsonl`

分析脚本可一次加载多个文件（同一次调试的多次运行可放同一目录）。

---

## 4. 各模块记录点与数据

### 4.1 carla-bridge（已实现）

| 位置 | 方向 | 记录内容 |
|------|------|----------|
| MQTT 收到 `vehicle/control` | in | topic、payload_summary、payload_size、vin |
| MQTT 发布 `vehicle/status` | out | topic、payload_summary、payload_size、vin（可采样，如每 1s 一条） |
| ZLM 推流 worker 启动/停止 | out | peer=zlm，topic_or_path=stream_id，payload_summary=start/stop |

### 4.2 backend（待接入）

建议在以下接口的**请求进入**与**响应返回**时各写一条：

- `GET /api/v1/me`
- `GET /api/v1/vins`
- `POST /api/v1/vins/{vin}/sessions`
- `GET /api/v1/sessions/{sessionId}/streams`

字段建议：`ts`、`module=backend`、`direction=in/out`、`peer=http`、`topic_or_path=path`、`payload_summary=method path status`、`session_id`、`vin`（从 path 解析）。可选：`payload_size`、出错时 `error`。

### 4.3 client（待接入）

| 位置 | 方向 | 记录内容 |
|------|------|----------|
| MQTT 收到 `vehicle/status`（或 mosquitto_sub 输出） | in | topic、payload_summary、payload_size、vin |
| MQTT 发送 `vehicle/control` | out | topic、payload_summary、payload_size、vin |
| 请求 backend（session/streams 等） | out/in | path、status、session_id、vin |
| WebRTC 连接状态变化 | - | peer=webrtc，topic_or_path=stream 或 url，payload_summary=state=closed/failed 等 |

### 4.4 media（ZLM）

- 一般不在此仓库内改 ZLM 代码。可选方案：  
  - 通过 ZLM hook 将 publish/play 等事件转发到本地脚本，由脚本按上述格式写 NDJSON；或  
  - 仅用 ZLM 容器日志 + 分析脚本的“按时间对齐”功能，与其它模块的 NDJSON 一起看。  
- 若后续在 media 目录增加“代理/网关”层，可在该层对请求/响应写 NDJSON。

### 4.5 Vehicle-side（待接入）

| 位置 | 方向 | 记录内容 |
|------|------|----------|
| MQTT 订阅收到 `vehicle/control` | in | topic、payload_summary、payload_size、vin |
| MQTT 发布 `vehicle/status` | out | topic、payload_summary、payload_size、vin（可采样） |
| 控制指令执行/看门狗/安全停车 | - | peer=control，payload_summary=执行结果或事件类型 |

---

## 5. C++ 模块写入示例（backend / client / Vehicle-side）

在需记录处调用“写一行 NDJSON”的逻辑即可。下面为 C++ 示例（可封装成共用函数，或内联最小实现）：

```cpp
// 仅在 RECORD_INTERACTION=1 时写入，避免生产环境开销
#include <fstream>
#include <chrono>
#include <nlohmann/json.hpp>  // 或手拼 JSON 字符串

void write_interaction_record(
    const std::string& module,
    const std::string& direction,
    const std::string& peer,
    const std::string& topic_or_path,
    const std::string& payload_summary,
    int payload_size,
    const std::string& vin,
    const std::string& session_id,
    const std::string& error)
{
    const char* env = std::getenv("RECORD_INTERACTION");
    if (!env || (std::string(env) != "1" && std::string(env) != "true")) return;
    std::string dir = ".";
    const char* dir_env = std::getenv("RECORD_INTERACTION_DIR");
    if (dir_env && dir_env[0]) dir = dir_env;

    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char ts_buf[32];
    strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%S", gmtime(&t));
    nlohmann::json j = {
        {"ts", std::string(ts_buf) + "Z"},
        {"module", module},
        {"direction", direction},
        {"peer", peer},
        {"topic_or_path", topic_or_path},
        {"payload_summary", payload_summary},
        {"payload_size", payload_size},
        {"vin", vin},
        {"session_id", session_id},
        {"error", error}
    };
    std::string line = j.dump() + "\n";
    std::string path = dir + "/" + module + "_interaction.jsonl";  // 或按日期分文件
    std::ofstream f(path, std::ios::app);
    if (f) f << line;
}
```

调用示例（backend 在 POST sessions 处理完后）：

```cpp
write_interaction_record("backend", "out", "http", "/api/v1/vins/" + vin + "/sessions",
    "POST 201 session_id=" + session_id, (int)res_body.size(), vin, session_id, "");
```

---

## 6. 载入与分析

### 6.1 分析脚本

```bash
# 指定记录目录，按时间范围、模块、主题、VIN 过滤，输出时间线
./scripts/analyze_interaction_log.py --dir ./recordings [--since "2026-02-10T12:00:00" ] [--module carla-bridge ] [--topic "vehicle/control" ] [--vin carla-sim-001 ] [--out timeline.txt ]
```

- `--dir`：包含各模块 `*.jsonl` 的目录。  
- `--since` / `--until`：ISO8601 时间，只保留该区间内记录。  
- `--module` / `--topic` / `--vin`：过滤条件，可多选。  
- `--out`：将过滤结果写入文件（否则 stdout）。  
- 输出为按 `ts` 排序的时间线，便于与日志、现象对照。

### 6.2 典型排查流程

1. 复现问题前：设置 `RECORD_INTERACTION=1`、`RECORD_INTERACTION_DIR=./recordings`，复现一次。  
2. 复现后：到 `RECORD_INTERACTION_DIR` 取对应时间段的 `*.jsonl`。  
3. 运行：  
   `./scripts/analyze_interaction_log.py --dir ./recordings --since <现象发生前 1 分钟> --until <现象发生后 1 分钟> --out issue_round.txt`  
4. 结合 `issue_round.txt` 与各模块日志（client/backend/carla-bridge/Vehicle-side 控制台或日志文件），看哪一环缺少或异常。

---

## 7. 风险与注意

- **磁盘**：长时间开启且写 `payload` 会占空间；开发阶段建议只开一段时间或仅 summary。  
- **性能**：写文件为同步追加，高 QPS 时可能带来延迟；status 类可采样（如每 1s 一条）。  
- **敏感信息**：`payload` 可能含 token/会话信息，记录目录需排除在版本库与生产部署之外。  
- **回滚**：关闭 `RECORD_INTERACTION` 即停写，无需改代码即可回滚。

---

## 8. 后续演进

- **MVP（当前）**：NDJSON 格式 + carla-bridge 全量实现 + 分析脚本 + 其余模块记录点文档与 C++ 示例。  
- **V1**：backend/client/Vehicle-side 接入上述写入了；media 通过 hook 或网关写 ZLM 事件。  
- **V2**：按 session_id 自动归档、与录制/回放系统关联、可选上传到问题追踪附件。

---

## 9. 实现状态

| 模块 | 状态 | 说明 |
|------|------|------|
| carla-bridge | 已实现 | MQTT in/out（status 按 1s 采样）、ZLM 推流 start/stop |
| backend | 待接入 | 见 §4.2 与 §5 C++ 示例，在 sessions/vins 等接口进出各写一条 |
| client | 待接入 | 见 §4.3，MQTT 收发、HTTP、WebRTC 状态 |
| media | 可选 | ZLM 不改代码时可对容器日志做时间对齐 |
| Vehicle-side | 待接入 | 见 §4.5 与 §5，MQTT 收发、控制执行事件 |
