# CARLA 仿真窗口视角切换排障

## 日志阶段与卡点诊断

Bridge 启动流程（按日志顺序）：

| 阶段 | 日志关键字 | 说明 |
|------|-------------|------|
| 1 | `[entrypoint] ========== 入口脚本启动` | entrypoint 开始 |
| 2 | `[entrypoint] 启动 CARLA（显示仿真窗口` | CarlaUE4 启动 |
| 3 | `[entrypoint] CARLA 端口已就绪` | CARLA RPC 可用 |
| 4 | `[entrypoint] USE_PYTHON_BRIDGE=1` | 准备 Python Bridge |
| 5 | `[entrypoint] 阶段: 即将 exec Python Bridge` | 即将启动 Bridge |
| 6 | `[carla-bridge] ========== Python Bridge 启动入口` | Bridge 脚本已 exec |
| 7 | `[Bridge] 阶段: 连接 CARLA` | 开始连接 CARLA |
| 8 | `[Bridge] 阶段: CARLA 连接成功` | 连接成功 |
| 9 | `[Bridge] 阶段: spawn 车辆` | 开始 spawn |
| 10 | `[Spectator] SPECTATOR_FOLLOW_VEHICLE=` | Spectator 配置 |
| 11 | `[Spectator] 更新 #1` | Spectator 首次更新 |

**若无 [carla-bridge] 或 [Bridge]**：entrypoint 卡在 pip 安装或未 exec 到 Bridge。

## CARLA 0.9.13 API 参考

- `world.get_spectator()` → carla.Actor（仿真窗口镜头，ID 通常为 0）
- `spectator.set_transform(transform)` → 设置镜头位置与朝向
- 详见：https://carla.readthedocs.io/en/0.9.13/python_api/#carla.World

## 增强日志

### 1. 启动时日志

Bridge 启动时会输出：

```
[Spectator] SPECTATOR_FOLLOW_VEHICLE=True SPECTATOR_VIEW_MODE=driver CAMERA_DRIVER_VIEW=True SPECTATOR_DEBUG=False
[Spectator] 仿真窗口将使用: 驾驶位主视角；前摄像头: 驾驶位主视角
```

### 2. 运行时日志（前 3 次 + 每 10s）

```
[Spectator] 更新 #1 mode=driver 车辆(x,y,z) yaw=... 镜头(x,y,z)
```

### 3. 开启详细日志

```bash
SPECTATOR_DEBUG=1 RESTART_CARLA=1 bash scripts/verify-carla-ui-only.sh
```

或修改 `docker-compose.carla.yml`：`SPECTATOR_DEBUG: "1"`

## 诊断清单

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 日志无 `[Spectator]` | Bridge 未启动或未进入主循环 | 查 `docker logs carla-server` 是否有 `USE_PYTHON_BRIDGE`、`启动 Python Bridge` |
| `world.get_spectator() 返回 None` | CARLA 无头模式（-RenderOffScreen） | 确认 `CARLA_SHOW_WINDOW=1`，重建/重启 |
| `spectator follow failed` | 异常（见 Traceback） | 设 `SPECTATOR_DEBUG=1` 查看完整堆栈 |
| 有更新日志但窗口视角不变 | 1) 仿真窗口非 spectator 2) CARLA 版本差异 | 尝试 `SPECTATOR_VIEW_MODE=third_person` 对比 |
| SPECTATOR_FOLLOW_VEHICLE=False | 环境变量未传入 | 检查 compose 中 `SPECTATOR_FOLLOW_VEHICLE: "1"` |

## 查看 Bridge 日志

```bash
docker logs carla-server 2>&1 | grep -E "\[Spectator\]|Spectator"
```

## 自动化验证

Spectator 功能可通过脚本自动验证：

```bash
# 单独验证 Spectator（需 CARLA 已运行）
bash scripts/verify-carla-spectator.sh

# 完整 CARLA 界面 + Spectator 验证（含启动、窗口、Spectator）
bash scripts/verify-carla-ui-only.sh
```

验证项：Bridge 启动、Spectator 配置/更新日志、`SPECTATOR_VIEW_MODE=driver`。详见 `scripts/verify-carla-spectator.sh`。
