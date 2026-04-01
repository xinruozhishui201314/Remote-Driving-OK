# CARLA 日志深度分析

## 根因：镜像使用旧版 entrypoint，强制无头模式

### 关键证据（进程列表）

```
su -s /bin/bash carla -c ... bash CarlaUE4.sh /Game/Maps/Town01 -RenderOffScreen -nosound
CarlaUE4-Linux-Shipping CarlaUE4 /Game/Maps/Town01 -RenderOffScreen -nosound
```

**结论**：即使设置了 `CARLA_SHOW_WINDOW=1`、`DISPLAY=:1`，实际启动仍带 `-RenderOffScreen`，导致无仿真窗口。

### 原因链

| 层级 | 说明 |
|------|------|
| 1 | Dockerfile 将 entrypoint 打包进镜像：`COPY entrypoint.sh /entrypoint.sh` + `ENTRYPOINT ["/entrypoint.sh"]` |
| 2 | Compose 挂载 `./deploy/carla/entrypoint.sh:/workspace/entrypoint.sh`，但**未覆盖**镜像的 `/entrypoint.sh` |
| 3 | 容器实际执行的是镜像内的 `/entrypoint.sh`（旧版），不是挂载的 `/workspace/entrypoint.sh` |
| 4 | 旧版 entrypoint 不识别 `CARLA_SHOW_WINDOW` 或始终使用 `-RenderOffScreen` |

### Bridge 未启动

日志停在「额外等待 45 秒让 CARLA RPC 完全就绪...」，未见 `USE_PYTHON_BRIDGE`、`启动 Python Bridge`。可能原因：

- 旧版 entrypoint 流程不同，可能缺少 Bridge 启动逻辑
- 或 60s 时仍在 pip 安装阶段，Bridge 尚未启动

---

## 彻底解决方案

### 方案 A：Compose 覆盖 entrypoint（推荐，无需重建镜像）

在 `docker-compose.carla.yml` 中显式使用挂载的 entrypoint：

```yaml
carla:
  entrypoint: ["/workspace/entrypoint.sh"]
  # ... 其余配置不变
```

这样容器会执行仓库中的 `deploy/carla/entrypoint.sh`，正确响应 `CARLA_SHOW_WINDOW=1`。

### 方案 B：重建镜像

```bash
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml build carla --no-cache
```

将最新 entrypoint 打包进镜像，适用于需要固化镜像的场景。
