# 日志与观测性最佳实践

## 1. 目标

- **统一格式**：各模块日志带时间戳（ISO8601）、模块名/标签、关键上下文（vin/session_id/peer/err）。
- **便于自动化**：文本格式易于 grep/awk，结构化字段便于解析；避免多行日志导致分析脚本失效。
- **快速定位**：出问题时能按 vin/session_id/时间范围迅速过滤，结合交互记录与诊断脚本给出“可能原因 + 排查步骤”。

---

## 2. 当前状态

| 模块 | 日志方式 | 时间戳 | 标签/Tag | 结构化字段 | 待改进 |
|------|----------|--------|----------|------------|--------|
| carla-bridge | 自定义 `_log(tag,msg)`（print） | HH:MM:SS | 有（CARLA/MQTT/ZLM/Control/RECORD） | 无 JSON 结构，可加 vin/session_id | 建议改 ISO8601，统一格式 |
| Vehicle-side | std::cout / std::cerr | 无 | 自定义前缀（如 `[MQTT]`） | 无 | 建议统一日志函数，加时间戳与 tag |
| backend | std::cout / std::cerr | 无 | 前缀 `[Backend][...]` | 部分（vin/session_id 打在日志行） | 建议加时间戳与统一格式 |
| client | qDebug / qWarning | 有（Qt 默认） | 无统一 tag | 无 | 建议加统一前缀（如 `[CLIENT][MODULE]`），vin/session_id |
| media（ZLM） | 容器日志（二选一） | 有 | 有 | 需按 ZLM 配置调 | 无需改仓库，调 config.ini 即可 |

---

## 3. 统一日志格式建议

### 3.1 文本格式（推荐用于所有模块）

```text
<ISO8601_TZ> [MODULE:TAG] <消息> key=value key=value ...
```

- **时间戳**：`2026-02-23T12:34:56.789Z`（UTC，带毫秒）。
- **MODULE**：`backend` / `carla-bridge` / `client` / `Vehicle-side` / `media`。
- **TAG**：`MQTT` / `HTTP` / `ZLM` / `WEBRTC` / `CONTROL` / `AUTH` / `SESSION` / `RECORD` 等。
- **上下文字段**：`vin=`、`session_id=`、`peer=`（mqtt/zlm/backend）、`err=`、`code=`、`url=`、`topic=` 等。

**示例**：

```text
2026-02-23T12:34:56.789Z [carla-bridge:MQTT] 收到 vehicle/control 消息 topic=vehicle/control vin=carla-sim-001 type=start_stream
2026-02-23T12:34:56.790Z [backend:SESSION] 创建会话 vin=carla-sim-001 session_id=xxx controller_user_id=yyy
2026-02-23T12:34:57.123Z [client:WEBRTC] 连接失败 url=whep://... err=ConnectionClosed state=closed
```

### 3.2 JSON 日志（可选）

若某些模块需要更结构化的日志（便于采集与分析），可按需输出：

```json
{"ts":"2026-02-23T12:34:56.789Z","module":"carla-bridge","tag":"MQTT","event":"message_received","topic":"vehicle/control","vin":"carla-sim-001","type":"start_stream"}
```

但注意：
- 容器日志通常按行读取，JSON 占一行。
- 不要混用文本与 JSON，避免解析歧义。
- 开发调试阶段推荐文本；生产可按需升级。

---

## 4. 各模块改进建议

### 4.1 carla-bridge（Python）

- **时间戳**：改用 `datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"`。
- **模块名**：统一加 `carla-bridge:` 前缀。
- **字段**：尽可能在每条日志里带 `vin=`（若适用）。

示例：

```python
import datetime, timezone

def _log(tag, msg, *args):
    ts = datetime.datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
    line = f"{ts} [carla-bridge:{tag}] {msg}" % args if args else f"{ts} [carla-bridge:{tag}] {msg}"
    print(line, flush=True)
```

### 4.2 backend / Vehicle-side（C++）

- 建议封装统一的日志函数，带时间戳与模块名。
- 字段：`vin=`、`session_id=`、`url=`、`topic=`、`code=`、`err=` 等。

示例：

```cpp
#include <chrono>
#include <iomanip>
#include <sstream>

std::string log_ts() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    gmtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << tm.tm_year+1900 << '-'
        << std::setw(2) << tm.tm_mon+1 << '-'
        << std::setw(2) << tm.tm_mday << 'T'
        << std::setw(2) << tm.tm_hour << ':'
        << std::setw(2) << tm.tm_min << ':'
        << std::setw(2) << tm.tm_sec << '.'
        << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

#define LOG_TAG(MODULE, TAG, MSG, ...) \
    std::cout << log_ts() << " [" #MODULE ":" #TAG "] " \
        << fmt::format(MSG, __VA_ARGS__) << std::endl;

LOG_TAG(backend, SESSION, "创建会话 vin={} session_id={}", vin, session_id);
```

### 4.3 client（Qt）

- 使用 `qInstallMessageHandler` 统一输出，加前缀与字段。

示例：

```cpp
#include <QDateTime>
#include <QDebug>

void customMessageHandler(QtMsgType type, const QMessageLogContext &ctx,
                         const QString &msg) {
    QString ts = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddThh:mm:ss.zzz") + "Z";
    QString tag;
    switch (type) {
        case QtDebugMsg:    tag = "DEBUG"; break;
        case QtWarningMsg:  tag = "WARN"; break;
        case QtCriticalMsg: tag = "CRIT"; break;
        case QtFatalMsg:   tag = "FATAL"; break;
    }
    QString prefix = QString("%1 [client:%2] ").arg(ts).arg(tag);
    // 可根据 ctx.category 等加细分 tag
    fprintf(stderr, "%s%s\n", prefix.toUtf8().constData(), msg.toUtf8().constData());
}

// 在 main() 开始处安装
qInstallMessageHandler(customMessageHandler);
```

---

## 5. 日志收集与分析工具配套

- **scripts/analyze.sh**：一键收集各模块日志与交互记录到 `diags/<timestamp>` 目录。
- **scripts/auto_diagnose.py**：基于关键词与模式做自动诊断（失败现象 → 可能原因 → 排查步骤）。
- **docs/ERROR_CODES.md**：统一错误码清单（日志中请使用 `code=` / `err=` 便于匹配与告警）。
- **docs/INTERACTION_RECORDING.md**：NDJSON 格式的交互记录与时间线分析。
- **docs/TROUBLESHOOTING_RUNBOOK.md**：常见问题知识库（见下一节）。

---

## 6. 演进路线

- **MVP（当前）**：各模块保持现有日志风格，但鼓励在关键路径加 vin/session_id 等上下文字段；统一文档说明。
- **V1**：在 backend/Vehicle-side 加统一日志函数与时间戳；client 加统一前缀；analyze.sh 收集与分析。
- **V2**：按需引入结构化 JSON 日志（配合集中日志系统）；与监控告警联动。

---

## 7. 注意事项

- 避免多行日志（换行导致 grep/分析失效），尽量用 `key=value` 在一行表达。
- 错误/异常处必须写 `err=` 与 `code=`，便于诊断脚本自动提取。
- 时延相关日志加 `elapsed_ms=`、`rtt_ms=` 等，用于性能分析。
