# 视频流故障排查指南

## 问题现象

客户端使用 WebRTC 播放视频流时，ZLM 返回 "stream not found" 错误。

## 快速定位：CARLA 仿真 vs 车端

| 场景 | VIN | 推流来源 | 验证脚本 |
|------|-----|----------|----------|
| **CARLA 仿真** | carla-sim-001 | CARLA Bridge（carla-server 容器） | `./scripts/verify-carla-stream-chain.sh` |
| **车端推流** | E2ETESTVIN0000001 等 | Vehicle-side 推流脚本 | `./scripts/verify-stream-e2e.sh` |

**若选车 carla-sim-001 后 stream not found**：先执行 `./scripts/verify-carla-stream-chain.sh` 预热推流链路（发送 start_stream → Bridge 推流 → ZLM 有流），通过后再在客户端点击「连接车端」。CARLA Bridge 启动需约 2 分钟（CARLA 服务 + 45s 等待），拉流延迟已设为 25s。

## 诊断步骤

### 1. 检查推流进程是否运行

```bash
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec vehicle ps aux | grep ffmpeg | grep -v grep
```

**预期结果**：应该看到 4 个 ffmpeg 进程（cam_front, cam_rear, cam_left, cam_right）

**如果进程不存在**：
- 检查车端是否收到 `start_stream` 消息
- 检查推流脚本是否执行成功
- 检查数据集目录是否挂载正确

### 2. 检查 HTTP-FLV 流是否可访问

```bash
for stream in cam_front cam_rear cam_left cam_right; do
  echo -n "$stream: "
  curl -sI -m 2 "http://127.0.0.1:80/teleop/${stream}.live.flv" | head -1
done
```

**预期结果**：所有流返回 `HTTP/1.1 200 OK`

**如果返回 404**：
- 推流可能未成功注册到 ZLM
- 检查 RTMP 推流是否成功
- 检查 ZLM 日志

### 3. 检查车端日志

```bash
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml logs vehicle --tail 100 | grep -E "start_stream|推流|ffmpeg|ERROR"
```

**预期结果**：
- 看到 `[Control] 收到 start_stream，启动数据集推流`
- 看到 `[Control] 执行推流脚本`
- 看到推流脚本的输出

### 4. 重新触发推流

如果推流进程不存在，重新发送 `start_stream` 消息：

```bash
# 服务名为 teleop-mosquitto（与 docker-compose.yml 一致）
docker compose -f docker-compose.yml exec -T teleop-mosquitto mosquitto_pub \
  -h localhost -p 1883 \
  -t "vehicle/control" \
  -m '{"type":"start_stream","vin":"carla-sim-001","timestampMs":1770605395525}'
```

等待 5-10 秒后检查推流进程是否启动。

### 5. 检查数据集目录

```bash
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec vehicle \
  bash -c 'test -d /data/sweeps/CAM_FRONT && echo "✓ CAM_FRONT exists" || echo "✗ CAM_FRONT missing"'
```

**预期结果**：所有相机目录都存在

## 常见问题

### 问题 1：推流进程启动后立即退出

**可能原因**：
- 数据集目录不存在或无法访问
- RTMP 推流地址错误
- ffmpeg 命令执行失败

**解决方法**：
1. 检查数据集目录挂载：`docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec vehicle ls -la /data/sweeps/`
2. 检查环境变量：`docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec vehicle env | grep -E "ZLM|SWEEPS"`
3. 手动执行推流脚本：`docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec vehicle bash /app/scripts/push-nuscenes-cameras-to-zlm.sh`

### 问题 2：HTTP-FLV 流存在但 WebRTC 播放失败

**可能原因**：
- WebRTC 播放需要流先完全注册到 ZLM
- ZLM WebRTC 配置问题
- 客户端 WebRTC 连接配置问题

**解决方法**：
1. 等待推流启动后 10-15 秒再尝试 WebRTC 播放
2. 检查 ZLM WebRTC 配置：`docker compose -f docker-compose.yml exec zlmediakit cat /opt/media/conf/config.ini | grep -A 10 "\[rtc\]"`
3. 检查客户端 WebRTC 连接日志

### 问题 3：ZLM API secret 验证失败

**现象**：`getMediaList` API 返回 `-100 Incorrect secret`

**解决方法**：
- 使用 HTTP-FLV 探测流是否存在（无需 secret）
- 检查 ZLM 配置文件中的 secret 是否正确
- 从 ZLM 容器内访问 API（127.0.0.1 可能不需要 secret）

## 验证脚本

使用 `scripts/verify-stream-e2e.sh` 进行端到端验证：

```bash
bash scripts/verify-stream-e2e.sh
```

该脚本会：
1. 检查服务状态
2. 等待车端 MQTT 订阅
3. 发送 `start_stream` 消息
4. 等待流注册到 ZLM
5. 验证四路流是否可访问

## 快速修复命令

如果推流进程不存在，执行以下命令重新启动：

```bash
# 1. 发送 start_stream 消息（CARLA 用 carla-sim-001，车端用对应 VIN）
docker compose -f docker-compose.yml exec -T teleop-mosquitto mosquitto_pub \
  -h localhost -p 1883 \
  -t "vehicle/control" \
  -m '{"type":"start_stream","vin":"carla-sim-001","timestampMs":1770605395525}'

# 2. 等待 5 秒
sleep 5

# 3. 检查推流进程
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec vehicle \
  ps aux | grep ffmpeg | grep -v grep | wc -l

# 4. 验证 HTTP-FLV 流
for stream in cam_front cam_rear cam_left cam_right; do
  echo -n "$stream: "
  curl -sI -m 2 "http://127.0.0.1:80/teleop/${stream}.live.flv" | head -1
done
```

## 相关文档

- [车辆底盘数据上传文档](./VEHICLE_CHASSIS_DATA_UPLOAD.md)
- [推流脚本文档](../scripts/push-nuscenes-cameras-to-zlm.sh)
- [端到端验证脚本](../scripts/verify-stream-e2e.sh)
