# 整个链路验证说明

## 结论：已具备验证整个链路的能力

从「鉴权 / 后端 / ZLM / MQTT / 车端推流 → 客户端拉流 → 界面操作」的整条远程驾驶链路，均可通过**一条命令**完成启动与自动化验证；缺 client-dev 镜像时会自动在宿主机构建并打 tag。

**推流触发方式**：车端**不在启动时自动推流**。仅在**客户端操作「连接车端」**时，客户端通过 MQTT 发送 `start_stream`，车端订阅到后执行 `VEHICLE_PUSH_SCRIPT`（测试图案或「边读数据集边推流」脚本），实现按需推流。

---

## 1. 一键验证整个链路（推荐：脚本直接验证，无需先起节点）

```bash
make verify-full-chain
```

等价于：`bash scripts/start-full-chain.sh no-client`

**会依次执行：**

1. **确保 client-dev 镜像存在**：若本地无 `remote-driving-client-dev:full`，则在宿主机拉取/编译 libdatachannel，挂载进容器后 commit 打 tag。  
2. **启动所有节点**：Postgres → Keycloak → Coturn → ZLMediaKit → Backend → MQTT → 车辆端 → client-dev。  
3. **逐环体验证**：Postgres、Keycloak、Backend /health、ZLM API、MQTT 发布、车端推流 → ZLM 四路流。  
4. **四路流 E2E**：`verify-stream-e2e.sh`（MQTT start_stream → 车端推流 → ZLM 四路流就绪）。  
5. **连接功能验证（可选）**：默认**不跑**；若需原「18s 自动连视频、跳过登录」的脚本化校验，请使用 `RUN_AUTO_CONNECT_VERIFY=1 bash scripts/start-full-chain.sh no-client` 或 `bash scripts/start-full-chain.sh no-client auto-connect`（无 DISPLAY 时仍会跳过 2c）。  
6. **不启动客户端 UI**，验证通过即退出，适合 CI 或本地“只验不操作”。

任一环节失败则脚本退出非 0。

---

## 2. 一行命令：启动全链路 + 验证 + 启动客户端（界面操作）

```bash
make e2e-full
```

等价于：`bash scripts/start-full-chain.sh`

**会依次执行：** 与上面 1～4 相同（确保镜像 → 启动节点 → 逐环验 → 四路流 E2E）；**默认不跑**第 5 步「跳过登录的自动连视频」；**然后**启动客户端窗口（`CLIENT_AUTO_CONNECT_VIDEO=0`），**从登录页开始**供完整人工验证。若仍要跑第 5 步再加参数：`bash scripts/start-full-chain.sh auto-connect`。

**人工验证步骤：** 登录（如 123/123）→ 选车并进入驾驶 → 点击「连接车端」→ 等待约 2.5s 拉四路流 → 确认四路「已连接」或有无画面。

**停止全链路：** `make e2e-stop`

---

## 3. 自动化验证（节点已启动时）

**前提：** 全链路节点已启动（例如已执行过 `make e2e-start-no-client` 或 `make verify-full-chain` 后未 stop）。

```bash
make verify-e2e
```

**包含：**

| 脚本 | 验证内容 |
|------|----------|
| **verify-stream-e2e** | 服务 Up → 发送 MQTT start_stream → 轮询 ZLM 直到 teleop 下四路流就绪（cam_front/cam_rear/cam_left/cam_right） |
| **verify-connect-feature** | client-dev 内编译客户端 → 以 CLIENT_AUTO_CONNECT_VIDEO=1 运行约 18 秒 → 校验日志：进入主界面、触发 connectFourStreams、MQTT start_stream、四路拉流、-400 重试逻辑等，无崩溃即通过 |

**典型用法：**

```bash
make verify-full-chain      # 一键验证整链（含确保镜像、启动、全部自动化验证）
# 或
make e2e-start-no-client    # 仅启动节点，不启动客户端
make verify-e2e             # 自动化验证四路流 + 连接功能
```

---

## 3.1 全链路 + 仿真验证（所有节点 + CARLA 仿真）

**前提：** 已启动**所有节点**（含车端 vehicle），即 `bash scripts/start-full-chain.sh no-client` 或等价命令。

- **车端** 仅响应 `VEHICLE_VIN`（默认 `E2ETESTVIN0000001`）的 start_stream/stop_stream。
- **CARLA Bridge** 仅响应 `carla-sim-001` 的 start_stream/stop_stream；与车端可同时运行，按 VIN 分流。

**验证脚本：**

```bash
./scripts/verify-full-chain-with-carla.sh
```

会依次：1）检查服务；2）等待车端订阅；3）发送 start_stream（VIN=E2ETESTVIN0000001），等待 ZLM 四路流 → **车端路径**；4）发送 start_stream（VIN=carla-sim-001），等待 ZLM 四路流 → **CARLA 仿真路径**。若未启动 CARLA 与 carla-bridge，第 4 步会超时并提示启动方式。

**仿真整链人工验证：** 启动所有节点后，在宿主机启动 CARLA 与 `carla-bridge`，再启动客户端；选车 **carla-sim-001**，点击「连接车端」即可看到仿真四路推流。详见 [CARLA_CLIENT_STREAM_GUIDE.md](CARLA_CLIENT_STREAM_GUIDE.md)。

### 3.2 远驾客户端 → CARLA 仿真 逐项验证（推荐）

**设计说明：** 实车部署时仅部署 **Vehicle-side**（流媒体与车辆的桥梁）；CARLA 仅用于验证远驾链路。逐项验证脚本在每一步标注「实车对应」，便于对照实车部署。

```bash
./scripts/verify-client-to-carla-step-by-step.sh
```

**前提：** `./scripts/build-carla-image.sh` 且 `./scripts/start-all-nodes.sh` 已执行。

**验证 7 项：** 1）基础设施；2）仿真端桥梁（CARLA + C++ Bridge，实车对应 Vehicle-side）；3）鉴权与会话；4）MQTT start_stream；5）ZLM 四路流就绪；6）控制指令 drive/remote_control；7）stop_stream。详见 [VERIFY_CLIENT_TO_CARLA_CHAIN.md](VERIFY_CLIENT_TO_CARLA_CHAIN.md)。

---

## 4. 其他验证目标（按需）

| 命令 | 说明 |
|------|------|
| `./scripts/verify-all-client-to-carla.sh` | **远驾→CARLA 全面功能验证**（8 项：镜像能力/基础设施/Backend/ZLM/MQTT/C++ Bridge/整链逐项/整链一次性） |
| `./scripts/verify-client-to-carla-step-by-step.sh` | **远驾→CARLA 逐项验证**（7 项，每项标注实车对应；Vehicle-side 为实车桥梁） |
| `make verify-full-chain` | **一键验证整链**：确保镜像 → 启动全链路 → 逐环验 + 四路流 E2E + 连接功能验证（不启动 UI） |
| `make verify` | 客户端在容器内编译并运行约 6 秒，无崩溃即通过 |
| `make verify-stream-e2e` | 仅验证「MQTT start_stream → 车端推流 → ZLM 四路流就绪」 |
| `make verify-vehicle-dataset-local` | **车端数据集本地校验**（在 Vehicle-side 容器内执行，推荐先做；需已挂载数据集卷） |
| `make verify-vehicle-dataset` | 车端数据集验证（宿主机，SWEEPS_PATH=路径） |
| `make verify-vehicle-dataset-docker` | 同 verify-vehicle-dataset-local |
| `make verify-connect` | 仅验证连接功能（编译 + 自动连接模式运行 18 秒 + 日志校验） |
| `make verify-client-video` | 客户端视频管线：源码结构 + CMake 配置 |
| `make verify-client-video-docker` | 在 client-dev 容器内做视频管线验证并编译 |
| `make e2e-status` | 查看全链路各节点运行状态 |

---

## 5. 链路覆盖范围

| 环节 | 启动 | 自动化验证 | 人工验证 |
|------|------|------------|----------|
| Postgres / Keycloak / Coturn | ✓ e2e-full | ✓ start-full-chain 逐环检 | - |
| ZLMediaKit | ✓ | ✓ API + 四路流就绪 | - |
| Backend | ✓ | ✓ /health | - |
| MQTT | ✓ | ✓ 发布 start_stream | - |
| 车辆端 | ✓ | ✓ 收 MQTT → 推流 → ZLM 四路流 | - |
| 客户端 | ✓ e2e-full 启动窗口 | ✓ verify-connect（自动连接模式） | ✓ 登录、选车、点连接、看四路 |

整体而言，**已具备从基础设施到车端推流、再到客户端拉流与界面操作的整个链路验证能力**。

---

## 6. 四路流超时未就绪（VERIFY_FAIL）排障

**现象**：`[4/4] VERIFY_FAIL: 超时未在 ZLM 上发现四路流 (app=teleop: cam_front ...)`。

**常见原因与处理：**

| 原因　　　　　　　　　　　　　 | 说明　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　| 处理　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　|
| --------------------------------| ---------------------------------------------------------------------------------------------------------------------------------------------------| -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **车端首次启动需编译**　　　　 | vehicle 使用 Dockerfile.dev，入口会先在容器内 `cmake` + `make`，再启动 VehicleSide 并订阅 MQTT。若在编译完成前就发送了 start_stream，车端收不到。 | 脚本已改为：等待 60s、且每 5s 重发一次 start_stream，车端就绪后能收到。若仍超时，可先 `make e2e-stop` 再 `make e2e-full` 重跑一次（第二次车端通常已编译好，启动快）。　　　　　　　　　　　　　　　　　　　　　　　　　 |
| **MQTT 发布主机名**　　　　　　| 从 `compose run` 临时容器内发布时，需用**服务名** `mqtt-broker` 连接 broker（与 vehicle 的 `MQTT_BROKER_URL=mqtt://mqtt-broker:1883` 一致）。　　 | 脚本已使用 `-h mqtt-broker`。　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　 |
| **车端未连上 MQTT 或推流失败** | 网络、broker 未就绪、或推流脚本缺 ffmpeg/环境。　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　 | 查看车端日志：`docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml logs vehicle`，确认是否有「已订阅主题」「收到 start_stream」「执行推流脚本」及 ffmpeg 报错。　　　　　　　　　　　　　　　　　　　|
| **ZLM 未就绪或 app 名不一致**　| 验证脚本查询 `app=teleop`，车端推流需使用相同 `ZLM_APP=teleop`。　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　| 确认 vehicle 环境变量 `ZLM_APP=teleop`、`ZLM_HOST=zlmediakit`；手动查流：`curl -s "http://127.0.0.1:80/index/api/getMediaList?secret=${ZLM_SECRET}&app=teleop"`（ZLM_SECRET 见 `deploy/zlm/config.ini [api] secret`）。 |

即使该步失败，脚本仍会继续并启动客户端；在界面点击「连接车端」会再次发送 start_stream，车端收到后推流，四路即可出现。
