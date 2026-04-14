# 视频流显示问题修复总结

## 问题描述

客户端无法显示视频流，日志显示 `stream not found` 错误。

## 根本原因

1. **推流检查逻辑错误**：`is_streaming_running()` 函数使用 `pgrep -f` 检查推流进程时，会匹配到 `pgrep` 命令本身，导致误判推流已在运行，从而跳过启动推流脚本。

2. **时序竞争**：客户端发送 `start_stream` 后**立即**发起 WebRTC 拉流，而车端需 5~15s 才能完成：收令 → 启动脚本 → 四路 ffmpeg 启动 → 连接 ZLM → 流注册。客户端原先仅重试 3 次、间隔 2s，总等待约 6s，不足以保证流就绪。

3. **日志不足**：无法精确定位是「未启动推流」还是「推流已启动但 ZLM 尚未就绪」。

## 修复内容

### 1. 车端精确定位日志 (`Vehicle-side/src/control_protocol.cpp`)

- 收到 `start_stream` 时打印：`收到 start_stream，检查并启动数据集推流（时间点: 客户端即将或已发起 WebRTC 拉流）`
- 检查前打印：`is_streaming_running()=true/false（收到 start_stream 后检查）`
- fork 脚本后打印：`推流脚本已 fork，ZLM 流约 5~15s 后可用，客户端将自动重试拉流`
- 500ms 后打印：`500ms 后 is_streaming_running()=true/false`
- 再等 2s 打印：`2.5s 后 is_streaming_running()=true/false`

便于从日志判断：是否误判「已在运行」、脚本是否成功 fork、何时检测到推流进程。

### 2. 客户端重试策略 (`client/src/webrtcclient.cpp` + `webrtcclient.h`)

- 将 **stream not found** 重试次数由 3 次改为 **8 次**，间隔由 2s 改为 **3s**（总等待约 24s，覆盖车端 5~15s 就绪时间）。
- 日志：每次拉流尝试打印 `拉流尝试 stream=...（若 stream not found 将最多重试 8 次，间隔 3s）`；收到 -400 时打印 `第 N 次尝试拉流（最多 8 次重试），还剩 M 次，3s 后重试`；成功时打印 `✓ 拉流成功 stream=...`。

### 3. 推流脚本日志 (`scripts/push-nuscenes-cameras-to-zlm.sh`)

- 脚本启动后打印：`[PUSH] 推流脚本已启动 PID=$$，正在启动四路 ffmpeg（ZLM 流约 5~15s 后可用）`
- 四路启动完成后打印：`[PUSH] 四路 ffmpeg 已全部启动（共 N 路），ZLM 流即将可用`

### 4. 修复推流检查逻辑 (`Vehicle-side/src/control_protocol.cpp`)

**问题代码**：
```cpp
std::string check_cmd = "pgrep -f 'ffmpeg.*" + rtmp_base + "/cam_front' >/dev/null 2>&1";
int ret = std::system(check_cmd.c_str());
return (ret == 0);
```

**修复后**：
```cpp
// ★ 修复：使用更精确的检查，避免匹配到 pgrep 命令本身
std::string check_cmd = "ps aux | grep -E 'ffmpeg.*" + rtmp_base + "/cam_front' | grep -v grep | grep -v pgrep >/dev/null 2>&1";
int ret = std::system(check_cmd.c_str());
return (ret == 0);
```

### 2. 验证推流状态

推流进程检查命令：
```bash
# 检查推流进程
docker exec remote-driving-vehicle-1 ps aux | grep ffmpeg | grep -v grep

# 检查 RTMP 推流是否成功
docker exec remote-driving-vehicle-1 timeout 3 ffmpeg -i rtmp://zlmediakit:1935/teleop/cam_front -frames:v 1 -f null - 2>&1 | grep -E "Stream|Input"

# 检查 ZLM 流注册（需要正确的 secret）
curl -s "http://127.0.0.1:80/index/api/getMediaList?secret=<secret>&app=teleop"
```

## 自动化验证

全链路已启动后执行：

```bash
bash scripts/verify-streaming-fix.sh
```

脚本会：清理旧推流 → 发送 start_stream → 等待四路 ffmpeg 就绪 → 检查车端日志关键行 → 验证 ZLM 上 cam_front 可拉流。

## 手动验证步骤

1. **清理旧进程**：
   ```bash
   docker exec remote-driving-vehicle-1 bash -c 'pkill -9 ffmpeg; rm -f /tmp/push-*.pid /tmp/push-*.lock'
   ```

2. **发送 start_stream 命令**（宿主机在仓库根 `source scripts/lib/mqtt_control_json.sh` 后执行，保证含 `schemaVersion`/`timestampMs`/`seq`）：
   ```bash
   source scripts/lib/mqtt_control_json.sh
   docker exec teleop-mosquitto mosquitto_pub -h mosquitto -p 1883 -t "vehicle/control" -m "$(mqtt_json_start_stream "123456789")"
   ```

3. **等待推流启动**（约 5-10 秒）：
   ```bash
   sleep 10
   ```

4. **验证推流进程**：
   ```bash
   docker exec remote-driving-vehicle-1 ps aux | grep ffmpeg | grep -v grep | wc -l
   # 应该输出 4（四路流）
   ```

5. **验证 RTMP 推流**：
   ```bash
   docker exec remote-driving-vehicle-1 timeout 3 ffmpeg -i rtmp://zlmediakit:1935/teleop/cam_front -frames:v 1 -f null - 2>&1 | grep -E "Stream|Input"
   # 应该显示流信息
   ```

6. **启动客户端并连接**：
   - 客户端会自动发送 `start_stream` 命令
   - 等待推流启动（约 5-10 秒）
   - 客户端会自动重试 WebRTC play（最多 3 次，每次间隔 2 秒）

## 预期行为

1. **推流启动**：收到 `start_stream` 后，4 个 ffmpeg 进程应在 5-10 秒内启动
2. **流注册**：推流启动后，流应在 2-5 秒内注册到 ZLMediaKit
3. **客户端拉流**：客户端连接后，WebRTC play 应在流注册后成功（可能需要重试 1-2 次）

## 故障排查

### 推流未启动

检查车端日志：
```bash
docker logs remote-driving-vehicle-1 --tail 50 | grep -E "Control|推流|start_stream"
```

检查推流脚本日志：
```bash
docker exec remote-driving-vehicle-1 tail -50 /tmp/push-stream.log
```

### 流未注册到 ZLM

检查 ZLM 日志：
```bash
docker logs teleop-zlmediakit --tail 50 | grep -E "rtmp|publish|teleop|cam_front"
```

检查网络连接：
```bash
docker exec remote-driving-vehicle-1 bash -c 'timeout 2 bash -c "exec 3<>/dev/tcp/zlmediakit/1935 && echo \"连接成功\" && exec 3<&-"'
```

### 客户端拉流失败

检查客户端日志中的 WebRTC 错误：
- `stream not found`：流尚未注册，等待重试
- `Assertion failed`：SDP 格式问题（已修复）
- 其他错误：检查网络连接和 ZLM 配置

## 相关文件

- `Vehicle-side/src/control_protocol.cpp`：推流检查逻辑修复
- `client/src/webrtcclient.cpp`：WebRTC play 重试逻辑
- `scripts/push-nuscenes-cameras-to-zlm.sh`：推流脚本

## 后续优化建议

1. **推流状态监控**：添加推流进程健康检查，自动重启失败的推流
2. **流注册通知**：使用 ZLM hook 通知客户端流已就绪
3. **重试策略优化**：根据流注册时间动态调整重试间隔
