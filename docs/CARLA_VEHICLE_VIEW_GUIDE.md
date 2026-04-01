# CARLA 仿真车辆与视角配置

## 1. 切换到车辆主视角

CARLA 仿真窗口默认使用**驾驶位主视角**（第一人称：镜头在驾驶位眼高，与车辆同向）。

- **主视角（默认）**：`SPECTATOR_VIEW_MODE=driver` — 仿真窗口显示驾驶位视野
- **第三人称**：`SPECTATOR_VIEW_MODE=third_person` — 镜头在车辆后方 8m、上方 4m
- **关闭跟随**：`SPECTATOR_FOLLOW_VEHICLE=0` — 可手动拖动仿真窗口视角

**客户端四路画面**：`CAMERA_DRIVER_VIEW=1`（默认）时，前摄像头为驾驶位主视角；设为 0 则为车头视角。

## 2. 仿真场景中的车辆配置

Bridge 启动时会**自动 spawn 一辆车**，无需手动配置。可配置项如下。

### 2.1 车辆类型（CARLA_VEHICLE_BP）

| 环境变量 | 默认 | 说明 |
|----------|------|------|
| `CARLA_VEHICLE_BP` | `vehicle.*` | 车辆蓝图过滤；取匹配列表的第一辆 |

**示例**：

```bash
# 使用 Tesla Model 3
CARLA_VEHICLE_BP=vehicle.tesla.model3

# 使用默认（第一辆可用车辆）
CARLA_VEHICLE_BP=vehicle.*
```

**常见车辆**：`vehicle.tesla.model3`、`vehicle.audi.tt`、`vehicle.bmw.grandtourer`、`vehicle.carlamotors.carlacola` 等。可在 CARLA 中查看完整列表。

### 2.2 出生点（CARLA_SPAWN_INDEX）

| 环境变量 | 默认 | 说明 |
|----------|------|------|
| `CARLA_SPAWN_INDEX` | `0` | 地图 spawn 点索引（0 为第一个） |

Town01 有多个 spawn 点，可更换起始位置：

```bash
CARLA_SPAWN_INDEX=5   # 使用第 6 个 spawn 点
```

### 2.3 启动时指定

```bash
CARLA_VEHICLE_BP=vehicle.tesla.model3 CARLA_SPAWN_INDEX=3 RESTART_CARLA=1 bash scripts/verify-carla-ui-only.sh
```

或修改 `docker-compose.carla.yml` 中对应环境变量后重启 CARLA。

## 3. 四路相机

Bridge 收到 `start_stream` 后会自动在车辆上 spawn 四路相机（前/后/左/右），并推流到 ZLM。相机位置在 `carla_bridge.py` 的 `CAMERA_CONFIGS` 中定义，无需额外配置。

## 4. 流程概览

```
Bridge 启动 → 连接 CARLA → spawn 车辆（CARLA_VEHICLE_BP + CARLA_SPAWN_INDEX）
         → 订阅 MQTT vehicle/control
         → 主循环：发布 vehicle/status + 更新 spectator 跟随车辆

客户端点击「连接车端」→ 发送 start_stream → Bridge spawn 四路相机 → 推流到 ZLM
```
