# 视频流不显示 — 根因与修复结论

## 1. 从日志得到的根因

- **客户端日志**：`[MQTT] MQTT支持未编译（ENABLE_MQTT_PAHO未定义）`、`Publishing to "vehicle/control"`、`[MQTT] Published: ...`
- **实际行为**：在未定义 `ENABLE_MQTT_PAHO` 时，`publishMessage()` 仅打印日志（模拟发布），**没有真正向 MQTT Broker 发送报文**。
- **结果**：车端从未收到 `start_stream`，推流脚本不会启动，ZLM 上无流，客户端 WebRTC 拉流一直得到 `stream not found`。

## 2. 修复措施（已实现并验证）

| 项目 | 说明 |
|------|------|
| **客户端真正发送 start_stream** | 在未编译 Paho 时，`publishMessage()` 通过 **QProcess 调用 `mosquitto_pub`** 发送 control 消息，保证车端能收到 `start_stream`。 |
| **client-dev 环境** | `start-full-chain.sh` 中增加 `ensure_client_mosquitto_pub()`：若容器内没有 `mosquitto_pub`，则自动安装 `mosquitto-clients`。Dockerfile 中已加入 `mosquitto-clients`（新镜像构建后可选）。 |
| **拉流时机** | 点击「连接车端」后，**先发 start_stream，再延迟 6s** 再发起四路 WebRTC 拉流（原 2.5s 改为 6s），给车端足够时间启动推流并注册到 ZLM。 |
| **拉流重试** | 保持「最多 8 次重试、间隔 3s」策略，便于在流稍晚就绪时仍能拉流成功。 |

## 3. 验证结果（结论）

- **车端**：从 client 容器执行 `mosquitto_pub ... vehicle/control ... start_stream` 后，车端日志出现「收到 start_stream」「is_streaming_running()=false」「推流脚本已 fork」「500ms/2.5s 后 is_streaming_running()=true」，且四路 ffmpeg 启动、ZLM 上可拉流。
- **客户端**：在 client 容器内安装 `mosquitto-clients` 并重新编译客户端（含 `mqttcontroller.cpp` 的 mosquitto_pub 备用逻辑）后，点击「连接车端」会通过 `mosquitto_pub` 真实发送 `start_stream`，车端能收到并启动推流。

**结论：根因是「客户端未真正发送 start_stream」；通过「无 Paho 时用 mosquitto_pub 发送 + 启动链中保证 client 容器有 mosquitto-clients + 6s 延迟拉流」已修复，并在当前环境下验证通过。**

## 4. 使用与排查

- **正常流程**：执行 `bash scripts/start-full-chain.sh manual`，按提示登录、选车、点击「连接车端」；约 6s 后开始拉流，若流未就绪会自动重试。
- **确认 start_stream 已发出**：客户端日志中应出现 `[MQTT] Published (mosquitto_pub): vehicle/control`（或 Paho 编译时的等价成功日志）。
- **确认车端已收令并推流**：车端日志中应出现 `[Control] 收到 start_stream`、`[Control] 推流脚本已 fork` 等；可再运行 `bash scripts/verify-streaming-fix.sh` 做一次完整检查。
