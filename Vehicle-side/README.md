# Vehicle-side 车辆端控制器

车辆端控制器，运行在 Jetson Orin 上，接收 MQTT 控制指令并控制车辆。

## 功能特性

- ✅ MQTT 控制指令接收
- ✅ 车辆控制（方向盘、油门、刹车、档位）
- ✅ ROS2 桥接支持（可选）
- ✅ 紧急停止功能
- ✅ VIN 验证

## 项目结构

```
Vehicle-side/
├── CMakeLists.txt          # CMake 构建配置
├── build.sh                # 一键编译脚本
├── run.sh                  # 一键运行脚本
├── debug.sh                # 调试脚本
├── README.md               # 本文档
└── src/                    # C++ 源代码
    ├── main.cpp            # 程序入口
    ├── vehicle_controller.h/cpp  # 车辆控制器
    ├── mqtt_handler.h/cpp        # MQTT 处理器
    └── ros2_bridge.h/cpp         # ROS2 桥接（可选）
```

## 依赖要求

### 必需依赖

- **CMake 3.16+**
- **C++17 编译器** (GCC 7+, Clang 5+)
- **Paho MQTT C++** - MQTT 客户端库

### 可选依赖

- **ROS2** (Humble/Foxy) - ROS2 桥接支持

## 编译说明

### 一键编译

```bash
cd Vehicle-side
./build.sh
```

### 手动编译

```bash
cd Vehicle-side
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 启用 ROS2 支持

如果系统安装了 ROS2，脚本会自动检测并启用：

```bash
# ROS2 Humble
source /opt/ros/humble/setup.bash
./build.sh

# ROS2 Foxy
source /opt/ros/foxy/setup.bash
./build.sh
```

## 运行说明

### 一键运行

```bash
cd Vehicle-side
./run.sh [mqtt_broker_url]

# 示例
./run.sh mqtt://192.168.1.100:1883
```

### 手动运行

```bash
cd Vehicle-side/build
./VehicleSide mqtt://192.168.1.100:1883
```

### 环境变量

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `MQTT_BROKER_URL` | MQTT Broker 地址 | `mqtt://mosquitto:1883` |
| `ZLM_RTMP_URL` | RTMP 推流地址 | `rtmp://zlmediakit:1935` |
| `ZLM_WHIP_URL` | WHIP 推流地址 | `http://zlmediakit/index/api/whip` |
| `VIN` | 车辆标识 | `TEST_VEHICLE_001` |
| `VEHICLE_CONTROL_SECRET` | 控制密钥 | `change_me_in_production` |
| `LOG_LEVEL` | 日志级别 | `info` |
| `WATCHDOG_TIMEOUT` | 看门狗超时（秒） | `5` |
| `SAFE_STOP_ENABLED` | 是否启用安全停车 | `true` |
| `DATABASE_URL` | PostgreSQL 连接（健康检查） | `postgresql://postgres:postgres@postgres:5432/teleop` |

### 调试说明

### GDB 调试

```bash
cd Vehicle-side
./debug.sh [mqtt_broker_url]
```

### 调试技巧

```bash
# 启动 GDB
gdb ./VehicleSide

# 设置断点
(gdb) break VehicleController::processCommand
(gdb) break MqttHandler::processControlCommand

# 运行
(gdb) run mqtt://192.168.1.100:1883

# 查看变量
(gdb) print cmd.steering
```

## MQTT 控制指令格式

接收的控制指令格式（JSON）：

```json
{
  "vin": "LSGBF53M8DS123456",
  "type": "drive",
  "steering": 0.5,
  "throttle": 0.3,
  "brake": 0.0,
  "gear": 1,
  "timestamp": 1234567890
}
```

**参数说明**:
- `vin`: 车辆 VIN 编号（可选，用于验证）
- `steering`: 方向盘角度 (-1.0 到 1.0)
- `throttle`: 油门 (0.0 到 1.0)
- `brake`: 刹车 (0.0 到 1.0)
- `gear`: 档位 (-1: 倒档, 0: 空档, 1: 前进)

## MQTT 主题

- **控制指令**: `vehicle/control` 或 `vehicle/<VIN>/control`
- **状态发布**: `vehicle/status` 或 `vehicle/<VIN>/status`

## 推流触发与「边推流边读数据集」

车端**不在启动时自动推流**。仅在收到 MQTT `vehicle/control` 下 `type: "start_stream"` 时（由**客户端点击「连接车端」**触发），才执行 `VEHICLE_PUSH_SCRIPT` 指定的脚本。

- **默认**：`push-testpattern-to-zlm.sh`，向 ZLM 推四路测试图案（无需数据集）。
- **边读数据集边推流**：将 `VEHICLE_PUSH_SCRIPT` 设为 `push-nuscenes-cameras-to-zlm.sh`，并挂载 nuscenes sweeps 目录、设置 `SWEEPS_PATH`。脚本会从数据集目录循环读图并推流。  
  示例（`docker-compose.vehicle.dev.yml` 或 env 覆盖）：
  - `VEHICLE_PUSH_SCRIPT=/app/scripts/push-nuscenes-cameras-to-zlm.sh`
  - `SWEEPS_PATH=/data/sweeps`
  - 挂载卷：`/path/to/nuscenes-mini/sweeps:/data/sweeps:ro`

**验证数据集可读**（与推流脚本结构一致）：**推荐先在 Vehicle-side 运行的容器中进行本地校验**（与推流时挂载、路径一致）：
- **本地校验（容器内）**：先启动 vehicle 并挂载数据集卷（如 `- /path/to/nuscenes-mini/sweeps:/data/sweeps:ro`），再执行  
  `make verify-vehicle-dataset-local`  
  或 `SWEEPS_PATH=/data/sweeps make verify-vehicle-dataset-local`
- 宿主机校验：`SWEEPS_PATH=/path/to/nuscenes-mini/sweeps make verify-vehicle-dataset`

## ROS2 桥接（可选）

如果启用 ROS2 支持，控制器会订阅 ROS2 话题：

- **话题**: `vehicle/cmd_vel` (geometry_msgs/Twist)
- **转换**: ROS2 Twist → 车辆控制指令

## 车辆控制接口

`VehicleController` 类提供了以下接口，需要根据实际硬件实现：

- `applySteering(double angle)` - 控制方向盘
- `applyThrottle(double value)` - 控制油门
- `applyBrake(double value)` - 控制刹车
- `applyGear(int gear)` - 控制档位

**实现示例**（CAN 总线）:
```cpp
void VehicleController::applySteering(double angle) {
    // 发送 CAN 消息控制方向盘
    can_send(STEERING_ID, angle_to_can_value(angle));
}
```

## 配置说明

### 配置文件位置

Vehicle-side 使用 YAML 配置文件，支持环境变量覆盖。

**主配置文件**:
- **新位置**（推荐）: `Vehicle-side/config/vehicle_config.yaml`
- **旧位置**（兼容）: `config/vehicle_config.yaml`（符号链接，指向新位置）
- **容器内路径**: `/app/config/vehicle_config.yaml`

### MQTT Broker 配置

通过命令行参数指定：
```bash
./VehicleSide mqtt://192.168.1.100:1883
```

### 环境变量

- `BUILD_TYPE`: 构建类型（Debug/Release，默认 Release）

## 故障排查

### MQTT 连接失败

1. 检查 Broker 地址和端口
2. 检查网络连接
3. 检查防火墙设置
4. 查看日志输出

### 控制指令无效

1. 检查 JSON 格式是否正确
2. 检查值范围是否合法
3. 查看控制日志输出
4. 检查车辆控制接口实现

### ROS2 桥接不工作

1. 确认 ROS2 已安装并 source
2. 检查话题名称是否正确
3. 查看 ROS2 日志

## 安全考虑

- ✅ 控制指令值范围限制
- ✅ 紧急停止功能
- ⚠️ 建议：添加 VIN 验证
- ⚠️ 建议：添加指令签名验证
- ⚠️ 建议：添加超时保护

## 后续开发

- [ ] 实现实际车辆控制接口（CAN/串口/ROS2）
- [ ] 添加车辆状态上报
- [ ] 添加安全保护机制
- [ ] 添加日志记录
- [ ] 添加配置文件支持
