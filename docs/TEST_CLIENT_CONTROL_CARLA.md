# 依次测试：客户端控制 CARLA 仿真车辆

按以下顺序执行，可验证远驾客户端是否能正常控制 CARLA 仿真系统中的车辆。

---

## 1. 前置条件

- 宿主机已安装 Docker、docker compose。
- （可选）宿主机已执行一次 `bash scripts/setup-host-for-client.sh`，以便后续启动客户端 GUI。
- 若使用 CARLA 容器：需先构建 CARLA 镜像（见下）。

---

## 2. 测试顺序

### 步骤 A：构建 CARLA 镜像（首次或镜像不存在时）

```bash
./scripts/build-carla-image.sh
```

若本地已有 `remote-driving/carla-with-bridge:latest` 或使用 Python Bridge 不需 C++ 构建，可跳过。

### 步骤 B：启动所有节点（含 CARLA 与 Bridge）

```bash
./scripts/start-all-nodes.sh
```

或一键启动并做节点状态检查（不自动弹客户端）：

```bash
./scripts/start-all-nodes-and-verify.sh
```

- 会启动：Postgres、Keycloak、Coturn、ZLM、Backend、MQTT、Vehicle、client-dev、**CARLA 仿真**、**CARLA Bridge**（容器内）。
- 可选：`SKIP_CARLA=1` 不启动 CARLA；`CARLA_MAP=Town02` 换地图。

### 步骤 C：等待服务就绪（可选）

```bash
./scripts/wait-for-health.sh
```

确认 Backend、ZLM、Keycloak 返回正常后再做后续验证。

### 步骤 D：整链逐项验证（客户端 → CARLA 仿真）

自动化验证：基础设施 → CARLA 容器 → 会话 API → MQTT start_stream → 推流到 ZLM → MQTT remote_control/drive → stop_stream。

```bash
./scripts/verify-client-to-carla-step-by-step.sh
```

- 通过即表示：**会话创建、MQTT 控制、Bridge 收包、ZLM 四路流** 均正常，客户端在界面上选车 `carla-sim-001` 并「连接车端」后应能拉流并控制。

### 步骤 E：控制链路验证（依据 CARLA 容器日志）

验证 MQTT 下发 remote_control / drive 后，CARLA 容器内 Bridge 是否收到并写日志、是否发布 vehicle/status。

```bash
./scripts/verify-client-control-carla.sh
```

- 通过即表示：**控制指令被 Bridge 接收并处理**，日志中出现 `[Control] 收到 type=remote_control`、`type=drive` 及 vehicle/status 发布。
- **若使用 Python Bridge 且步骤 2/4（drive）失败**：需确保 Bridge 支持 `type=drive`（本项目 `carla-bridge/carla_bridge.py` 已支持）。若通过挂载使用本地 `carla-bridge`，修改后**重启 carla 容器**即可；若使用镜像内代码，需重新构建镜像并重启：`./scripts/build-carla-image.sh` 后 `docker compose ... up -d carla`。

### 步骤 F：启动客户端并在界面中人工验证（可选）

1. 启动客户端：

   ```bash
   docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec client-dev bash -c 'cd /workspace/client/build && ./remote_driving_client'
   ```

   或按 `start-all-nodes-and-verify.sh` 输出中的命令执行。

2. 在客户端中：
   - 登录：用户名 `e2e-test`，密码 `e2e-test-password`（或当前 Keycloak 中配置的测试账号）。
   - 选择车辆：**carla-sim-001**。
   - 点击「确认并进入驾驶」/「连接车端」。
   - 等待四路画面出现后，点击「远驾接管」，用键盘或方向盘输入控制，观察 CARLA 仿真中车辆是否响应。

---

## 3. 一键顺序（推荐）

若希望从零开始一次性跑完自动化验证（不包含客户端界面人工操作）：

```bash
# 1) 构建 CARLA 镜像（首次）
./scripts/build-carla-image.sh

# 2) 启动全部节点
./scripts/start-all-nodes.sh

# 3) 等待健康
./scripts/wait-for-health.sh

# 4) 整链验证
./scripts/verify-client-to-carla-step-by-step.sh

# 5) 控制链路验证（依据日志）
./scripts/verify-client-control-carla.sh
```

若 4、5 均通过，则**客户端工程能够正常控制 CARLA 仿真系统中的车辆**（自动化链路与日志判断均符合预期）；最后可在步骤 F 中打开客户端界面做人工确认。

---

## 4. 常见失败与排查

| 现象 | 排查 |
|------|------|
| carla-server 未运行 | 先执行 `./scripts/start-all-nodes.sh`（不要 `SKIP_CARLA=1`）；或单独 `docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.carla.yml up -d carla` |
| 4/7 或 5/7 超时（ZLM 无流） | 查看 CARLA 日志：`docker logs carla-server 2>&1 | tail -80`，确认 Bridge 是否收到 start_stream、是否推流到 ZLM |
| verify-client-control-carla 未看到 [Control] | 确认容器内运行的是 C++ Bridge 且已连 MQTT；日志中应有 MQTT 订阅与 vehicle/control 接收记录 |
| 客户端拉流黑屏 | 检查 Backend 返回的 WHEP URL 是否可达；ZLM 是否有对应 app/stream；见 [TROUBLESHOOTING_RUNBOOK.md](TROUBLESHOOTING_RUNBOOK.md) |
