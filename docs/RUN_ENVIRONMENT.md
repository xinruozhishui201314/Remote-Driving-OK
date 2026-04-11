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
| `LIBGL_ALWAYS_SOFTWARE` | （由客户端自动设置） | **勿在脚本中硬编码。** 客户端在 `QGuiApplication` 之前（`client_display_runtime_policy.cpp`）自动选择：**若 `nvidia-smi -L` 列出 GPU 或存在 `/dev/nvidia0`，必须使用硬件 GL 栈**（`unset` + `QT_XCB_GL_INTEGRATION=xcb_egl`），并打印 `glxinfo -B` 供对照；**无 NVIDIA 时**按 **`glxinfo -B`**：软光栅 → 软件栈；硬件渲染器串 → 硬件栈；**glxinfo 无法判定时若存在 `/dev/dri/renderD*`**（compose 常挂载）→ 硬件栈；否则保守软件栈。 |
| `QT_XCB_GL_INTEGRATION` | （由客户端自动设置） | 与 `LIBGL_ALWAYS_SOFTWARE` 同上，由自动策略写入；一般无需手传。 |
| `CLIENT_ASSUME_HARDWARE_GL` | `1` | 跳过探测，强制硬件栈（`unset LIBGL_ALWAYS_SOFTWARE`，`xcb_egl`）。 |
| `CLIENT_ASSUME_SOFTWARE_GL` | `1` | 跳过探测，强制软件栈。 |
| `CLIENT_SKIP_AUTO_GL_STACK` | `1` | 完全不修改 GL 相关环境（高级 / 调试）。 |
| `CLIENT_SKIP_GLXINFO_PROBE` | `1` | 不跑 glxinfo；有 NVIDIA 或 `/dev/dri/renderD*` 则仍倾向硬件栈；否则保守软件栈。 |
| `CLIENT_FORCE_XCB_EGL` | `1` | 不修改 GL 环境（调试用；软件栈下可能断连）。 |
| `CLIENT_REQUIRE_HARDWARE_PRESENTATION` | `1` | 启动后若 `LIBGL_ALWAYS_SOFTWARE` 或 `GL_RENDERER` 为软件光栅，**拒绝启动**（非 0 退出）。 |
| `CLIENT_TELOP_STATION` | `1` | 与 `CLIENT_REQUIRE_HARDWARE_PRESENTATION=1` 等效（别名）。 |
| （默认） | — | **Linux + 有 `DISPLAY`/`WAYLAND` + `QT_QPA_PLATFORM` 为空或 `xcb`**：进程内**默认**启用与上相同的硬件呈现门禁（远控台默认行为）。 |
| `CLIENT_GPU_PRESENTATION_OPTIONAL` | `1` | **关闭**上述默认门禁（无独显/CI/强制软件栈）；`scripts/start-full-chain.sh` 在 `TELEOP_GPU_OPTIONAL=1` 或 `TELEOP_REQUIRE_HW_GL=0` 时会传入。 |
| `CLIENT_ALLOW_SOFTWARE_PRESENTATION` | `1` | 显式允许软光栅，**跳过**硬件门禁（调试）。 |
| `CLIENT_GL_SWAP_INTERVAL` | `1` | 默认在 `QGuiApplication` 前设置 `QSurfaceFormat::defaultFormat().swapInterval`（默认可为 1，向驱动申请 VSync）；`0` 关闭请求。 |
| `CLIENT_GL_DEFAULT_FORMAT_SKIP` | `1` | 不修改默认 `QSurfaceFormat`（调试用）。 |
| `CLIENT_VIDEO_RUNTIME_SLO` | `0` | 关闭运行期 QueuedLag 持续超阈 SLO（默认开启：连续 5s maxQueuedLag≥80ms 打 CRITICAL 并写指标）。 |
| `TELEOP_REQUIRE_HW_GL` | `1` | **`scripts/start-full-chain.sh`（默认）**：传入 `CLIENT_TELOP_STATION=1`（显式）；Linux+xcb 下即使不传，进程内也已默认硬件门禁。`TELEOP_GPU_OPTIONAL=1` 时脚本传入 `CLIENT_GPU_PRESENTATION_OPTIONAL=1`。 |
| `ZLM_VIDEO_URL` | `http://zlmediakit:80` | 拉流 base URL（容器内）。 |
| `MQTT_BROKER_URL` | `mqtt://mosquitto:1883` | MQTT 地址（容器内）。 |
| `CLIENT_LOG_FILE` | `/tmp/remote-driving-client.log` | 客户端日志路径（容器内），便于闪退后排查。 |
| `CLIENT_VIDEO_SAVE_FRAME` | `png` / `raw` / `both` | **条状/花屏首选定界（优先于只猜 stride）**：解码后、Scene Graph **前**落盘 RGBA；文件名 `<stream>_f<frameId>.png`，**frameId 与解码输出一致**。与日志中 **同一 stream、同一 frameId（或 Evidence 行的 `fid=`）** 对齐 `[H264][FrameDump]` → `DECODE_OUT` / `[Client][VideoEvidence]` → `RS_APPLY` → `SG_UPLOAD`。PNG 已花→解码/sws/色彩/隔行上游；PNG 正常屏花→纹理/RHI/合成。 |
| `CLIENT_VIDEO_SAVE_FRAME_DIR` | 目录 | 默认：系统临时目录下 `remote-driving-frame-diag`。 |
| `CLIENT_VIDEO_SAVE_FRAME_MAX` | 默认 `3` | 每路最多落盘张数（上限 1000，防打满磁盘）。 |
| `CLIENT_VIDEO_EVIDENCE_CHAIN` | `1` | 开启端到端像素证据链日志 `[Client][VideoEvidence]`（DECODE_OUT→PRESENT_TX→MAIN_PRESENT→RS_APPLY→SG_UPLOAD）；条状/花屏时按 **同 stream + 同 fid** 对比 `rowHash`/`bpl`，找**第一个不一致的 stage**。 |
| `CLIENT_VIDEO_EVIDENCE_EARLY_MAX` | `10` | 证据链：前 N 帧每帧记录（默认 10）。 |
| `CLIENT_VIDEO_EVIDENCE_INTERVAL` | `60` | 证据链：之后每 N 帧记录一条；`0` 表示仅打前 `EARLY_MAX` 帧。 |
| `CLIENT_VIDEO_EVIDENCE_STRIPE_ROWS` | `1` | `1`（默认）对 rowHash 采样首行+次行+中间行+末行，更易检出整行错位；`0` 仅首两行（与旧版哈希兼容、灵敏度较低）。 |
| `CLIENT_VIDEO_PIPELINE_TRACE` | `1` | **四路视频环节日志**：在 RTP 预热日志量、`DECODE_OUT`/`PRESENT_TX`/`MAIN_PRESENT`/`RS_APPLY`/`SG_UPLOAD`、DMA-BUF pitch/offset、`[VideoE2E][RS][SG][CPU]` 等处按 **每路 frameId** 采样输出（前缀 `[Client][VideoE2E]`）；与 `CLIENT_VIDEO_EVIDENCE_CHAIN` 可叠加。 |
| `CLIENT_VIDEO_PIPELINE_TRACE_EARLY` | `48` | Pipeline trace：前 N 帧每帧打摘要（每路独立计数）。 |
| `CLIENT_VIDEO_PIPELINE_TRACE_INTERVAL` | `30` | Pipeline trace：之后每 N 帧一条；`0` 表示仅 EARLY 阶段。 |
| `CLIENT_VIDEO_CPU_PRESENT_FORMAT_STRICT` | （默认开启） | **CPU 呈现路径**（`RemoteVideoSurface` 与 **`QVideoSink::setVideoFrame`**）：须 `QImage::Format_RGBA8888` 且 `bytesPerLine >= width*4`，否则**拒绝呈现**（不打 `setVideoFrame` / 不提交纹理）并打 `[PresentContract][REJECT]`；设为 `0` 时允许 `convertToFormat(RGBA8888)`（仅兼容/排障，可能掩盖上游格式错误）。 |
| `TELEOP_GPU_OPTIONAL` / `CLIENT_GPU_PRESENTATION_OPTIONAL` | 见上表 | **多路视频 / 远控台**：不要将正确性寄托在 **Mesa llvmpipe** 对历史 BGRA 路径的行为上；解码→主线程呈现约定 **RGBA8888 + `bytesPerLine >= width*4`**。无硬件 GL 时可用 `TELEOP_GPU_OPTIONAL=1` 等关闭启动门禁，但**仍应保持 RGBA 契约**；CI 通过 `scripts/verify-client-video-rgba-contract.sh` 禁止热路径再引入 BGR/RGB32。 |

**像素/stride 契约（CPU 呈现）**：`H264Decoder` 软解经 swscale 输出 **RGBA** → `QImage::Format_RGBA8888`；`WebRtcClient` 对 `RemoteVideoSurface` 使用 **`applyCpuRgba8888Frame` + `CpuVideoRgba8888Frame::tryAdopt`**；对 **`QVideoSink`** 在默认严格模式下同样校验 **RGBA8888 + `bytesPerLine >= width*4`**，不满足则**不调用** `setVideoFrame`。**NV12 DMA-BUF** 走 `applyDmaBufFrame`，与 CPU 路径类型隔离。QML 若调用 `applyFrame(QImage)` 仍走兼容路径（可 normalize）。

### 2.0 视频条状/花屏：**优先**「落盘解码帧 + 同 frameId 日志」

1. 导出环境（示例）：`CLIENT_VIDEO_SAVE_FRAME=png`、`CLIENT_VIDEO_SAVE_FRAME_DIR=/tmp/vdiag`、`CLIENT_VIDEO_SAVE_FRAME_MAX=30`，并建议 **`CLIENT_VIDEO_EVIDENCE_CHAIN=1`**（或与 `CLIENT_VIDEO_PIPELINE_TRACE=1` 叠加）。  
2. 复现一次后：在目录中打开 `*_f<数字>.png`，记下 **`<数字>` = frameId**。  
3. 在客户端日志中过滤该路 stream，并搜同一 frameId，例如：  
   `grep -E 'FrameDump|DECODE_OUT|VideoEvidence|RS_APPLY|SG_UPLOAD' "$CLIENT_LOG_FILE" | grep 'stream=你的路名' | grep -E 'frameId=123|fid=123|_f123'`（将 `123` 换成实际 frameId）。  
4. **判读**：若 **PNG 已异常**，问题在解码/sws/色彩/隔行等（GPU 纹理尚未介入）；若 **PNG 正常而窗口仍花**，重点查 `SG_UPLOAD` / `TexSize` / GL 栈与窗口合成。

### 2.1 CARLA 仿真窗口（默认显示）

运行 `bash scripts/start-all-nodes-and-verify.sh` 时，CARLA 仿真窗口默认会弹出。脚本会在启动节点前自动执行 `xhost +local:docker`。

若窗口未弹出，确认宿主机有图形环境、已执行 `xhost +local:docker`，且 `DISPLAY` 与当前终端一致（`echo $DISPLAY` 多为 `:0` 或 `:1`）。

无头模式（不显示 CARLA 窗口）：`CARLA_SHOW_WINDOW=0 bash scripts/start-all-nodes-and-verify.sh`

---

若**手动**在容器内启动客户端，建议带上与脚本一致的参数，例如：

```bash
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec -it \
  -e DISPLAY=:0 -e QT_QPA_PLATFORM=xcb \
  -e ZLM_VIDEO_URL=http://zlmediakit:80 -e MQTT_BROKER_URL=mqtt://mosquitto:1883 \
  client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login'
```
（GL 栈由客户端自动选择；若须强制软件可加 `-e CLIENT_ASSUME_SOFTWARE_GL=1`。）

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
- **客户端**：根据 **glxinfo** / **nvidia-smi** 自动选择硬件或软件 GL；无可用 GPU 或无法建立 GLX 上下文时走软件光栅。建议在 client-dev 镜像中安装 **mesa-utils**（`glxinfo`）以便准确识别 AMD/Intel；仅 NVIDIA 时可依赖 **nvidia-smi** 辅助判定。

---

## 5. 检查清单（首次或出问题时）

| 检查项 | 命令/操作 |
|--------|------------|
| X11 允许 Docker | `xhost +local:docker` 或 `bash scripts/setup-host-for-client.sh` |
| DISPLAY 已设置 | `echo $DISPLAY` 应输出 `:0` 或 `:1` 等；容器会使用相同值，窗口未弹出时可尝试 `xhost +local:docker` 或与当前终端一致地设置 DISPLAY 后重跑 |
| 客户端日志 | 容器内：`tail -50 /tmp/remote-driving-client.log`；或宿主机：`docker compose ... exec client-dev tail -50 /tmp/remote-driving-client.log` |
| 视频花屏定界 | 见 **§2.0**：`CLIENT_VIDEO_SAVE_FRAME=png` + 同 frameId 对齐 `FrameDump` / `DECODE_OUT` / `SG_UPLOAD` |
| Qt xcb / libxcb-cursor0 报错 | 需重建 client-dev 镜像以安装依赖：`bash scripts/build-client-dev-full-image.sh`（见 `client/Dockerfile.client-dev`） |
| client-dev 镜像 | **remote-driving-client-dev:full 已具备运行条件**（Qt6、libdatachannel、FFmpeg、xcb 等），有该镜像即可直接启动；启动脚本使用 --no-build，首次或更新需先运行 `bash scripts/build-client-dev-full-image.sh` |
| 端口未被占用 | 再次运行前由 start-all-nodes-and-verify.sh 阶段 0 自动 down 已有节点 |

---

## 6. 相关文档与脚本

- **宿主机一次性设置脚本**：`scripts/setup-host-for-client.sh`
- **一键启动并验证（含客户端启动与诊断）**：`scripts/start-all-nodes-and-verify.sh`
- **客户端无界面 / libGL 等排查**：`docs/ADD_VEHICLE_GUIDE.md` 末尾「客户端无法启动 / libGL」、`docs/CLIENT_UI_VERIFICATION_GUIDE.md`
- **CARLA 与客户端拉流**：`docs/CARLA_CLIENT_STREAM_GUIDE.md`
