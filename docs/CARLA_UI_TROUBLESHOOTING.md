# CARLA 仿真窗口不显示 — 排障指南

## 快速验证命令

```bash
# 仅启动 CARLA 并验证窗口（不启动 Backend/Client）
bash scripts/verify-carla-ui-only.sh

# 强制重启后验证
RESTART_CARLA=1 bash scripts/verify-carla-ui-only.sh
```

## 常见原因与处理

### 1. Wayland 下 DISPLAY 错误（最常见）

**现象**：配置检查全通过，但窗口不显示。

**原因**：Wayland 下 XWayland 通常使用 `:0`，而终端可能显示 `DISPLAY=:1`。

**处理**：显式使用 `:0` 启动：

```bash
DISPLAY=:0 RESTART_CARLA=1 bash scripts/verify-carla-ui-only.sh
```

### 2. X11 权限不足

**现象**：`cannot open display` 或 `could not connect to display`。

**处理**：在宿主机执行：

```bash
xhost +local:docker
xhost +local:
xhost +SI:localuser:root
```

### 3. 验证 X11 是否正常

若 xeyes 能显示，说明 Docker → X11 转发正常，问题在 CARLA/Unreal：

```bash
docker run --rm -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix x11-apps xeyes
```

若无 `x11-apps` 镜像，可先拉取：`docker pull x11-apps`

### 4. 等待时间不足

CARLA 启动需约 60–90 秒（apt、Python、CarlaUE4 初始化）。若 10s 内 `docker logs` 为空属正常。

**处理**：等待 2 分钟后查看日志：

```bash
docker logs carla-server 2>&1 | tail -100
```

### 5. 检查会话类型

```bash
echo $XDG_SESSION_TYPE   # 若为 wayland，优先尝试 DISPLAY=:0
```

### 6. 确认在图形桌面终端运行

不要在 SSH 会话或纯 TTY 中运行；需在图形桌面的终端（GNOME Terminal、Konsole 等）中执行。

---

## 诊断清单

| 步骤 | 命令 | 预期 |
|-----|------|------|
| 1 | `echo $DISPLAY` | `:0` 或 `:1` |
| 2 | `ls -la /tmp/.X11-unix/` | 存在 `X0` 或 `X1` |
| 3 | `xhost` | 含 `local:docker` 或 `local:` |
| 4 | `docker run --rm -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix x11-apps xeyes` | xeyes 窗口弹出 |
| 5 | `DISPLAY=:0 RESTART_CARLA=1 bash scripts/verify-carla-ui-only.sh` | CARLA 窗口弹出 |
