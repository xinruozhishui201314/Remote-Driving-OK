# 常见问题排查手册（Runbook）

本手册按“现象 → 排查步骤 → 解决方案”组织，配合问题分析工具链使用。

---

## 问题分析工具链（高效分析入口）

| 步骤 | 命令/文档 | 说明 |
|------|------------|------|
| 1. 收集 | `./scripts/analyze.sh` | 将各模块日志与交互记录收集到 `diags/<timestamp>/` |
| 2. 自动诊断 | 查看 `diags/<timestamp>/diagnosis.txt` | `auto_diagnose.py` 基于关键词与 `err=`/`code=` 输出可能原因 |
| 3. 错误码 | [docs/ERROR_CODES.md](ERROR_CODES.md) | 日志中的统一错误码含义与对应排查方向 |
| 4. 本手册 | 下方按章节 | 按现象查“排查步骤”与“解决方案” |
| 5. 分布式 | [docs/DISTRIBUTED_DEPLOYMENT.md](DISTRIBUTED_DEPLOYMENT.md) | 跨网/多机部署时的端点与配置检查清单 |

**快速诊断入口**：执行 `./scripts/analyze.sh` → 打开 `diags/<timestamp>/diagnosis.txt` → 根据提示或 `err=`/`code=` 在本手册中搜索对应章节。

---

## 1. Client 登录失败

### 现象
- 登录页一直转圈或提示“网络错误”
- F12 Network 中 `/realms/teleop/protocol/openid-connect/token` 返回 400/401/503

### 排查步骤
1. 检查 Keycloak 容器是否 Up：`docker compose ps keycloak`。
2. 检查 Keycloak 就绪：`curl http://localhost:8080/health/ready`。
3. 检查 Client 到 Keycloak 的网络是否可达：从 Client 所在网络 `curl http://<KEYCLOAK_URL>/health/ready`。
4. 确认 Realm 名称（`teleop`）与 Client 代码中的 issuer 一致。

### 解决方案
- 若 Keycloak 未启动：`docker compose up -d keycloak`。
- 若 Keycloak 不就绪：等待 60–120 秒（首次启动较慢），或查看 Keycloak 日志 `docker compose logs keycloak`。
- 若域名/IP 错误：修改 Client 登录页的 serverUrl（应指向 Backend，由 Backend 返回 issuer）；或环境变量 `KEYCLOAK_URL`。

---

## 2. Client 选车后创建会话失败（403/503）

### 现象
- 选车后点“创建会话”或“确认并进入驾驶”，提示 403/503。
- Backend 日志中 `403 forbidden` 或 `503 internal`。

### 排查步骤
1. 检查 Backend 日志：`docker compose logs backend | grep POST.*sessions`。
2. 查看该请求的 vin 与当前用户的 vin_grants：查询 DB `SELECT * FROM vin_grants WHERE vin='...';`。
3. 确认该 vin 是否在用户账号下被授权：Backend 日志应显示 `[Backend][POST sessions] vin=... has_access=true/false`。
4. 若 503，检查 DB 是否可达：`docker compose exec postgres pg_isready`；Backend 日志中的 DB 连接错误。

### 解决方案
- 若 vin 未授权：通过 admin 侧或 owner 侧添加车辆/授权（见 ADD_VEHICLE_GUIDE）。
- 若 DB 不可达：检查 postgres 容器与 `DATABASE_URL` 配置。
- 若 Backend 容器异常：重启 Backend `docker compose restart backend`。

---

## 3. Client 拉流黑屏或状态 Closed

### 现象
- 创建会话后，四路画面均黑屏；F12 Network 中 WHEP 请求失败或状态 Closed。
- `auto_diagnose.py` 识别出“WebRTC 拉流失败”。

### 排查步骤
1. 检查 ZLM 是否收到流：`curl http://<ZLM_HOST>/index/api/getMediaList`，看是否存在 `<vin>-<sessionId>` 流。
2. 检查车端是否在推流：查看 carla-bridge 或 Vehicle 日志中是否有“推流目标”与“worker 已启动”。
3. 检查 WHEP URL 是否有效：复制 URL 到 Postman/curl，看返回是否为 SDP。
4. 检查 Coturn 是否正常：`docker compose logs coturn`；检查 TURN 端口是否开放（UDP/TCP 3478，中继范围）。
5. 查看 F12 WebRTC 统计（RTT、丢包、带宽）。

### 解决方案
- 若 ZLM 无流：检查车端 ZLM 配置（ZLM_HOST/ZLM_RTMP_PORT）；确认车端网络能到 ZLM。
- 若 WHEP URL 失效（session 已结束）：重新创建会话。
- 若 TURN 不可达：检查 `COTURN_EXTERNAL_IP` 与防火墙；确保 Client 能访问 TURN 的 UDP/TCP 端口。
- 若浏览器限制：允许使用摄像头/麦克风；禁用 VPN（部分 VPN 阻断 UDP）。

---

## 4. 车端推流失败（ZLM 无流）

### 现象
- Client 黑屏，且 `auto_diagnose.py` 识别“车端推流未到达 ZLM”。
- carla-bridge 日志中“首帧超时”或“worker 已退出”。

### 排查步骤
1. 检查 carla-bridge 容器日志：`docker compose logs carla-bridge | grep ZLM`。
2. 检查 ZLM 日志：`docker compose logs zlmediakit | grep rtmp`。
3. 确认车端网络能否到 ZLM：从车端容器 `telnet <ZLM_HOST> 1935`。
4. 检查 ZLM 配置：`deploy/zlm/config.ini` 中 `rtmp.enable=1`。

### 解决方案
- 若 ZLM 未启动：`docker compose up -d zlmediakit`。
- 若网络不通：检查防火墙、安全组开放 1935（RTMP）与 80/443（HTTP(S)）。
- 若 ZLM 端口配置错误：核对车端的 `ZLM_HOST`、`ZLM_RTMP_PORT` 与部署一致（见 DISTRIBUTED_DEPLOYMENT.md）。

---

## 5. MQTT 控制无响应（车端收不到控制指令）

### 现象
- Client 方向盘/踏板操作无反应；车端日志无“收到 vehicle/control”消息。
- `auto_diagnose.py` 识别“MQTT 连接失败”或“无订阅记录”。

### 排查步骤
1. 检查 Client 日志：是否有“连接 broker 成功”与“订阅 vehicle/control”。
2. 检查车端/Bridge 日志：是否有“MQTT 已连接”与“订阅 vehicle/control 成功”。
3. 检查 Mosquitto 日志：`docker compose logs mosquitto`，看是否有连接/订阅/发布记录。
4. 确认 Broker 地址一致：Client 与车端都连接同一 `mqtt://<host>:<port>`。

### 解决方案
- 若 Mosquitto 未启动：`docker compose up -d mosquitto`。
- 若 Broker 地址不一致：修改 Client 的连接设置（UI 或环境变量）与车端 `MQTT_BROKER_URL`。
- 若认证失败：检查 mosquitto 配置中的 ACL 与密码；`MQTT_VEHICLE_PASSWORD`、`MQTT_CLIENT_PASSWORD`。

---

## 6. 数据库连接失败（Backend 503）

### 现象
- Client 访问 `/api/v1/vins` 等 API 返回 503；Backend 日志中“database failed”。
- `auto_diagnose.py` 识别“数据库连接失败”。

### 排查步骤
1. 检查 postgres 容器：`docker compose ps postgres`。
2. 从 Backend 容器尝试连接：`docker compose exec backend pg_isready -h postgres -U teleop_user -d teleop_db`。
3. 检查 `DATABASE_URL` 环境变量是否正确（格式 `postgresql://user:password@host:port/db`）。
4. 查看 Backend 日志中的 DB 错误（如 “FATAL: password authentication failed”）。

### 解决方案
- 若 postgres 未启动：`docker compose up -d postgres`。
- 若密码错误：修改 `.env` 中 `POSTGRES_PASSWORD` 并重建数据库或更新 pg_hba.conf（开发环境可重置）。
- 若网络不通：检查 Backend 到 postgres 的网络（同 teleop-network）。

---

## 7. CARLA 仿真正常启动

### 现象
- carla-bridge 日志中“CARLA 连接失败”或“RPC 超时”；`auto_diagnose.py` 识别。
- 仿真车辆未出现或控制无反应。

### 排查步骤
1. 检查 carla-server 容器：`docker compose ps carla-server`（若有）。
2. 从 carla-bridge 容器尝试连接：`docker compose exec carla-bridge nc -zv <CARLA_HOST> 2000`。
3. 检查 CARLA 日志（若单独容器）。

### 解决方案
- 若 CARLA 未启动：按 README 启动 CARLA（`./scripts/start-carla-sim.sh` 或 docker compose）。
- 若端口错误：核对 `CARLA_HOST` 与 `CARLA_PORT`（默认 127.0.0.1:2000）。
- 若版本不兼容：确保 `pip install carla` 版本与 CARLA 服务一致。

---

## 8. 右视图/高精地图不显示（布局诊断）

### 直接查看远驾操作界面
```bash
bash scripts/show-driving-ui.sh   # 仅远驾操作界面，跳过登录，约 2 秒后自动进入
```
若未起效：1) 确保在图形桌面终端运行（非 SSH 无头）；2) `xhost +local:docker`；3) 若 client-dev 启动失败，检查 `docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml -f docker-compose.layout-preview.yml logs client-dev`

### 快速验证（布局专项脚本，约 14ms）
```bash
bash scripts/verify-driving-layout.sh          # 快速模式
bash scripts/verify-driving-layout.sh --compile # 含编译
```

### 运行时日志定位
```bash
docker compose logs client-dev 2>&1 | grep "\[Client\]\[UI\]\[Layout\]"
```

### 现象
- 右视图或高精地图区域空白、不显示。

### 排查步骤
1. 查看布局日志：`docker compose logs client-dev 2>&1 | grep "\[Client\]\[UI\]\[Layout\]"`
2. 关注 `右列=WxH`、`右视图=WxH`、`高精地图=WxH`，若为 0x0 说明未获得布局空间
3. 参考 [docs/CLIENT_UI_LAYOUT_DEBUG.md](CLIENT_UI_LAYOUT_DEBUG.md) 进行诊断

### 解决方案
- 若右列=0x0：检查中列是否使用 `Layout.fillWidth: true`，应移除以保留右列空间
- 若右视图/高精地图=0x0：增加 `Layout.minimumWidth`/`Layout.minimumHeight`

---

## 9. Client 崩溃/退出

### 现象
- Client 突然关闭；容器内日志中 “segfault” 或 “signal 11”。

### 排查步骤
1. 查看 client-dev 容器日志最后几十行：`docker compose logs client-dev | tail -100`。
2. 检查是否有 “QML 加载失败” 或 “Resource not found”。
3. 若启用 core dump，查看堆栈（需要在容器内配置 ulimit 与 gdb）。

### 解决方案
- 若 QML 缺失：确认 QML 资源是否挂载正确（docker-compose 映射）；路径大小写。
- 若依赖缺失：检查 client-dep 镜像是否包含 `libdatachannel`、Qt 等。
- 若代码崩溃：启用调试符号、gdb 定位到具体行；临时可尝试重启 client `docker compose restart client-dev`。

---

## 9. 防火墙/网络问题

### 现象
- 某个链路始终“连接超时”；但在本地可以。
- 例如：Client 能访问 Backend，但无法访问 MQTT Broker（跨网）。

### 排查步骤
1. 在出问题的一端执行 `telnet <target> <port>` 或 `nc -zv <target> <port>`。
2. 查看防火墙规则：`sudo iptables -L -n` 或云服务商安全组。
3. 检查 Docker 网络：`docker network inspect teleop-network`。

### 解决方案
- 开放必要的端口：
  - Backend: HTTPS（默认 443 或 8080）
  - MQTT: 1883 / 8883（TLS）
  - ZLM: 80/443、1935（RTMP）
  - Coturn: 3478（STUN/TURN UDP/TCP）与中继端口范围
- 使用 NAT 穿透或 VPN 确保各模块可互访（见 DISTRIBUTED_DEPLOYMENT.md）。

---

## 10. 时延/丢包问题

### 现象
- 拉流卡顿、控制反应慢；F12 WebRTC 统计中 RTT > 500ms、丢包 > 5%。

### 排查步骤
1. 测试网络连通性：在各模块间 `ping` / `mtr`。
2. 查看带宽：若视频码率 > 带宽，必然卡顿。
3. 检查 ZLM 与 Coturn 的 CPU/负载。

### 解决方案
- 就近部署 ZLM/MQTT：减少物理距离。
- 调整视频参数：降低分辨率/帧率/码率（carla-bridge：`CAMERA_WIDTH`/`CAMERA_HEIGHT`/`CAMERA_FPS`，以及 **`VIDEO_BITRATE_KBPS`（Python 脚本默认 2000；`docker-compose.carla.yml` 当前默认 512kbps/路、1280×720）**；可按需改 env 覆盖）。
- 优先使用有线网络；避免公共 Wi-Fi。
- 检查 Coturn 配置，选择就近的中继服务器。

---

## 11. start-all-nodes-and-verify.sh 起效不了 / 不生效

### 现象
- 执行 `bash scripts/start-all-nodes-and-verify.sh` 后：脚本报错退出、客户端未弹出、QML 修改不生效、或部分节点未就绪。

### 快速诊断
```bash
# 一键收集诊断信息（约 10 秒）
bash scripts/diagnose-start-all.sh
# 输出会指出失败阶段与可能原因
```

### 按阶段排查

| 阶段 | 失败表现 | 常见原因 | 解决方案 |
|------|----------|----------|----------|
| **阶段 0** | 端口/资源冲突 | 已有容器占用端口 | 手动 `docker compose ... down` 后重试 |
| **阶段 1** | 编译验证失败 | client-dev 镜像缺失 | 先执行 `bash scripts/build-client-dev-full-image.sh` |
| | | backend/vehicle 编译失败 | 查看 `${TMPDIR}/compile-verify-*/backend.log` 等 |
| **阶段 2** | 启动失败 | 端口被占用 | `lsof -i :8081 -i :1883 -i :80` 检查 |
| | | 网络缺失 | `docker network create teleop-network` |
| **阶段 3** | 某节点未就绪 | Backend 8081 无响应 | 等待 10–15s 或 `docker compose logs backend` |
| | | Keycloak/ZLM 未就绪 | 首次启动需 60–120s，可 `sleep 30` 后重跑 |
| **客户端** | 窗口不弹出 | DISPLAY/X11 未配置 | `xhost +local:docker`；`echo $DISPLAY` 应为 `:0` 或 `:1` |
| | QML 修改不生效 | 未挂载 client 或缓存 | 确认 `docker-compose.vehicle.dev.yml` 挂载 `./client`；重启 client-dev |

### 分步执行（定位失败点）
```bash
# 1. 跳过编译，直接启动（若镜像已就绪）
SKIP_COMPILE=1 bash scripts/start-all-nodes-and-verify.sh

# 2. 不启动 CARLA（无 GPU/无 CARLA 时）
SKIP_CARLA=1 bash scripts/start-all-nodes-and-verify.sh

# 3. 不启动 Vehicle（仅测 Backend/Client）
SKIP_VEHICLE=1 SKIP_CARLA=1 bash scripts/start-all-nodes-and-verify.sh

# 4. 仅编译验证（不启动）
bash scripts/parallel-compile-verify.sh
```

### 前置依赖检查
- **Client-Dev 镜像**：`docker image inspect remote-driving-client-dev:full` 应成功；否则执行 `bash scripts/build-client-dev-full-image.sh`。
- **libdatachannel**：`client/deps/libdatachannel-install` 存在；否则先运行 `bash scripts/install-libdatachannel-for-client.sh`。
- **X11**：在有图形桌面的终端运行；`echo $DISPLAY` 为 `:0` 或 `:1`；`xhost +local:docker` 已执行。

### 解决方案汇总
- 若提示「Client-Dev 镜像不存在」：`bash scripts/build-client-dev-full-image.sh` 后重跑。
- 若 Phase 1 编译失败：查看 `/tmp/compile-verify-*/backend.log`、`vehicle.log`、`client-dev.log` 最后 20 行。
- 若 Phase 3 某服务未就绪：`docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml ps` 查看状态；`docker compose logs <服务名>` 排查。
- 若客户端无窗口：执行 `bash scripts/setup-host-for-client.sh`；确认 DISPLAY 与宿主机一致。
- 若 QML 修改不生效：确认 `docker-compose.vehicle.dev.yml` 中 client 挂载 `./client:/workspace/client:ro`；修改后需重启 client-dev 容器。

---

## 12. 如何使用交互记录快速定位问题

1. 开启记录：设置 `RECORD_INTERACTION=1` 与 `RECORD_INTERACTION_DIR=./recordings`。
2. 复现问题后，收集：`./scripts/analyze.sh`。
3. 使用分析脚本：

```bash
# 按时间范围过滤
./scripts/analyze_interaction_log.py --dir ./recordings \
  --since "2026-02-23T12:00:00" --until "2026-02-23T12:05:00" \
  --out timeline.txt

# 按模块/VIN 过滤
./scripts/analyze_interaction_log.py --dir ./recordings \
  --module carla-bridge --vin carla-sim-001
```

4. 在 `timeline.txt` 中对照问题发生的时间点，看哪一环节缺失/异常。

---

## 13. 汇报问题时请附带

为提高问题定位效率，请提供：

1. `analyze.sh` 产生的目录（diags/<timestamp>）。
2. 问题发生的精确时间（UTC 或本地时区）。
3. 涉及的 VIN 与 session_id（若有）。
4. 重现步骤（最小化步骤最好）。
5. Client F12 Network 日志片段（若有前端错误）。
