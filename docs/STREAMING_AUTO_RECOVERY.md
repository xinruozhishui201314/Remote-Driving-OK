# 推流自动恢复机制

## 概述

本文档说明远程驾驶系统中推流进程的自动恢复机制，确保无论 Docker 容器如何重启，推流功能都能自动恢复并稳定运行。

## 问题背景

在之前的实现中，存在以下问题：

1. **容器重启后推流丢失**：Docker 容器重启后，推流进程（ffmpeg）会丢失，需要手动重新发送 `start_stream` 命令才能恢复。
2. **重复启动风险**：推流脚本没有检查现有进程，可能导致重复启动多个推流进程。
3. **进程管理不健壮**：推流进程意外退出后，无法自动恢复。

## 解决方案

### 1. 推流脚本改进

#### 1.1 PID 文件管理

两个推流脚本（`push-nuscenes-cameras-to-zlm.sh` 和 `push-testpattern-to-zlm.sh`）现在都实现了：

- **PID 文件检查**：启动前检查 PID 文件，如果进程仍在运行，跳过启动。
- **进程验证**：不仅检查 PID 是否存在，还验证进程命令行是否匹配推流脚本。
- **锁文件机制**：使用锁文件防止并发启动多个实例。
- **自动清理**：进程退出时自动清理 PID 文件和锁文件。

**PID 文件位置**：
- NuScenes 推流：`/tmp/push-nuscenes-cameras.pid`
- 测试图案推流：`/tmp/push-testpattern.pid`

**环境变量**：
- `PIDFILE_DIR`：PID 文件目录（默认：`/tmp`）

#### 1.2 进程检查逻辑

```bash
# 检查 PID 文件
if [[ -f "$PIDFILE" ]]; then
  old_pid=$(cat "$PIDFILE")
  if kill -0 "$old_pid" 2>/dev/null; then
    # 验证进程命令行
    cmdline=$(cat "/proc/$old_pid/cmdline" | tr '\0' ' ')
    if echo "$cmdline" | grep -q "push-.*-to-zlm.sh"; then
      echo "推流进程已在运行，跳过启动"
      exit 0
    fi
  fi
fi

# 检查是否有 ffmpeg 进程正在推流到相同的 RTMP 地址
if pgrep -f "ffmpeg.*${rtmp_base}/cam_front" >/dev/null; then
  echo "检测到已有推流进程，跳过启动"
  exit 0
fi
```

### 2. 车端推流管理改进

#### 2.1 推流状态跟踪

车端程序（`Vehicle-side`）现在维护推流状态：

- **状态标记**：收到 `start_stream` 命令后，设置 `g_streaming_should_be_running = true`。
- **进程检查**：实现 `is_streaming_running()` 函数，检查推流进程是否在运行。
- **自动恢复**：实现 `check_and_restore_streaming()` 函数，定期检查推流状态，如果推流应该运行但未运行，自动重启。

#### 2.2 健康检查机制

在主循环中，每 10 秒执行一次推流健康检查：

```cpp
// 主循环中
auto streamingCheckElapsed = std::chrono::duration_cast<std::chrono::seconds>(
    now - lastStreamingCheckTime).count();
if (streamingCheckElapsed >= 10) {
    check_and_restore_streaming();
    lastStreamingCheckTime = now;
}
```

**健康检查逻辑**：

1. 如果推流不应该运行（未收到 `start_stream`），跳过检查。
2. 如果推流应该运行但进程不存在：
   - 等待至少 5 秒（避免频繁重启）。
   - 调用 `run_dataset_push_script()` 重启推流。
   - 等待 500ms 后验证进程是否成功启动。

#### 2.3 启动时检查

收到 `start_stream` 命令时：

1. 设置 `g_streaming_should_be_running = true`。
2. 调用 `run_dataset_push_script()`：
   - 先检查推流是否已在运行（避免重复启动）。
   - 如果未运行，启动推流脚本。
   - 使用 `nohup` 确保进程在后台稳定运行。
   - 等待 500ms 后验证进程是否成功启动。

## 工作流程

### 正常启动流程

1. **客户端点击「连接车端」** → 发送 `start_stream` 命令。
2. **车端收到命令** → 检查推流是否已在运行。
3. **如果未运行** → 启动推流脚本（NuScenes 或测试图案）。
4. **推流脚本启动** → 检查 PID 文件和现有进程，避免重复启动。
5. **推流进程运行** → 定期健康检查确认进程存活。

### 容器重启恢复流程

1. **容器重启** → 所有进程（包括推流进程）丢失。
2. **车端程序重启** → 连接 MQTT，进入主循环。
3. **健康检查触发**（10 秒后）：
   - 检查 `g_streaming_should_be_running`（如果之前收到过 `start_stream`，仍为 `true`）。
   - 检查推流进程是否存在（不存在）。
   - **自动重启推流** → 调用 `run_dataset_push_script()`。
4. **推流恢复** → 推流进程重新启动，视频流恢复正常。

### 进程意外退出恢复流程

1. **推流进程意外退出**（例如：ffmpeg 崩溃、资源不足等）。
2. **健康检查触发**（10 秒后）：
   - 检测到推流进程不存在。
   - **自动重启推流**（等待至少 5 秒后）。
3. **推流恢复** → 推流进程重新启动。

## 配置说明

### 环境变量

| 变量名 | 说明 | 默认值 |
|--------|------|--------|
| `VEHICLE_PUSH_SCRIPT` | 推流脚本路径 | `scripts/push-nuscenes-cameras-to-zlm.sh` |
| `PIDFILE_DIR` | PID 文件目录 | `/tmp` |
| `ZLM_HOST` | ZLMediaKit 主机地址 | `127.0.0.1` |
| `ZLM_RTMP_PORT` | ZLMediaKit RTMP 端口 | `1935` |
| `ZLM_APP` | ZLMediaKit 应用名 | `teleop` |

### 健康检查参数

- **检查间隔**：10 秒（在主循环中）
- **重启间隔**：至少 5 秒（避免频繁重启）
- **启动验证延迟**：500ms（等待进程启动后验证）

## 日志说明

### 推流脚本日志

- `INFO: 推流进程已在运行 (PID: xxx)，跳过启动`
- `WARN: 检测到已有 ffmpeg 进程正在推流`
- `ERROR: 无法获取锁文件`

### 车端日志

- `[Control] 收到 start_stream，检查并启动数据集推流`
- `[Control] 推流进程已在运行，跳过启动`
- `[Control] ✓ 推流进程已成功启动`
- `[Control] 检测到推流进程未运行，尝试自动恢复...`

## 故障排查

### 问题 1：推流无法启动

**症状**：日志显示 `推流脚本执行后未检测到运行中的进程`

**排查步骤**：
1. 检查推流脚本日志：`cat /tmp/push-stream.log`
2. 检查数据集路径（NuScenes）：`echo $SWEEPS_PATH`
3. 检查 ZLMediaKit 连接：`telnet $ZLM_HOST $ZLM_RTMP_PORT`
4. 检查 ffmpeg 是否可用：`which ffmpeg`

### 问题 2：推流重复启动

**症状**：多个 ffmpeg 进程同时推流

**排查步骤**：
1. 检查 PID 文件：`cat /tmp/push-*.pid`
2. 检查进程：`ps aux | grep ffmpeg`
3. 手动清理：`killall ffmpeg && rm -f /tmp/push-*.pid /tmp/push-*.lock`

### 问题 3：容器重启后推流未恢复

**症状**：容器重启后，推流未自动恢复

**排查步骤**：
1. 检查车端日志：`docker compose logs vehicle | grep -i "stream\|推流"`
2. 确认是否收到过 `start_stream`：`grep "start_stream" /path/to/vehicle.log`
3. 检查健康检查是否触发：`grep "检测到推流进程未运行" /path/to/vehicle.log`
4. 手动触发：发送 `start_stream` 命令

### 问题 4：推流频繁重启

**症状**：日志显示频繁的自动恢复尝试

**可能原因**：
- 推流脚本启动失败（数据集路径错误、ZLMediaKit 不可达等）
- 资源不足（CPU/内存）
- 网络问题

**排查步骤**：
1. 检查推流脚本日志：`tail -f /tmp/push-stream.log`
2. 检查系统资源：`top`、`free -h`
3. 检查网络连接：`ping $ZLM_HOST`

## 验证方法

### 手动验证

1. **启动全链路**：
   ```bash
   bash scripts/start-full-chain.sh manual
   ```

2. **客户端连接**：点击「连接车端」，触发 `start_stream`。

3. **验证推流**：
   ```bash
   # 检查推流进程
   ps aux | grep ffmpeg
   
   # 检查 PID 文件
   cat /tmp/push-*.pid
   
   # 检查推流日志
   tail -f /tmp/push-stream.log
   ```

4. **模拟容器重启**：
   ```bash
   # 停止车端容器
   docker compose stop vehicle
   
   # 启动车端容器
   docker compose start vehicle
   
   # 等待 10-15 秒后检查推流是否自动恢复
   sleep 15
   ps aux | grep ffmpeg
   ```

### 自动化验证脚本

创建验证脚本 `scripts/verify-streaming-auto-recovery.sh`：

```bash
#!/bin/bash
# 验证推流自动恢复功能

echo "=== 推流自动恢复验证 ==="

# 1. 检查推流进程
echo "[1] 检查推流进程..."
if pgrep -f "ffmpeg.*rtmp://.*/cam_front" >/dev/null; then
    echo "  ✓ 推流进程正在运行"
else
    echo "  ✗ 推流进程未运行"
fi

# 2. 检查 PID 文件
echo "[2] 检查 PID 文件..."
if [ -f /tmp/push-nuscenes-cameras.pid ] || [ -f /tmp/push-testpattern.pid ]; then
    echo "  ✓ PID 文件存在"
    cat /tmp/push-*.pid 2>/dev/null
else
    echo "  ⊘ PID 文件不存在（可能未启动推流）"
fi

# 3. 模拟进程退出
echo "[3] 模拟推流进程退出..."
pkill -f "ffmpeg.*rtmp://.*/cam_front" 2>/dev/null
sleep 2

# 4. 等待自动恢复（最多 15 秒）
echo "[4] 等待自动恢复（最多 15 秒）..."
for i in {1..15}; do
    if pgrep -f "ffmpeg.*rtmp://.*/cam_front" >/dev/null; then
        echo "  ✓ 推流已自动恢复（${i} 秒后）"
        exit 0
    fi
    sleep 1
done

echo "  ✗ 推流未自动恢复（可能需要手动触发 start_stream）"
exit 1
```

## 总结

通过实现推流脚本的进程检查、PID 文件管理、锁文件机制，以及车端的推流状态跟踪和健康检查机制，系统现在能够：

1. ✅ **避免重复启动**：推流脚本检查现有进程，避免重复启动。
2. ✅ **容器重启恢复**：容器重启后，推流自动恢复（如果之前收到过 `start_stream`）。
3. ✅ **进程意外退出恢复**：推流进程意外退出后，自动重启。
4. ✅ **健壮的进程管理**：使用 PID 文件和锁文件确保进程唯一性。

这确保了无论 Docker 容器如何重启，推流功能都能自动恢复并稳定运行，满足用户的需求："运行状态一定要和docker镜像重启无关，避免程序受docker镜像的环境影响，无论docker镜像怎么重启，运行都要正常"。
