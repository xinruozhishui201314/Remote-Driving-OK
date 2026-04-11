# 环卫扫地车远程驾驶系统（ZLMediaKit + WebRTC + Keycloak + VIN权限）
版本：v1.3 (深度重构版)
更新日期：2026-02-06

## 1. 项目背景与目标
本项目为环卫扫地车提供一套端到端远程驾驶（Teleoperation）系统。车端通过 WebRTC 格式将多路摄像头视频推送到流媒体服务器 ZLMediaKit，由客户端驾驶舱从 ZLMediaKit 低时延拉流观看；客户端通过键盘或远程驾驶套件（方向盘/踏板/手柄）输入控制指令，经由 ZLMediaKit（优先 DataChannel；不可用则 WebSocket/转发通道）转发至车端执行。同时系统需要具备完整的安全与运维能力：鉴权、VIN绑定授权、会话锁、消息完整性、看门狗、故障码、异常处理、录制审计、监控告警等。

## 0. 目录结构
/
├── backend/          # C++
├── Vehicle-side/     # C++, 车端代理
├── client/           # qt C++
├── media/            # ZLMediaKit 配置与 Dockerfile
├── deploy/           # docker-compose, k8s helm
└── docs/             # 接口文档, 架构图

**核心目标**
- 多车管理：每台车都有唯一 VIN（类似身份证号）；一个账号下可添加多个 VIN 并授权不同权限。
- 低时延：满足远程驾驶对端到端时延（<150ms）/抖动/丢包的要求。
- 高安全：强鉴权 + 会话锁 + 消息完整性 + 防重放 + 断链安全停车 + 冗余通信。
- 易运维：故障码体系、日志、指标、回放/录制、审计追踪。

**非目标（v1）**
- 完整自动驾驶/规划控制算法（仅远程驾驶 + 安全约束）。
- 高级调度/工单系统（可在后续版本扩展）。

---

## 2. 术语
- VIN：车辆唯一标识。
- 车端代理：车载工控机进程，负责推流、收控、上报遥测/故障。
- 驾驶舱客户端：操作员端 Web/Electron 应用。
- 控制面：业务后端，负责鉴权、VIN权限、会话管理、审计。
- 数据面：ZLMediaKit 负责音视频/实时通道转发。
- Fail-safe：失效安全机制，指系统发生故障时进入预定安全状态（如停车）。
- 降级策略：在通信质量下降或部分子系统失效时，通过限制功能（如限速）来维持基本可控性的策略。
- 冗余链路：同时维护主（如 5G）备（如 4G/WiFi）两路通信通道，故障时自动切换。
- TraceID：跨设备追踪标识，用于关联客户端、后端、车端所有日志。

---

## 3. 总体架构（推荐）
### 3.1 组件
1) **Keycloak（鉴权）**
- 用户注册/登录、OIDC、JWT、角色管理。
- 作为唯一身份源（SSO）。

2) **远程驾驶业务后端（Teleop Backend）**
- JWT 校验、VIN 绑定与授权、会话管理（控制锁/观察者）、签发短期会话令牌、审计/录制索引、故障码入库、策略下发（限速/超时）。
- 数据库：PostgreSQL（建议）；遥测可选 TimescaleDB。

3) **ZLMediaKit（流媒体/转发， media/ZLMediaKit文件夹）**
- WebRTC 推拉流（车端 publish，客户端 play）。
- 鉴权：publish/play 必须 token 通过（ZLM hook/鉴权接口）。
- 控制通道：优先 WebRTC DataChannel；若 ZLM 不便实现，采用“ZLM WebSocket + 后端/网关转发”，但对外仍以 ZLM 域名统一入口与鉴权策略。

4) **车端代理（Vehicle-side文件夹）**
- 摄像头采集、硬件编码（H.264/H.265 视部署情况）、WebRTC 推流至 ZLM。
- 接收控制指令、执行到车辆控制器（CAN/串口/以太网）。
- 看门狗与安全停车、故障检测与上报。
- **链路冗余管理**：双网卡切换、网络质量评估。

5) **驾驶舱客户端（client文件夹）**
- 客户端远程驾驶操作界面设计为client/picture/远程驾驶客户端UI设计.png样式，可以根据显示的要求调整，以最小化调整，登录界面和选择车辆vin界面尽量简单。
- OIDC 登录（Keycloak）。
- 车辆列表与 VIN 授权可视化。
- 低时延拉流播放、遥测与故障展示、输入设备映射、死手（deadman）、远程急停（E-Stop）。

6) **可观测性**
- Prometheus + Grafana：时延/丢包/码率/会话数/看门狗触发。
- 日志：统一 JSON 结构化日志。
- 审计：谁在何时控制了哪台 VIN，操作事件。

---

## 4. 用户/角色与 VIN 权限模型
### 4.1 角色（Keycloak Realm Roles）
- admin：管理用户、车辆、策略、审计、录制。
- owner（账号拥有者）：管理自己账号下 VIN 的绑定、授权。
- operator：可申请控制指定 VIN（需被授权）。
- observer：仅拉流查看指定 VIN（需被授权）。
- maintenance：查看故障/诊断，可进行受限操作。

### 4.2 VIN 绑定与授权（必须实现）
**强约束：任何“看/控/维护”必须以 VIN 权限为准。**

- 一个账号（Account）下可以绑定多个 VIN：
  - 绑定方式：
    1) admin 后台绑定；或
    2) owner 输入 VIN + 车辆绑定码（由车端/出厂贴纸/后台生成）完成绑定。
- VIN 授权规则：
  - owner 可以把某 VIN 授权给若干用户（operator/observer/maintenance），并指定权限集合与有效期：
    - `vin.view`：允许拉流观看、查看遥测/故障
    - `vin.control`：允许申请控制锁并发送控制指令
    - `vin.maintain`：允许运行诊断/清故障（受限）
- 约束：
  - 同一 VIN 同一时刻只能有 1 个控制者，可有多个观察者。
  - 所有授权操作写入审计日志。

### 4.3 数据模型（概要）
- accounts(id, name, ...)
- users(id, keycloak_sub, account_id, ...)
- vehicles(vin PK, model, capabilities, safety_profile, ...)
- account_vehicles(account_id, vin, bind_time, status)
- vin_grants(vin, grantee_user_id, permissions[], expires_at, created_by)
- sessions(session_id, vin, controller_user_id, state, started_at, ended_at, ...)
- session_participants(session_id, user_id, role_in_session)
- fault_events(id, vin, session_id, code, severity, message, ts, payload)
- audit_logs(id, actor_user_id, action, vin, session_id, ts, ip, ua, detail_json)

---

## 5. 功能需求（全链路）
### 5.1 登录/注册/鉴权
- Keycloak 承担：
  - 用户注册（默认管理员创建；可选自注册+审核）
  - 登录（OIDC Authorization Code + PKCE）
  - 颁发 JWT（access/refresh）
- Teleop Backend：
  - 校验 JWT（JWKS、iss/aud/exp/nbf）
  - 结合 DB 做 VIN 授权判定（RBAC + ABAC：角色 + VIN grant）

### 5.2 车辆列表与状态
- 用户登录后仅能看到自己被授权的 VIN 列表：
  - online/offline
  - 当前控制者（若有）
  - 最近心跳时间
  - 故障摘要（最高严重级别）
  - 基础遥测：速度、电量/电压等

### 5.3 会话管理（控制锁/观察者）
- 创建会话：
  - operator 对某 VIN 发起“申请控制”
  - 后端检查：`vin.control` 授权 + VIN 在线 + 无控制者（或允许抢占但必须 admin）
  - 后端签发短期 session 凭证（详见 §9）
- 会话状态：
  - REQUESTED → ACTIVE → ENDING → ENDED / FAILED
- 退出会话：
  - operator 手动结束
  - 超时：N 秒无命令 → 自动结束并触发车端安全停车策略
  - 管理员强制结束
- 观察者加入：
  - 仅需 `vin.view`；不能发送控制

### 5.4 流媒体（ZLMediaKit + WebRTC）
- 车端推流：
  - WebRTC publish 到 ZLM
  - 多路摄像头：front/rear/right/left 等（能力由 vehicles.capabilities 决定）
  - 关键帧间隔建议 1s，可配置
- 客户端拉流：
  - 通过后端获取该 VIN 当前可用 stream 列表与播放 URL
  - 客户端 WebRTC play，显示统计（RTT、丢包、码率、FPS、解码耗时）
- 回退：
  - WebRTC 不可用时，观察者可用低延迟 HLS/FLV（不允许控制时使用回退流，避免错判时延）

### 5.5 控制链路（客户端→ZLM→车端）
- **多重冗余通信**：
  - 车端必须支持双网卡接入（如 5G + 4G 或 WiFi）。
  - 链路管理模块实时监测链路质量（RTT、丢包率、抖动）。
  - **切换逻辑**：
    - 主链路 RTT > 200ms 或 丢包率 > 10% 持续 1s → 触发备链路切换。
    - 切换需保证 TCP/UDP 连接的平滑过渡（如 SCTP 或应用层重连）。
    - 防抖动：设定链路切换的粘滞阈值，避免频繁跳变。
- 输入支持：
  - 键盘（WASD/方向键）
  - 远程驾驶套件（HID）：方向盘/油门/刹车/档位/死手
- 指令类型：
  - DriveCommand：方向、油门、刹车、档位、限速
  - ModeCommand：启停远程驾驶、灯光/喇叭、扫地作业开关
  - EStopCommand：远程急停
  - Heartbeat：维持链路活性与时延统计
- 频率：
  - 20~50Hz 可配置（默认 30Hz）
- 安全约束：
  - 必须 deadman = true 才允许输出油门
  - 车端必须二次校验：越界/突变/频率异常 → 拒绝并上报故障码
  - 看门狗：500ms（可配置）无有效心跳/指令 → 进入安全停车（SAFE_STOP）

### 5.6 遥测上报（车端→客户端）
- 10~20Hz：
  - 车速、方向角、档位
  - 电量/电压、控制器状态
  - 作业系统状态（刷盘/吸尘/喷水，如有）
  - 温度、CPU/GPU、编码器丢帧
  - **网络质量**：RTT、丢包率、当前使用链路（5G/4G/WiFi）
- 客户端 HUD 展示与历史曲线（session 内）

### 5.7 异常处理与故障码（必须实现）
- **故障码模型**：
  - code：格式 `XX-YYYY` (域-序列号)，如 `NET-1001`
  - severity：INFO/WARN/ERROR/CRITICAL
  - domain：TELEOP/NETWORK/VEHICLE_CTRL/CAMERA/POWER/SWEEPER/SECURITY
  - latch：是否锁存（需人工清除）
  - recommended_action
  - payload（结构化）
- **CRITICAL 触发**：
  - 车端立即安全停车
  - 控制保持但禁止再起步，直到故障清除（策略可配置）
- 客户端：
  - 故障列表、最新严重告警 banner
  - ACK（确认已读）
  - 对可清除故障发起 clear 请求（需 `vin.maintain`）

### 5.8 录制与审计
- 每个 session：
  - 录制视频（ZLM 服务端录制）
  - 记录控制指令/遥测/故障时间线（backend 入库）
- 审计要求：
  - 登录、会话开始/结束、控制锁获取/释放、E-Stop、授权变更、链路切换等必须写入 audit_logs
  - 可导出

---

## 6. 非功能需求（性能/稳定性/安全）
### 6.1 全链路时延模型
为确保远程驾驶的即时响应性，定义端到端时延模型。系统需保障总时延（P95）< 150ms。

| 阶段 | 符号 | 描述 | P95 目标 | 监控指标 |
| :--- | :--- | :--- | :--- | :--- |
| **1. 采集** | T1 | 摄像头曝光到进入采集缓冲区 | < 5ms | 传感器驱动耗时 |
| **2. 编码** | T2 | 编码器开始编码到输出首字节 | < 30ms | 编码器输出延迟 |
| **3. 传输** | T3 | 车端发出到客户端接收（单向网络） | < 50ms | RTT/2 + 抖动缓冲 |
| **4. 解码** | T4 | 接收缓冲区到解码完成 | < 30ms | 解码器输出延迟 |
| **5. 渲染** | T5 | 解码完成到屏幕显示（上屏） | < 15ms | GPU 渲染耗时 |
| **总时延** | E2E | T1 + T2 + T3 + T4 + T5 | **< 130ms** | (预留 20ms 抖动余量) |

**监控要求**：
- 车端需上报 T1, T2 时间戳。
- 客户端需上报 T4, T5 时间戳。
- 系统需根据 RTP 时间戳和 NTP/PTP 同步计算 T3。
- 时延超过 150ms 需触发 UI 告警，超过 300ms 触发“轻度降级”。

### 6.2 可靠性
- 断网/抖动：自动重连，重连过程中车辆保持 STOPPED
- 服务端故障：后端无状态可水平扩展；ZLM 可做主备/集群（按需要）
- 车端进程：systemd 守护、自启动、崩溃自动拉起
- **链路冗余**：双链路热备，故障切换时间 < 1s。

### 6.3 安全
- 全链路 TLS
- 最小权限
- 流媒体 publish/play 鉴权
- 控制消息完整性（见 §9）
- 防重放（seq + nonce + 时间窗）
- 限流：控制消息与 API

---

## 7. ZLMediaKit 接入要求（必须落地）
- 启用 WebRTC，并配置 STUN/TURN（推荐 coturn）
- 开启鉴权 hook：
  - publish 鉴权：仅允许车端持有有效 session/vehicle token 推送指定 vin/stream
  - play 鉴权：仅允许拥有 vin.view 的用户/会话拉流
- 录制开关按 session 控制（开始会话→开启录制；结束会话→停止并归档）

---

## 8. 复杂网络分布式部署与连接自愈
针对车辆位于不同网络环境（非局域网）的场景，系统必须支持复杂的网络穿透与自愈机制。

### 8.1 网络穿透与传输协议
1.  **ICE/STUN/TURN (WebRTC 视频流)**
    - **STUN (Session Traversal Utilities for NAT)**：用于获取公网 IP 和端口，支持 Full Cone 和 Symmetric NAT 穿透。
    - **TURN (Traversal Using Relays around NAT)**：当 STUN 失败（如处于对称 NAT 后），强制使用中继服务器（TURN）转发视频流。
    - **ICE (Interactive Connectivity Establishment)**：自动优先级排序策略：`Host (LAN) > Server Reflex (STUN) > Peer Reflex (Direct) > Relay (TURN)`。
2.  **MQTT Over TLS (控制面/信号)**
    - 用于指令下发和高可靠遥测上报。
    - 必须启用 TLS 1.3。
    - QoS 策略：控制指令 QoS 1 (At least once)，遥测数据 QoS 0 (At most once)。

### 8.2 连接自愈协议
定义一套在恶劣网络环境下保持连接可用的自动化协议。

**状态定义**：
- `HEALTHY_UDP`：UDP 传输正常，丢包率 < 10%，时延 < 200ms。
- `DEGRADED_UDP`：UDP 传输劣化，10% < 丢包率 < 20%。
- `FAILED_UDP`：UDP 不可达或丢包率 > 20%。
- `FALLBACK_TCP`：强制降级至 TCP 传输（WebSocket 或 MQTT Over TCP）。

**触发条件**：
1.  **丢包阈值触发**：统计窗口 2s 内，WebRTC 数据包丢失率 > 20%。
2.  **ICE 穿透失败**：连续 3 次 ICE Candidate 连接尝试失败，TURN 服务器无响应。
3.  **黑盒检测**：连续 5s 未收到任何视频 RTP 包或控制 ACK。

**自愈切换逻辑**：
1.  **触发检测**：网络监控模块监测到上述任一条件。
2.  **状态迁移**：
    - `HEALTHY_UDP` → `DEGRADED_UDP`：记录 `NET_W_1005` 告警，客户端 UI 黄色提示。
    - `DEGRADED_UDP` → `FAILED_UDP` 或直接满足强触发条件：
3.  **执行降级**：
    - **视频流**：尝试切换 WebRTC 候选对 到 TURN (TCP)，若失败则降级为 HLS/FLV (低延迟模式)。
    - **控制流**：从 WebRTC DataChannel 切换至 MQTT Over TLS 或 WebSocket Secure (WSS)。
    - **车辆行为**：立即触发“轻度降级”模式（限速 20km/h）。
4.  **恢复机制**：
    - 当 TCP 传输稳定且 UDP 探测包（每隔 10s 发送一次）连续 3 次成功（丢包 < 5%）：
    - 切换回 `HEALTHY_UDP`。
    - 恢复全速权限。
    - 产生 `NET_I_1006` 恢复日志。

---

## 9. 消息完整性与 Fail-safe 安全状态机
远程驾驶控制消息必须保证：完整性、身份可信、防重放、顺序/时序可控。

### 9.1 消息防篡改与防重放
**方案：WebRTC DataChannel（DTLS）**
- DTLS 天然提供加密与完整性。
- **应用层防重放**：
  - `seq` 单调递增。
  - `timestampMs` 与车端本地时间窗口校验（±2s）。
  - 每条消息包含 `sessionId`、`vin`。

### 9.2 消息字段（规范）
- schemaVersion
- vin
- sessionId
- seq（uint32，单调递增）
- timestampMs（客户端时间）
- nonce（可选）
- payload（drive/mode/estop）
- signature（当使用 HMAC 方案）

### 9.3 Fail-safe 降级策略（参考 ISO 26262）
系统根据系统状态和网络质量动态调整车辆行为：

| 系统状态 | 触发条件 | 车辆行为 | 通知 |
| :--- | :--- | :--- | :--- |
| **正常 (ACTIVE)** | 链路正常，时延 < 150ms | 全权限控制，响应速度正常 | 绿色指示灯 |
| **轻度降级** | 150ms < 时延 < 300ms 或 5% < 丢包 < 10% | **限速至 20km/h**，关闭扫地功能 | 黄色闪烁，语音播报“网络不稳定” |
| **重度降级** | 时延 > 300ms 或 丢包 > 10% | **限速至 5km/h**，禁止加速，建议停车 | 红色闪烁，语音播报“即将接管” |
| **链路丢失** | 心跳超时 (>500ms) | **急停**，方向盘归中（若支持） | 红色常亮，警报声 |
| **故障锁定 (FAULT_LOCKED)** | 硬件故障或 CRITICAL 码 | 切断动力，机械刹车 | 需人工复位 |

**车端验收规则（强制）**：
- seq <= last_seq：丢弃（并计数）
- timestamp 超窗：丢弃（并计数）
- signature 不通过：丢弃并上报 `SEC-7001`
- 频率超限：丢弃并上报 `SEC-7002`
- 连续丢包 > 阈值：进入降级模式

### 9.4 安全状态机（车端逻辑）
状态流转：
1.  **IDLE**（无会话）→ **ARMED**（鉴权通过，死手按下）。
2.  **ARMED** → **ACTIVE**（收到有效 DriveCommand）。
3.  **ACTIVE** → **DEGRADED_WARNING**（轻度/重度降级触发）。
4.  **DEGRADED_WARNING** → **ACTIVE**（网络恢复）。
5.  **ACTIVE** / **DEGRADED_WARNING** → **SAFE_STOP**（心跳丢失 / E-Stop）。
6.  **SAFE_STOP** → **STOPPED**（速度为0）。
7.  **STOPPED** → **IDLE**（会话结束）。

---

## 10. API（后端）概要（必须实现并可测试）
Base: /api/v1
鉴权：Bearer JWT（Keycloak）

- GET /me
- GET /vins                # 列出当前用户可见 VIN（含权限）
- POST /vins/bind          # owner/admin 绑定 VIN（vin + bind_code）
- POST /vins/{vin}/grant   # owner/admin 授权给用户
- POST /vins/{vin}/revoke
- GET /vins/{vin}/status
- POST /vins/{vin}/sessions                 # 创建会话（申请控制/观察）
- POST /sessions/{sessionId}/end
- POST /sessions/{sessionId}/lock           # 获取控制锁
- POST /sessions/{sessionId}/unlock
- GET  /sessions/{sessionId}/streams        # 获取拉流地址/streamId
- GET  /sessions/{sessionId}/recordings
- GET  /vins/{vin}/faults?active=true
- POST /faults/{faultId}/ack
- POST /faults/{faultId}/clear              # 需权限

**OpenAPI 真源**：正式路径、请求/响应模型以 `backend/api/openapi.yaml` 为准（与上表概要冲突时以 OpenAPI 为准并回写本节）。

---

## 11. 全链路结构化日志规范 (Observability)
为了实现精准的故障定位和性能分析，所有模块（Vehicle-side, Backend, Client）必须统一日志格式。

### 11.1 统一日志格式
所有日志必须输出为单行 JSON 格式。

**标准字段**：
- 时间戳、级别、消息、模块名；涉及远端会话时必须携带 `vin`、`sessionId`；跨服务追踪携带 `traceId`（与 §2 术语一致）。字段扩展须向后兼容（只 additive）。

---

## 12. 接口契约真源与分级演进（MVP / V1 / V2）

### 12.1 真源与 CI 门禁（冲突处理）

| 层级 | 单一真源 | 机器校验入口 |
|------|----------|----------------|
| HTTP API（Backend 对外） | `backend/api/openapi.yaml` | `.github/workflows/contract-ci.yml` → `scripts/verify-contract-artifacts.sh`（OpenAPI 结构）+ `scripts/verify-contract-v1-cross-service.sh`（路由与契约对齐） |
| MQTT 控制/状态消息 | `mqtt/schemas/*.json` | 同上 + `mqtt/schemas/examples/manifest.json` 下 golden 实例 |
| 客户端远驾 UI 模块边界 | `docs/CLIENT_UI_MODULE_CONTRACT.md` | `.github/workflows/client-ci.yml` → `scripts/verify-client-ui-module-contract.sh`（及 `verify-client-ui-quality-chain.sh`） |
| 业务与安全不变量 | **本文档** | 评审与场景/E2E；若与 OpenAPI / JSON Schema 字面冲突，**以本文档为准** 并应在同一次变更中修正契约文件 |

### 12.2 MVP（当前基线）

- **每层一真源**：HTTP→OpenAPI；MQTT→`mqtt/schemas`；QML→Facade 文档。
- **CI 每条链路至少一项校验**：`contract-ci`（HTTP+MQTT）与 `client-ci`（Facade）；本地可 `./scripts/verify-contract-artifacts.sh`。
- **兼容性默认**：字段与响应 **只 additive**；`schemaVersion` / OpenAPI `info.version` 按语义化版本管理。

### 12.3 V1（扩展基线）

- **Golden 全覆盖**：每种对外 MQTT Schema 在 `mqtt/schemas/examples/manifest.json` 中至少登记一条合法样例；新增消息类型须同步追加。
- **跨服务契约测试**：`scripts/validate_api_against_openapi.py` 校验 Backend 注册路由与 OpenAPI 路径模板（含 `{param}` 与 C++ 正则路由匹配）；由 `verify-contract-v1-cross-service.sh` 串联。
- **弃用策略**：见 §12.5；任何弃用须在 OpenAPI / Schema 描述中写明 `deprecated` 与下线时间窗口。

### 12.4 V2（发布绑定）

- **Breaking 自动检测**：PR 上对 `backend/api/openapi.yaml` 运行 `oasdiff breaking`（见 `scripts/verify-openapi-breaking-change.sh` 与 `contract-ci`）；发现 breaking 须 bump **主版本**（URL `/api/vx` 或 OpenAPI `info.version` 主版本）并一次性更新全部 consumers。
- **生成物绑定（可选演进）**：在发布流水线中对 OpenAPI / Schema 生成客户端 Stub 或文档站点，产物版本与镜像 tag 对齐；具体工具链由 `docs/API_STANDARDIZATION.md` 维护。

### 12.5 弃用策略（Deprecation）

1. **标记**：在 OpenAPI 的 operation 或字段、或在 JSON Schema 的字段描述中标注 `deprecated: true` 或文字「弃用 / 替换为 …」。
2. **并行期**：至少保留 **一个完整 minor 周期**（或不少于 4 周，取较长者），期间旧字段仍可读写；新字段优先在并行期内由新版本 producer 写入。
3. **移除**：仅允许在 **主版本升级** 中删除字段或收紧必填；必须同时提供迁移说明（`docs/` 或 OpenAPI description）与回滚方案。
4. **观测**：弃用期内记录「仍使用旧字段」的 metrics 或日志采样；归零后再执行移除。
