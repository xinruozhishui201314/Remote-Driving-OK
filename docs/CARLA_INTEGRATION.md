# CARLA 仿真与远程驾驶车端闭环集成

## 0) Executive Summary

- **目标**：用宿主机上的 `carlasim/carla:latest` 镜像与项目现有「客户端 ↔ MQTT ↔ 车端 ↔ ZLM」链路对接，实现**远程驾驶仿真闭环**：操作员在客户端发控制指令 → MQTT → **CARLA 桥接** → CARLA 仿真车执行 → 相机画面推 ZLM → 客户端拉流 + 底盘状态从 CARLA 回传 MQTT。
- **CARLA 现成内容**：镜像内自带多张地图（Town01～Town10 等）、内置传感器与车辆模型，无需额外下载场景数据即可做基础驾驶仿真；进阶可用 OpenDRIVE/Scenario Runner。
- **实现方式**：新增 **CARLA Bridge**（Python）：订阅 `vehicle/control`、控制 CARLA 车辆、将 CARLA 相机推送到 ZLM、向 `vehicle/status` 发布仿真状态。

---

## 1) 架构与数据流

```
┌─────────────────┐     vehicle/control      ┌──────────────────┐     CARLA Python API      ┌─────────────────┐
│  远程驾驶客户端   │ ──────────────────────► │  Mosquitto       │ ◄─────────────────────── │  CARLA Bridge   │
│  (client-dev)   │     (steering/throttle/  │  (MQTT Broker)   │                           │  (Python)       │
│                 │      brake/gear/          │                 │  vehicle/status           │                 │
│                 │      start_stream)        │                 │ ────────────────────────► │  订阅 control   │
└────────┬────────┘                           └────────┬───────┘                           │  控制 CARLA 车  │
         │                                              │                                  │  相机 → ZLM      │
         │ 拉流 (WebRTC/HTTP-FLV)                        │ 发布 status                     │  状态 → MQTT    │
         │                                              │                                  └────────┬────────┘
         ▼                                              │                                           │
┌─────────────────┐                           ┌────────▼───────┐                           ┌────────▼────────┐
│  ZLMediaKit     │ ◄── RTMP 四路流 ────────── │  CARLA Server  │ ◄── 控制 + 取图 ────────── │  CARLA (Docker) │
│  (流媒体)       │     cam_front/rear/left/   │  (carlasim/    │     (throttle/steer/      │  carlasim/carla │
│                 │     right                 │   carla:latest)│      camera.listen)       │  :latest        │
└─────────────────┘                           └────────────────┘                           └─────────────────┘
```

- **vehicle/control**：与现有车端一致，JSON 含 `type`（`start_stream`|`stop_stream`|`remote_control`）、`steering`、`throttle`、`brake`、`gear`、`vin`、`timestampMs` 等。
- **vehicle/status**：与现有车端一致，JSON 含 `speed`、`gear`、`steering`、`throttle`、`brake`、`battery`、`odometer` 等，由 CARLA 桥接从仿真器读取并发布。

---

## 2) CARLA 现成场景与数据

| 类型 | 说明 |
|------|------|
| **内置地图** | 镜像内自带 Town01～Town10 等，`client.load_world('Town01')` 即可，**无需额外下载**。 |
| **车辆/传感器** | 内置车辆蓝图（如 `vehicle.tesla.model3`）、相机（RGB）、语义分割等，可直接 spawn。 |
| **OpenDRIVE** | 支持 `.xodr` 自定义道路，`client.generate_opendrive_world(odr_path)` 可生成新地图。 |
| **Scenario Runner** | 需单独安装/脚本，可定义场景（车辆、行人、路线），用于进阶测试。 |

结论：**仅用 Docker 镜像即可做闭环**，无需额外场景数据包；若要自定义道路或场景再考虑 OpenDRIVE / Scenario Runner。

---

## 3) 宿主机与工程对接方式

### 3.1 网络与端口

- CARLA 默认端口：**2000**（主）、2001、2002。
- 本工程：Mosquitto 1883、ZLM 1935(RTMP)/80(HTTP) 等，均在 `teleop-network`。
- 将 CARLA 与 **carla-bridge** 接入同一 Docker 网络，bridge 内用 `carla:2000` 连 CARLA、`mosquitto:1883` 连 MQTT、`zlmediakit:1935` 推 RTMP。

### 3.2 两种部署方式

| 方式 | 说明 |
|------|------|
| **A. CARLA 宿主机直跑** | 宿主机 `docker run ... carlasim/carla:latest`，bridge 与 Mosquitto/ZLM 在 compose 内，bridge 连 `host.docker.internal:2000`（或宿主机 IP）。 |
| **B. CARLA 进 Compose** | 将 CARLA 作为 compose 服务（需 GPU、`runtime: nvidia`），与 bridge、mosquitto、zlmediakit 同网。 |

推荐先 **A**：宿主机起 CARLA，compose 只起 bridge，便于调试与复用现有 `carlasim/carla:latest`。

---

## 4) CARLA Bridge 职责

1. **连 CARLA**：`carla.Client(host, 2000)`，加载地图（如 Town01），spawn 车辆与四路相机（前/后/左/右）。
2. **订阅 MQTT**：`vehicle/control`，解析 JSON，处理 `start_stream`（开始推流）、`stop_stream`（停止）、`remote_control`（启用/禁用）、以及 steering/throttle/brake/gear。
3. **控制 CARLA 车辆**：将 throttle/brake/steer 应用到 `vehicle.apply_control(carla.VehicleControl(...))`，gear 映射到 CARLA 驾驶模式。
4. **相机 → ZLM**：从 `camera.listen()` 取帧，编码（如 H264）后经 RTMP 推到 `zlmediakit:1935/teleop/cam_front` 等四路（与现有车端流名一致）。
5. **状态 → MQTT**：定时（如 50Hz）从 CARLA 读 `vehicle.get_velocity()`、转向角等，组 JSON 发布到 `vehicle/status`（格式与现有车端一致）。

---

## 5) 快速启动（宿主机已有 CARLA 镜像）

### 5.1 启动 CARLA 服务器与场景（宿主机）

**推荐：用项目内一键脚本（同时启动所有节点 + CARLA + 场景 + Bridge）**

```bash
./scripts/start-all-nodes.sh
```

默认加载场景 **Town01**。指定其他场景：`CARLA_MAP=Town02 ./scripts/start-all-nodes.sh`。内置地图：Town01～Town10 等（见 [CARLA 地图](https://carla.readthedocs.io/en/latest/core_map/)）。

**仅手动启动 CARLA 容器并指定场景：**

```bash
# 需 NVIDIA 驱动 + nvidia-container-toolkit；/Game/Maps/Town01 为场景路径
docker run -d --name carla-server \
  -p 2000-2002:2000-2002 \
  --runtime=nvidia \
  -e NVIDIA_VISIBLE_DEVICES=0 \
  carlasim/carla:latest \
  bash CarlaUE4.sh /Game/Maps/Town01 -RenderOffScreen -nosound
```

无 GPU 时可使用无头模式（若镜像支持），或使用带 `-opengl` 的镜像变体。**CARLA Bridge** 也可通过环境变量 `CARLA_MAP=Town01` 在连接后执行 `load_world('Town01')` 切换场景（若服务端未预加载地图）。

### 5.2 启动本工程栈 + CARLA Bridge

- 启动现有全链路（Mosquitto、ZLM、后端、client-dev 等），**不启动** 原 Vehicle-side 车端（或起 CARLA 桥接后不再起原车端）。
- 在项目内启动 **carla-bridge**（见下节），连接 `host.docker.internal:2000`（Linux 需 Docker 20.10+ 或宿主机 IP）、`mosquitto:1883`、`zlmediakit:1935`。

### 5.3 客户端操作

与现有一致：登录 → **选车「carla-sim-001」**（需后端种子数据包含该 VIN）→ 进入驾驶页 → 点击「连接车端」→ 发 `start_stream`；bridge 收到后开始推四路流并发布 `vehicle/status`，约 6s 后客户端拉流并显示四宫格画面与底盘数据，即可实现**远程驾驶仿真闭环**。

**详细步骤与故障排查**：见 [CARLA_CLIENT_STREAM_GUIDE.md](CARLA_CLIENT_STREAM_GUIDE.md)。可用 `./scripts/start-carla-sim.sh` 仅启动基础栈（不启动车端），再按提示启动 CARLA、Bridge 与客户端。

---

## 6) 项目内 CARLA Bridge 占位与扩展

- 代码与说明见 **`carla-bridge/`** 目录（README + Python 脚本骨架）。
- **当前实现**：`carla_bridge.py` 已实现 **控制 + 状态闭环**（MQTT 收 control → 控制 CARLA 车 → 发布 vehicle/status）；**相机推流到 ZLM** 为预留扩展（在 `start_stream` 时 spawn 四路相机、取帧编码推 RTMP 即可与现有客户端四路流一致）。
- 协议与主题与现有车端完全一致：`vehicle/control`、`vehicle/status`、ZLM 流名 `teleop/cam_*`，便于与原车端切换或 A/B 测试。

---

## 7) 参考

- [CARLA Docker 构建与运行](https://carla.readthedocs.io/en/latest/build_docker/)
- [CARLA Python API 连接](https://carla.readthedocs.io/en/latest/connecting_the_client/)
- [CARLA 地图与导航](https://carla.readthedocs.io/en/latest/core_map/)
- 本仓库：`Vehicle-side/README.md`、`docs/VEHICLE_CHASSIS_DATA_UPLOAD.md`、`deploy/mosquitto/README.md`
