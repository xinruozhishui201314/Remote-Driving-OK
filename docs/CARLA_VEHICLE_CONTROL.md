# CARLA 仿真车辆控制说明

## 1. 控制链路

```
客户端 (方向盘/踏板/档位) → MQTT vehicle/control → CARLA Bridge → vehicle.apply_control() → CARLA 仿真车
```

## 2. 支持的消息类型

| type | 说明 | 关键字段 |
|------|------|----------|
| `remote_control` | 远驾接管开关 | `enable`: true/false |
| `drive` | 驾驶控制 | `steering`, `throttle`, `brake`, `gear` |
| `gear` | 单独档位 | `value` 或 `gear` |
| `emergency_stop` | 急停 | `enable`: true/false |

## 3. 档位映射（与客户端 DrivingInterface 一致）

| 数值 | 含义 | CARLA 映射 |
|------|------|------------|
| -1 | R 倒档 | `reverse=True`, `hand_brake=False` |
| 0 | N 空档 | `reverse=False`, `hand_brake=False` |
| 1 | D 前进 | `reverse=False`, `hand_brake=False` |
| 2 | P 驻车 | `reverse=False`, `hand_brake=True` |

## 4. 控制逻辑

- **远驾未启用**：`throttle=0`, `brake=0.5`（安全制动），不执行用户控制
- **急停激活**：`throttle=0`, `brake=1`, `hand_brake=True`
- **远驾启用**：按 MQTT 收到的 steering/throttle/brake/gear 应用

## 5. 环境变量

| 变量 | 默认 | 说明 |
|------|------|------|
| `CONTROL_HZ` | 50 | 控制应用频率（Hz） |
| `CONTROL_DEBUG` | 0 | 1=每帧输出 apply_control 参数 |

## 6. 日志前缀（便于 grep 精准定位）

| 前缀/关键字 | 说明 |
|-------------|------|
| `[Control] 收到 drive` | 收到 drive 消息及参数 |
| `[Control] 收到 emergency_stop` | 收到急停 |
| `[Control] 收到 gear` | 收到档位 |
| `[Control] 应用 #` | apply_control 执行（前 5 次 + 每 10s） |
| `[Control] 急停激活` | 急停生效 |
| `[Control] 发布 vehicle_status` | 发布状态（每 5s 或 CONTROL_DEBUG） |

## 7. 验证

```bash
# 完整控制链路验证（remote_control + drive + apply_control + status）
./scripts/verify-client-control-carla.sh

# 开启详细控制日志
CONTROL_DEBUG=1 RESTART_CARLA=1 bash scripts/verify-carla-ui-only.sh
```

## 8. 排查

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 车不动 | 1) 未发 remote_control enable=true 2) 未发 drive | 客户端先点「远驾接管」再操作 |
| 档位无反应 | gear 未在 drive 中或 type=gear 未处理 | 检查 MQTT payload 含 gear/value |
| 急停无效 | emergency_stop 未处理 | 检查 Bridge 日志是否有「收到 emergency_stop」 |
| 速度始终 0 | vehicle.get_velocity() 异常 | 检查 CARLA 连接与 vehicle 对象 |
