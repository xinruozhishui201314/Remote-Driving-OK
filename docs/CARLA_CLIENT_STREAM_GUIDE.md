# 远驾客户端手动连接 CARLA 仿真并观看推流

## 目标

在客户端界面**手动点击「连接车端」**后，能够看到 CARLA 仿真四路相机推流（前/后/左/右），并收到底盘状态（速度、档位等）。

## 前提

- 宿主机已安装 Docker，可选 NVIDIA 驱动（CARLA 需 GPU）。
- 后端数据库已包含仿真车 **carla-sim-001**（种子数据 `03_seed_test_data.sql` 已写入，与 e2e-test 账号同属一 account）。

## 流程概览

```
客户端点击「连接车端」→ MQTT 连接 → 发送 start_stream → CARLA Bridge 收到 → 四路相机推流到 ZLM
→ 约 6s 后客户端拉流 (teleop/cam_front 等) → 四宫格显示仿真画面
```

## 方式一：先构建 CARLA 镜像再启动 + 整链验证（推荐）

**适用于 C++ Bridge：先构建满足运行的 CARLA 镜像，再启动并做逐项功能与整链验证。**

```bash
# 1. 构建 CARLA + C++ Bridge 运行环境镜像（仅需一次）
./scripts/build-carla-image.sh

# 2. 启动所有节点（使用已构建镜像，容器内自动编译并运行 C++ Bridge）
./scripts/start-all-nodes.sh

# 3. 推流链路分步验证（推荐先跑，遇失败即停并提示修复）
./scripts/verify-carla-stream-chain.sh

# 3b. CARLA Bridge 逐项功能验证（start_stream / stop_stream / vehicle/status）
./scripts/verify-carla-bridge-cpp-features.sh

# 3c. 可选：确认视频流来自 CARLA 相机（需 USE_PYTHON_BRIDGE=1）
./scripts/verify-carla-video-source.sh

# 4. 远驾客户端到 CARLA 仿真整链验证（会话 → MQTT → 四路流）
./scripts/verify-full-chain-client-to-carla.sh

# 5. 启动客户端：选车 carla-sim-001 → 确认进入驾驶 → 连接车端 → 远驾接管
```

车端路径（E2ETESTVIN0000001）与 CARLA 路径（carla-sim-001）可同时运行；若需验证车端+CARLA 两条路径，可再运行 `./scripts/verify-full-chain-with-carla.sh`。

## 方式二：仅仿真（不启动车端容器）

```bash
# 仅启动基础栈（不启动 vehicle），再启动 CARLA + bridge + 客户端
./scripts/start-carla-sim.sh
# 按提示：启动 CARLA → 启动 carla-bridge → 启动客户端 → 选车 carla-sim-001 → 连接车端
```

## 方式三：手动逐步

### 1. 启动基础栈（不启动真实车端）

```bash
cd /path/to/Remote-Driving
docker compose -f docker-compose.yml up -d
# 不加 -f docker-compose.vehicle.dev.yml 则不会启动车端容器
```

等待 Postgres、Keycloak、ZLMediaKit、Backend、Mosquitto 就绪（约 1～2 分钟）。

### 2. 启动 CARLA 服务器（宿主机）

```bash
docker run -d --name carla-server \
  -p 2000-2002:2000-2002 \
  --runtime=nvidia -e NVIDIA_VISIBLE_DEVICES=0 \
  carlasim/carla:latest
```

### 3. 启动 CARLA Bridge（宿主机）

Bridge 需与客户端使用**同一 MQTT** 和**同一 ZLM**。支持 **C++ Bridge**（推荐）或 **Python Bridge**；若在容器内启动，entrypoint 会**优先执行已构建的 C++ 可执行文件**，否则回退到 Python。

#### 3a. 使用 C++ Bridge（推荐）

需先安装依赖并构建（宿主机或容器内）：

```bash
# 依赖：cmake、libpaho-mqtt-dev、paho-mqttpp3（或 Paho MQTT C++）
# Ubuntu/Debian: sudo apt install cmake libpaho-mqtt-dev
cd carla-bridge/cpp
mkdir -p build && cd build
cmake ..   # 不设 CARLA_ROOT 时仅 MQTT + testsrc 推流，无需 LibCarla
make -j4
```

运行（环境变量与 Python 版一致）：

```bash
export CARLA_HOST=127.0.0.1
export MQTT_BROKER=127.0.0.1
export MQTT_PORT=1883
export ZLM_HOST=127.0.0.1
export ZLM_RTMP_PORT=1935
export ZLM_APP=teleop
export VIN=carla-sim-001
./carla_bridge
```

未链接 LibCarla 时，收到 `start_stream` 后 C++ Bridge 会使用 **ffmpeg testsrc** 推四路测试画面到 ZLM；协议（`vehicle/control`、`vehicle/status`）与 Python 版一致。

#### 3b. 使用 Python Bridge

若未构建 C++ Bridge，可使用 Python 版：

```bash
cd carla-bridge
pip install -r requirements.txt
export CARLA_HOST=127.0.0.1
export MQTT_BROKER=127.0.0.1
export MQTT_PORT=1883
export ZLM_HOST=127.0.0.1
export ZLM_RTMP_PORT=1935
python3 carla_bridge.py
```

（若客户端在 Docker 内通过 `mosquitto:1883` / `zlmediakit` 连接，则宿主机上的 Bridge 用 `127.0.0.1` 访问映射出的 1883/1935/80 即可。）

### 4. 启动客户端

- **Docker 内**：`docker compose -f docker-compose.yml run --rm client-dev`（或项目内提供的 client 启动方式）。
- **本机**：若有 Qt 编译环境，直接运行客户端可执行文件。

确保后端、Keycloak、ZLM、MQTT 对客户端可达（Docker 网络或端口映射一致）。

### 5. 客户端操作

| 步骤 | 操作 |
|------|------|
| 1 | 打开客户端，登录（如 e2e-test / 123 或 realm 中配置的账号）。 |
| 2 | 在车辆列表中选择 **carla-sim-001**（仿真车）。 |
| 3 | 点击「确认」创建会话并进入远程驾驶主界面。 |
| 4 | 点击顶栏 **「连接车端」**。 |
| 5 | 等待约 6 秒：先发 MQTT `start_stream`，CARLA Bridge 收到后 spawn 四路相机并推流到 ZLM，客户端再拉流。 |
| 6 | 四宫格应显示 CARLA 四路画面，底盘数据（速度、档位等）来自 Bridge 发布的 `vehicle/status`。 |

如需断开：点击「已连接」可发送 `stop_stream` 并断开视频流；Bridge 会停止推流并销毁相机。

## 故障排查

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 车辆列表没有 carla-sim-001 | 种子数据未加载或账号无权限 | 种子在 Postgres 首次初始化时加载。若 DB 已存在且为旧数据，可在 teleop_db 中执行：`deploy/postgres/03_seed_test_data.sql` 里与 carla-sim-001 相关的两条 INSERT（vehicles + account_vehicles），或删除 postgres 数据卷后重新启动以触发初始化。 |
| 连接不上仿真车辆 / 无会话配置 | 未先创建会话就进入驾驶 | 选车后必须点击「**确认并进入驾驶**」（会先创建会话再进入）；若只点了「确认」未创建会话，lastWhepUrl/mqtt_broker 为空，连接车端会失败。看日志 `[CLIENT][Session]` 是否有「会话创建成功」和 `control.mqtt_broker_url`。 |
| 点击「连接车端」后一直无画面 | 1) Bridge 未收到 start_stream 2) ZLM 未收到推流 3) 客户端拉流地址不对 4) 客户端在宿主机、后端返回的 host 不可达 | 1) 确认 Bridge 与客户端用同一 MQTT Broker，查看 Bridge 日志是否打印「收到 start_stream」「四路相机已 spawn」 2) 检查 ZLM 是否在 1935 收 RTMP、Bridge 的 ZLM_HOST/ZLM_RTMP_PORT 是否正确 3) 会话返回的 WHEP 的 host 对客户端可达（client 在 Docker 内用 zlmediakit；**客户端在宿主机时**需在「连接设置」中把 MQTT 填为 `mqtt://127.0.0.1:1883`、或设置 ZLM_VIDEO_URL=http://127.0.0.1:80） 4) 看客户端日志 `[CLIENT][MQTT]`、`[CLIENT][WebRTC]` 定位到连接或拉流步骤。 |
| 仅部分格子有画面 | 某路 ffmpeg 未启动或推流失败 | 查看 Bridge 日志、宿主机是否安装 ffmpeg；可先跑 `./scripts/verify-carla-cameras.sh --integrate` 验证四路取帧。 |
| 底盘数据不更新 | Bridge 未发布或客户端未订阅 status | Bridge 发布到 `vehicle/status`；客户端订阅 `vehicle/status` 与 `vehicle/<vin>/status`，确认 MQTT 已连接且 currentVin 为 carla-sim-001。 |

## 如何增加仿真车辆

客户端车辆列表来自后端 **GET /api/v1/vins**，数据来自 DB 的 `vehicles` 与 `account_vehicles`。  

**按正常流程增加车辆（含仿真车）的完整步骤**见 → **[增加车辆操作指南](ADD_VEHICLE_GUIDE.md)**。  

此处仅简述仿真车相关要点：

1. **在 DB 中插入车辆与账号绑定**（与 `deploy/postgres/03_seed_test_data.sql` 格式一致）：
   ```sql
   INSERT INTO vehicles (vin, model) VALUES ('carla-sim-003', 'carla-sim') ON CONFLICT (vin) DO NOTHING;
   INSERT INTO account_vehicles (account_id, vin, status) VALUES ('b0000000-0000-0000-0000-000000000001'::uuid, 'carla-sim-003', 'active') ON CONFLICT (account_id, vin) DO NOTHING;
   ```
2. **CARLA Bridge 按 VIN 分流**：当前 Bridge 默认只响应 `VIN=carla-sim-001`（环境变量 `VIN`）。若需多车，可起多个 Bridge 容器，每个设置不同 `VIN`（或后续改 Bridge 支持多 VIN）。
3. 重启客户端或重新进入选车页，即可在列表中看到新车辆。**连接 CARLA 仿真请选 carla-sim-001**（当前 Bridge 默认 `VIN=carla-sim-001`）。若需 carla-sim-002 也走仿真，需单独起一个 Bridge 容器并设置 `VIN=carla-sim-002`。

种子数据已包含 **carla-sim-001**、**carla-sim-002**（e2e-test 账号可见）；选 **carla-sim-001** 后点击「连接车端」→「远驾接管」即可操作 CARLA 仿真车辆。

## 与真实车端切换

- 使用 **carla-sim-001** / **carla-sim-002**：仅启动 CARLA + Bridge，不启动 Vehicle-side 容器。
- 使用 **E2ETESTVIN0000001** 等真实车端：用 `docker-compose.vehicle.dev.yml` 启动 vehicle 服务，不要同时起 CARLA Bridge（否则两方都会响应同一 `vehicle/control`）。

同一 MQTT 下同时只应有一个「车端」在订阅 `vehicle/control` 并推流，避免冲突。

## 逐环节验证（CARLA → 客户端能否接管）

从 CARLA 到客户端依次确认，便于定位「无法接管」或「stream not found」：

```bash
./scripts/verify-carla-to-client.sh
```

脚本会检查：1) CARLA 容器是否运行 → 2) 日志是否已加载地图/车辆/Bridge 连 MQTT → 3) 发送 start_stream 后 Bridge 是否推流 → 4) ZLM 上是否有四路流 → 5) Backend 会话是否可创建 → 6) 客户端操作提示。若某步失败，按输出提示排查。

**若 Bridge 在容器内启动失败**（如 `ImportError: libjpeg.so.8` 或 `carla-0.9.13-py2.7` 与 python3 不兼容）：

- **方案 A**：在宿主机运行 Bridge（与 CARLA 容器同机）。CARLA 容器端口 2000 已映射到宿主机，MQTT/ZLM 使用宿主机 127.0.0.1 与 compose 映射端口：
  ```bash
  cd carla-bridge
  pip install -r requirements.txt
  export CARLA_HOST=127.0.0.1 CARLA_PORT=2000 MQTT_BROKER=127.0.0.1 MQTT_PORT=1883 ZLM_HOST=127.0.0.1 ZLM_RTMP_PORT=1935 VIN=carla-sim-001
  python3 carla_bridge.py
  ```
- **方案 B**：使用带 Python 3 CARLA 的镜像或自建镜像，并在 entrypoint 中安装缺失依赖（如 libjpeg）。

**客户端「stream not found」**：多为 Bridge 未向 ZLM 推流。确认 Bridge 已收到 start_stream（日志中 `收到 start_stream`、`四路相机已 spawn`），且 ZLM 与 Bridge 网络可达（Compose 下 ZLM_HOST=zlmediakit）。

## 排查用日志关键字

| 环节 | 日志关键字（便于 grep） |
|------|-------------------------|
| 客户端 | `[CLIENT][Session]`、`[CLIENT][选车]`、`[CLIENT][连接车端]`、`[CLIENT][MQTT]`、`[CLIENT][WebRTC]`、`[REMOTE_CONTROL]` |
| 后端 | `[Backend][GET /api/v1/vins]`、`[Backend][POST /api/v1/vins/.../sessions]`、`[Backend][POST sessions]` |
| CARLA Bridge | `[CARLA]`、`[MQTT]`、`[Control]`、`start_stream`、`vin_ok` |
| 车端 | `start_stream`、`VEHICLE_VIN`、`已订阅`、`本车响应` |
