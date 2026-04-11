# CARLA Bridge（远程驾驶仿真闭环）

将 CARLA 仿真器与项目现有「客户端 ↔ MQTT ↔ 车端 ↔ ZLM」链路对接，实现远程驾驶仿真闭环。

## 依赖

- 宿主机已安装 [CARLA Docker 镜像](https://hub.docker.com/r/carlasim/carla) 并启动（端口 2000）。
- 本工程 Mosquitto、ZLMediaKit 已启动（compose 或宿主机）。

## 安装

```bash
cd carla-bridge
pip install -r requirements.txt
```

CARLA 版本需与服务器一致（例如服务器 `carlasim/carla:latest` 对应 0.9.x，可 `pip install carla==0.9.15`）。

## 配置环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `CARLA_HOST` | `host.docker.internal` 或 `127.0.0.1` | CARLA 服务器地址 |
| `CARLA_PORT` | `2000` | CARLA 端口 |
| `CARLA_MAP` | 空（使用服务端当前世界） | 场景/地图名，如 `Town01`、`Town02`；Bridge 连接后会执行 `load_world(CARLA_MAP)` |
| `MQTT_BROKER` | `127.0.0.1` 或 `mosquitto`（compose 内） | MQTT Broker 地址 |
| `MQTT_PORT` | `1883` | MQTT 端口 |
| `ZLM_HOST` | `127.0.0.1` 或 `zlmediakit` | ZLM 地址 |
| `ZLM_RTMP_PORT` | `1935` | RTMP 端口 |
| `ZLM_APP` | `teleop` | 应用名（与现有车端一致） |
| `CAMERA_WIDTH` | `640` | 相机分辨率宽（`docker-compose.carla.yml` 默认可覆盖为 1280=720P） |
| `CAMERA_HEIGHT` | `480` | 相机分辨率高（配合宽度，720P 为 720） |
| `CAMERA_FPS` | `10`（脚本默认） | 推流帧率；README 旧值 15 以 `carla_bridge.py` 为准 |
| `VIDEO_BITRATE_KBPS` | `2000` | 每路 libx264 目标码率（kbps）；例如 512=低码率省带宽 |
| `VIN` | `carla-sim-001` | 车辆标识 |

**四路推流**：收到 MQTT `start_stream` 后，在车辆上挂载前/后/左/右四路相机，取帧转 BGR，经 ffmpeg 推 RTMP 到 ZLM（`rtmp://{ZLM_HOST}:{ZLM_RTMP_PORT}/{ZLM_APP}/cam_front` 等）。需宿主机或容器内安装 **ffmpeg**。收到 `stop_stream` 时停止推流并销毁相机。

## 运行

1. 启动 CARLA 服务器（宿主机）：
   ```bash
   docker run -d --name carla-server -p 2000-2002:2000-2002 --runtime=nvidia -e NVIDIA_VISIBLE_DEVICES=0 carlasim/carla:latest
   ```

2. 启动本工程栈（Mosquitto、ZLM 等），不启动原 Vehicle-side 车端。

3. 运行 bridge（宿主机或与 compose 同网容器内）：
   ```bash
   export CARLA_HOST=127.0.0.1
   export MQTT_BROKER=127.0.0.1
   python carla_bridge.py
   ```

4. 在远程驾驶客户端中登录、**选车「carla-sim-001」**（需后端种子数据包含该 VIN）、进入驾驶页后点击「连接车端」。当前版本已实现**控制与底盘状态闭环**及**四路相机 → ZLM 推流**（收到 `start_stream` 后自动 spawn 四路相机并推流，约 6s 后客户端拉流可见四宫格画面；`stop_stream` 停止并销毁相机）。

**车辆与视角**：Bridge 启动时自动 spawn 车辆；CARLA 仿真窗口默认镜头跟随车辆（第三人称）。可配置 `CARLA_VEHICLE_BP`（如 `vehicle.tesla.model3`）、`CARLA_SPAWN_INDEX`、`SPECTATOR_FOLLOW_VEHICLE`，详见 [CARLA_VEHICLE_VIEW_GUIDE.md](../docs/CARLA_VEHICLE_VIEW_GUIDE.md)。

   **一键启动与操作说明**：见 [docs/CARLA_CLIENT_STREAM_GUIDE.md](../docs/CARLA_CLIENT_STREAM_GUIDE.md)；也可先运行 `./scripts/start-carla-sim.sh` 再按提示启动 CARLA、Bridge 与客户端。

## 协议说明

- **订阅** `vehicle/control`：与 Vehicle-side 相同 JSON（`type`：`start_stream` / `stop_stream` / `remote_control`，以及 `steering`、`throttle`、`brake`、`gear` 等）。
- **发布** `vehicle/status`：与现有车端相同 JSON（`speed`、`gear`、`steering`、`throttle`、`brake`、`battery`、`odometer` 等），数据来源于 CARLA 仿真状态。
- **推流**：四路 RTMP 到 ZLM，流名 `cam_front`、`cam_rear`、`cam_left`、`cam_right`（app=teleop），与现有车端一致。

详细设计见 [docs/CARLA_INTEGRATION.md](../docs/CARLA_INTEGRATION.md)。

## 验证四路相机

1. **单元测试**（不依赖 CARLA，推荐先跑）：
   ```bash
   cd carla-bridge
   pip install -r requirements.txt   # 含 numpy，to_bgr 测试才不跳过
   python3 -m unittest tests.test_cameras -v
   ```
   覆盖：`to_bgr` 形状/类型、四路配置数量与 stream_id、推流 worker 在收到 None/stop 时退出。

2. **集成验证**（需 CARLA 已启动）：在真实 CARLA 中 spawn 车辆与四路相机，每路收一帧并检查形状。
   ```bash
   cd carla-bridge
   export CARLA_HOST=127.0.0.1   # 与 CARLA 一致
   python3 verify_cameras.py
   ```
   退出码 0 表示四路均收到一帧且为 (height, width, 3) BGR；非 0 表示超时或异常。
