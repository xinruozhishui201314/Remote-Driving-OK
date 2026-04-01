# 运行环境要求与一次性设置（固化）

本文档汇总**宿主机与容器**运行本远驾系统所需的环境要求及**一次性/首次设置**，按此执行可避免「客户端无界面」「libGL 报错」「端口冲突」等问题再次出现。

---

## 1. 宿主机一次性设置（客户端有界面必做）

在**首次**使用或**客户端窗口无法弹出**时，在宿主机执行以下步骤。

### 1.1 允许 Docker 容器连接 X11（必须）

容器内 Qt 客户端需要把窗口显示到宿主机桌面，宿主机必须允许本地 Docker 访问 X11：

```bash
xhost +local:docker
```

- **生效范围**：当前登录会话；重启或重新登录后需再次执行。
- **可选持久化**：若希望每次登录自动执行，可写入 `~/.profile` 或 `~/.bashrc`：
  ```bash
  # 允许 Docker 容器显示 GUI（远驾客户端）
  [ -n "$DISPLAY" ] && xhost +local:docker 2>/dev/null
  ```

### 1.2 设置 DISPLAY（通常已自动设置）

- 有图形桌面的 Linux 通常已设置 `DISPLAY=:0`。
- 若未设置或客户端报错 “cannot open display”，在运行启动脚本或客户端前执行：
  ```bash
  export DISPLAY=:0
  ```

### 1.3 一键执行上述设置（推荐）

项目提供脚本，**首次或客户端无界面时**执行一次即可：

```bash
bash scripts/setup-host-for-client.sh
```

该脚本会：执行 `xhost +local:docker`、在未设置时导出 `DISPLAY=:0`，并打印简短说明。`start-all-nodes-and-verify.sh` 在启动客户端前也会自动调用此脚本，并**等待编译/启动完成**（最多 240s，每 5s 检查）再继续——编译在容器内并行进行（make -j4），**必须编译完成且客户端进程起来后才算成功**。阶段 2 的节点检查已并行执行以加快验证。

---

## 2. 客户端容器内环境（脚本已固化）

以下由 **start-all-nodes-and-verify.sh** / **run-client-ui.sh** 等脚本自动传入，一般无需手改。

| 环境变量 | 典型值 | 说明 |
|----------|--------|------|
| `DISPLAY` | 与宿主机一致 | 容器内使用**与宿主机相同的 DISPLAY**（如宿主机 `:1` 则传 `:1`），否则会报 “could not connect to display”。可覆盖：**CLIENT_DISPLAY=:0** 等。 |
| `LIBGL_ALWAYS_SOFTWARE` | `1` | 容器内无宿主机 NVIDIA 驱动时使用软件渲染，避免 `libGL failed to load driver: nvidia-drm` 导致无法启动。 |
| `ZLM_VIDEO_URL` | `http://zlmediakit:80` | 拉流 base URL（容器内）。 |
| `MQTT_BROKER_URL` | `mqtt://mosquitto:1883` | MQTT 地址（容器内）。 |
| `CLIENT_LOG_FILE` | `/tmp/remote-driving-client.log` | 客户端日志路径（容器内），便于闪退后排查。 |

### 2.1 CARLA 仿真窗口（默认显示）

运行 `bash scripts/start-all-nodes-and-verify.sh` 时，CARLA 仿真窗口默认会弹出。脚本会在启动节点前自动执行 `xhost +local:docker`。

若窗口未弹出，确认宿主机有图形环境、已执行 `xhost +local:docker`，且 `DISPLAY` 与当前终端一致（`echo $DISPLAY` 多为 `:0` 或 `:1`）。

无头模式（不显示 CARLA 窗口）：`CARLA_SHOW_WINDOW=0 bash scripts/start-all-nodes-and-verify.sh`

---

若**手动**在容器内启动客户端，建议带上与脚本一致的参数，例如：

```bash
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec -it \
  -e DISPLAY=:0 -e LIBGL_ALWAYS_SOFTWARE=1 \
  -e ZLM_VIDEO_URL=http://zlmediakit:80 -e MQTT_BROKER_URL=mqtt://mosquitto:1883 \
  client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login'
```

宿主机需已执行 `xhost +local:docker`（或已运行 `scripts/setup-host-for-client.sh`）。

---

## 3. 启动前关闭已有节点（已固化到脚本）

为避免端口占用与旧进程干扰，**start-all-nodes-and-verify.sh** 已内置「阶段 0」：

- 检测当前 Compose 服务及 CARLA 容器是否在运行；
- 若有则先执行 `docker compose ... down` 及停止 CARLA，等待约 5 秒后再启动。

无需再手动 `docker compose down`，直接执行脚本即可。

---

## 4. CARLA / GPU（可选）

- **CARLA 仿真**：默认需宿主机有 NVIDIA 显卡并安装 **nvidia-container-toolkit**，否则 CARLA 容器可能无法启动。安装方法见项目内：
  ```bash
  sudo ./scripts/install-nvidia-container-toolkit.sh
  ```
- **客户端**：当前默认使用软件渲染（`LIBGL_ALWAYS_SOFTWARE=1`），不依赖宿主机 GPU；若希望客户端使用 GPU 加速，需在 compose 中为 client-dev 配置 nvidia 设备并去掉该环境变量（参见 docs/ADD_VEHICLE_GUIDE.md 等）。

---

## 5. 检查清单（首次或出问题时）

| 检查项 | 命令/操作 |
|--------|------------|
| X11 允许 Docker | `xhost +local:docker` 或 `bash scripts/setup-host-for-client.sh` |
| DISPLAY 已设置 | `echo $DISPLAY` 应输出 `:0` 或 `:1` 等；容器会使用相同值，窗口未弹出时可尝试 `xhost +local:docker` 或与当前终端一致地设置 DISPLAY 后重跑 |
| 客户端日志 | 容器内：`tail -50 /tmp/remote-driving-client.log`；或宿主机：`docker compose ... exec client-dev tail -50 /tmp/remote-driving-client.log` |
| Qt xcb / libxcb-cursor0 报错 | 需重建 client-dev 镜像以安装依赖：`bash scripts/build-client-dev-full-image.sh`（见 `client/Dockerfile.client-dev`） |
| client-dev 镜像 | **remote-driving-client-dev:full 已具备运行条件**（Qt6、libdatachannel、FFmpeg、xcb 等），有该镜像即可直接启动；启动脚本使用 --no-build，首次或更新需先运行 `bash scripts/build-client-dev-full-image.sh` |
| 端口未被占用 | 再次运行前由 start-all-nodes-and-verify.sh 阶段 0 自动 down 已有节点 |

---

## 6. 相关文档与脚本

- **宿主机一次性设置脚本**：`scripts/setup-host-for-client.sh`
- **一键启动并验证（含客户端启动与诊断）**：`scripts/start-all-nodes-and-verify.sh`
- **客户端无界面 / libGL 等排查**：`docs/ADD_VEHICLE_GUIDE.md` 末尾「客户端无法启动 / libGL」、`docs/CLIENT_UI_VERIFICATION_GUIDE.md`
- **CARLA 与客户端拉流**：`docs/CARLA_CLIENT_STREAM_GUIDE.md`
